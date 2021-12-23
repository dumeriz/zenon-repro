#include "App.h"
#include "WebSocket.h"
#include "WebSocketData.h"
#include "WebSocketProtocol.h"
#include "libusockets.h"
#include "quill/detail/LogMacros.h"
#include "request_handler.hpp"

#include <cstdint>
#include <exception>
#include <future>
#include <quill/Quill.h>
#include <string>
#include <thread>

namespace reverse
{
    struct unused_per_socket_data
    {
    };

    class proxy_error : std::exception
    {
        std::string const reason_;

    public:
        proxy_error(std::string_view reason) : std::exception{}, reason_{reason} {}
        auto what() const noexcept -> const char* override { return reason_.data(); }
    };

    namespace detail
    {

        // repeating the definition from uws:Websocket.h here, as it is burried there in a templated class unnecessarily
        template <bool SSL> using uws_result_t = typename uWS::WebSocket<SSL, true, unused_per_socket_data>::SendStatus;

        // used to derive the exact uws_result_t type from an App-template argument
        template <typename App> struct ssl_bool
        {
        };
        template <> struct ssl_bool<uWS::SSLApp>
        {
            static constexpr bool ssl = true;
        };
        template <> struct ssl_bool<uWS::App>
        {
            static constexpr bool ssl = false;
        };
    } // namespace detail

    class proxy
    {
        size_t id_;
        uint16_t port_;
        std::string node_url_;
        uint16_t node_port_;

        std::thread run_thread_;
        us_listen_socket_t* listen_socket_{nullptr};
        std::atomic_bool reject_connections_{false};

    public:
        proxy(size_t id, uint16_t port, std::string_view node_url, uint16_t node_port)
            : id_{id}, port_{port}, node_url_{node_url}, node_port_{node_port}
        {
        }

        ~proxy() { close(); }

        proxy(proxy const&) = delete;
        proxy(proxy&& other) = delete;
        proxy& operator=(proxy const&) = delete;
        proxy& operator=(proxy&&) = delete;

        auto ws(uint16_t timeout) { start_future_or_throw<uWS::App>(timeout, uWS::SocketContextOptions{}); }

        auto wss(uint16_t timeout, std::string keyfile, std::string certfile)
        {
            start_future_or_throw<uWS::SSLApp>(timeout, uWS::SocketContextOptions{.key_file_name = keyfile.c_str(),
                                                                                  .cert_file_name = certfile.c_str()});
        }

        auto close() -> void
        {
            reject_connections_.store(true);
            if (listen_socket_)
            {
                LOG_INFO(quill::get_logger(), "{}: Closing socket", id_);
                us_listen_socket_close(0, listen_socket_);
            }

            listen_socket_ = nullptr;

            if (run_thread_.joinable())
            {
                LOG_INFO(quill::get_logger(), "{}: Waiting for thread", id_);
                run_thread_.join();
            }
        }

    private:
        template <typename App> auto start_future_or_throw(uint16_t timeout, uWS::SocketContextOptions opts) -> void
        {
            reject_connections_.store(false);
            std::promise<void> startup_barrier;
            auto startup_result = startup_barrier.get_future();
            run_thread_ = std::thread(&proxy::run<App>, this, timeout, opts, std::move(startup_barrier));

            try
            {
                startup_result.get();
            }
            catch (connection_error const& ce)
            {
                throw proxy_error(ce.what());
            }
        }

