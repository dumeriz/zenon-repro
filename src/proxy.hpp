#include "App.h"
#include "WebSocket.h"
#include "WebSocketData.h"
#include "WebSocketProtocol.h"
#include "libusockets.h"
#include "logging.h"
#include "request_handler.hpp"

#include <cstdint>
#include <exception>
#include <future>
#include <string>
#include <thread>

namespace reverse
{
    struct per_socket_data
    {
        std::unique_ptr<handler> node_link;
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
        template <bool SSL> using uws_result_t = typename uWS::WebSocket<SSL, true, per_socket_data>::SendStatus;

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

        ~proxy()
        {
            if (!reject_connections_.load())
            {
                close();
            }

            if (run_thread_.joinable())
            {
                logging::info("{}: Waiting for thread", id_);
                run_thread_.join();
            }
        }

        proxy(proxy const&) = delete;
        proxy(proxy&& other) = delete;
        proxy& operator=(proxy const&) = delete;
        proxy& operator=(proxy&&) = delete;

        auto ws(uint16_t timeout) { run_thread_ = std::thread(&proxy::run<uWS::App>, this, timeout, "", ""); }

        auto wss(uint16_t timeout, std::string keyfile, std::string certfile)
        {
            run_thread_ = std::thread(&proxy::run<uWS::SSLApp>, this, timeout, keyfile, certfile);
        }

        auto close() -> void
        {
            logging::info("{}: Shutdown", id_);

            reject_connections_.store(true);
            if (listen_socket_)
            {
                logging::info("{}: Closing socket", id_);
                us_listen_socket_close(0, listen_socket_);
            }

            listen_socket_ = nullptr;
        }

    private:
        template <typename App> auto run(uint16_t timeout, std::string keyfile, std::string certfile) -> void
        {
            // on-message: forward-copy the received message to the asynchronously called handler
            auto const on_message = [this, timeout](auto* ws, std::string_view message, uWS::OpCode opcode)
            {
                if (reject_connections_)
                {
                    ws->end(); // send FIN and close socket
                    return;
                }

                if (opcode != uWS::TEXT)
                {
                    logging::warning("{}: Ignoring non-TEXT message (opcode {})", id_, static_cast<int>(opcode));
                    return;
                }

                auto& node_link{ws->getUserData()->node_link};
                if (!*node_link)
                {
                    logging::error("{}: Handler in invalid state; discarding message", id_);
                    return;
                }

                std::string msg{message};
                auto result =
                    std::async(std::launch::async, [m = std::move(msg), &node_link] { return (*node_link)(m); });

                if (std::future_status::ready != result.wait_for(std::chrono::milliseconds(timeout)))
                {
                    logging::error("{}: TIMEOUT after {}ms awaiting the handler result\n"
                                   "If this happens often increase the timeout value",
                                   id_, timeout);
                    return;
                }

                auto const response = result.get();

                logging::debug("{}: Received response {}", id_, response);

                using result_t = detail::uws_result_t<detail::ssl_bool<std::decay_t<App>>::ssl>;
                if (auto code = ws->send(response, uWS::OpCode::TEXT); code != result_t::SUCCESS)
                {
                    logging::error("{}: SEND returned {}", id_, code);
                }
            };

            // on_open: open a connection to the node for each websocket connection
            auto const on_open = [this](auto* ws)
            {
                if (reject_connections_.load())
                {
                    logging::debug("{}: Rejecting connection from {}", id_, ws->getRemoteAddressAsText());
                    ws->close();
                    return;
                }

                logging::info("{}: Connection from {}", id_, ws->getRemoteAddressAsText());
                auto* ud{ws->getUserData()};

                try
                {
                    size_t const connection_timeout_s = 3;
                    ud->node_link = std::make_unique<handler>(node_url_, node_port_, connection_timeout_s);
                }
                catch (connection_error const& err)
                {
                    logging::error("{}: Connection to node failed @ {}:{}", id_, node_url_, node_port_);
                    ws->close();
                    return;
                }

                logging::info("{}: Connected to node @ {}:{}", id_, node_url_, node_port_);
            };

            auto const on_close = [this](auto* ws, int code, std::string_view message)
            {
                ws->getUserData()->node_link.reset();
                logging::info("{}: CLOSE with remote={}. Code={}, message={}", id_, ws->getRemoteAddressAsText(), code,
                              message);
            };

            auto const on_drain = [this](auto* ws)
            {
                auto amount = ws->getBufferedAmount();
                if (amount)
                {
                    logging::debug("{}: ====> buffered data: {}", id_, amount);
                }
            };

            auto const on_listen = [&](auto* listen_socket)
            {
                if (listen_socket)
                {
                    listen_socket_ = listen_socket;
                    logging::debug("{}: Listening on port {}", id_, port_);
                }
            };

            constexpr auto is_ssl = detail::ssl_bool<std::decay_t<App>>::ssl;
            using TApp = typename uWS::TemplatedApp<is_ssl>;
            using ws_behavior_t = typename TApp::template WebSocketBehavior<per_socket_data>;

            TApp uws_app{uWS::SocketContextOptions{.key_file_name = keyfile.empty() ? nullptr : keyfile.data(),
                                                   .cert_file_name = certfile.empty() ? nullptr : certfile.data()}};

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

            uws_app.template ws<per_socket_data>("/*", std::move(behavior)).listen(port_, on_listen).run();

            logging::info("{}: Listener fallthrough", id_);
        }
    };
} // namespace reverse