        template <typename App>
        auto run(uint16_t timeout, uWS::SocketContextOptions opts, std::promise<void> startup_signal) -> void
        {
            constexpr auto is_ssl = detail::ssl_bool<std::decay_t<App>>::ssl;
            using TApp = typename uWS::TemplatedApp<is_ssl>;
            TApp uws_app{opts};

            auto* logger{quill::get_logger()};
            std::unique_ptr<handler> node_link;

            try
            {
                size_t connection_timeout_s = 3;
                node_link = std::make_unique<handler>(node_url_, node_port_, 3);
            }
            catch (connection_error const& err)
            {
                startup_signal.set_exception(std::current_exception());
                return;
            }

            startup_signal.set_value();

            LOG_INFO(logger, "{}: Connected to node @ {}:{}", id_, node_url_, node_port_);

            // keep the lambdas more readable by preventing clang-format from putting the brace at the line end

            // on-message: forward-copy the received message to the asynchronously called handler
            auto const on_message = [this, &node_link, logger, timeout](auto* ws, std::string_view message,
                                                                        uWS::OpCode opcode) {
                if (reject_connections_)
                {
                    ws->end(); // send FIN and close socket
                    return;
                }

                if (opcode != uWS::TEXT)
                {
                    LOG_WARNING_NOFN(logger, "{}: Ignoring non-TEXT message (opcode {})", id_,
                                     static_cast<int>(opcode));
                    return;
                }

                if (!*node_link)
                {
                    LOG_ERROR_NOFN(logger, "{}: Handler in invalid state; discarding message", id_);
                    return;
                }

                std::string msg{message};
                auto result =
                    std::async(std::launch::async, [m = std::move(msg), &node_link] { return (*node_link)(m); });

                if (std::future_status::ready != result.wait_for(std::chrono::milliseconds(timeout)))
                {
                    LOG_ERROR_NOFN(logger,
                                   "{}: TIMEOUT after {}ms awaiting the handler result\n"
                                   "If this happens often increase the timeout value",
                                   id_, timeout);
                    return;
                }

                auto const response = result.get();

                LOG_DEBUG_NOFN(logger, "{}: Received response {}", id_, response);

                using result_t = detail::uws_result_t<detail::ssl_bool<std::decay_t<App>>::ssl>;
                if (auto code = ws->send(response, uWS::OpCode::TEXT); code != result_t::SUCCESS)
                {
                    LOG_ERROR_NOFN(logger, "{}: SEND returned {}", id_, code);
                }
            };

            // the other callbacks are mostly just logging handlers

            auto const on_open = [this, logger](auto* ws) {
                if (reject_connections_.load())
                {
                    LOG_DEBUG_NOFN(logger, "{}: Rejecting connection from {}", id_, ws->getRemoteAddressAsText());
                    ws->close();
                }
                else
                {
                    LOG_INFO_NOFN(logger, "{}: Connection from {}", id_, ws->getRemoteAddressAsText());
                }
            };

            auto const on_close = [this, logger](auto* ws, int code, std::string_view message) {
                LOG_INFO_NOFN(logger, "{}: CLOSE with remote={}. Code={}, message={}", id_,
                              ws->getRemoteAddressAsText(), code, message);
            };

            auto const on_drain = [this, logger](auto* ws) {
                auto amount = ws->getBufferedAmount();
                if (amount)
                {
                    LOG_DEBUG_NOFN(logger, "{}: ====> buffered data: {}", id_, amount);
                }
            };

            auto const on_listen = [&](auto* listen_socket) {
                if (listen_socket)
                {
                    listen_socket_ = listen_socket;
                    LOG_DEBUG_NOFN(logger, "{}: Listening on port {}", id_, port_);
                }
            };

            using ws_behavior_t = typename TApp::template WebSocketBehavior<unused_per_socket_data>;

            ws_behavior_t behavior = {
                .compression = uWS::CompressOptions(uWS::DEDICATED_COMPRESSOR_4KB | uWS::DEDICATED_DECOMPRESSOR),
                .maxPayloadLength = 100 * 1024 * 1024,
                .idleTimeout = 16,
                .maxBackpressure = 100 * 1024 * 1024, // enforce DROPPED when clients are too slow
                .closeOnBackpressureLimit = false,
                .resetIdleTimeoutOnSend = false,
                .sendPingsAutomatically = true,
                .upgrade = nullptr,
                .open = on_open,
                .message = on_message,
                .drain = on_drain,
                .ping = nullptr, // auto pings enabled above
                .pong = nullptr,
                .close = on_close};

            uws_app.template ws<unused_per_socket_data>("/*", std::move(behavior)).listen(port_, on_listen).run();

            LOG_INFO(logger, "{}: Listener fallthrough", id_);
        }
    };
} // namespace reverse
