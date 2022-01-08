#pragma once

#include "App.h"
#include "WebSocket.h"
#include "WebSocketData.h"
#include "WebSocketProtocol.h"
#include "libusockets.h"
#include "logging.h"

#include <cstdint>
#include <exception>
#include <future>
#include <string>
#include <thread>

namespace reverse
{
    /// @brief A new instance of client_handler that will be associated with every new websocket client.
    struct client_handler
    {
        virtual ~client_handler() = default;

        /// @brief Process a single request from a websocket client.
        ///
        /// This method will be called in an async context.
        ///
        /// @param[in] request
        /// @return the response to be send to the requesting client.
        virtual auto operator()(std::string request) -> std::string = 0;

        /// @brief State of this handler - if false, connected websocketclients will be disconnected and no new
        /// connections will be accepted.
        virtual operator bool() const = 0;
    };

    using client_handler_ptr = std::unique_ptr<client_handler>;

    /// @brief Generates a new client_handler.
    using client_handler_factory = std::function<client_handler_ptr()>;

    struct per_socket_data
    {
        client_handler_ptr handler;
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
            using websocket_t = uWS::WebSocket<ssl, true, per_socket_data>;
        };
        template <> struct ssl_bool<uWS::App>
        {
            static constexpr bool ssl = false;
            using websocket_t = uWS::WebSocket<ssl, true, per_socket_data>;
        };

        template <typename App> using websocket_t = typename ssl_bool<std::decay_t<App>>::websocket_t;

        template <typename App>
        auto constexpr send_expect_result = uws_result_t<ssl_bool<typename std::decay_t<App>>::ssl>::SUCCESS;

        inline auto is_handler_valid(client_handler_ptr const& h) { return h && static_cast<bool>(*h); }

        inline auto make_client_handler(client_handler_factory nc) { return nc(); }

        inline auto client_handler_execute(client_handler_ptr const& ncp, std::string request, uint16_t timeout)
            -> std::optional<std::string>
        {
            auto fut = std::async(std::launch::async, [&] { return (*ncp)(request); });

            if (std::future_status::ready == fut.wait_for(std::chrono::milliseconds(timeout)))
            {
                return std::make_optional(fut.get());
            }
            return std::nullopt;
        }
    } // namespace detail

    class proxy
    {
        size_t id_;
        uint16_t port_;
        client_handler_factory make_client_handler_;

        std::thread run_thread_;
        us_listen_socket_t* listen_socket_{nullptr};
        std::atomic_bool reject_connections_{false};

    public:
        proxy(size_t id, uint16_t port, client_handler_factory nc) : id_{id}, port_{port}, make_client_handler_{nc} {}

        ~proxy()
        {
            if (!reject_connections_.load())
            {
                close();
            }

            if (run_thread_.joinable())
            {
                log_info("{}: Waiting for thread", id_);
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
            log_info("{}: Shutdown", id_);

            reject_connections_.store(true);
            if (listen_socket_)
            {
                log_info("{}: Closing socket", id_);
                us_listen_socket_close(0, listen_socket_);
            }

            listen_socket_ = nullptr;
        }

        template <typename App> auto send_data(typename detail::websocket_t<App>* ws, std::string const& data)
        {
            if (auto code = ws->send(data, uWS::OpCode::TEXT); code != detail::send_expect_result<App>)
            {
                log_error("{}: SEND returned {}", id_, code);
            }
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
                    log_warning("{}: Ignoring non-TEXT message (opcode {})", id_, static_cast<int>(opcode));
                    return;
                }

                auto& handler{ws->getUserData()->handler};
                if (!detail::is_handler_valid(handler))
                {
                    log_error("{}: Handler in invalid state; discarding message", id_);
                    return;
                }

                if (auto response{detail::client_handler_execute(handler, message.data(), timeout)})
                {
                    log_debug("{}: Sending {}", id_, response.value());
                    send_data<App>(ws, response.value());
                }
                else
                {
                    log_error("{}: TIMEOUT awaiting the handler result. "
                              "If this happens often increase the timeout value",
                              id_);
                }
            };

            // on_open: instantiate a handler for the new websocket client
            auto const on_open = [this](auto* ws)
            {
                if (reject_connections_.load())
                {
                    log_debug("{}: Rejecting connection from {}", id_, ws->getRemoteAddressAsText());
                    ws->close();
                    return;
                }

                log_info("{}: Connection from {}", id_, ws->getRemoteAddressAsText());
                auto* ud{ws->getUserData()};

                ud->handler = make_client_handler_();

                if (!detail::is_handler_valid(ud->handler))
                {
                    log_error("{}: Invalidated handler", id_);
                    ws->close();
                    return;
                }
            };

            auto const on_close = [this](auto* ws, int code, std::string_view message)
            {
                ws->getUserData()->handler.reset();
                log_info("{}: CLOSE with remote={}. Code={}, message={}", id_, ws->getRemoteAddressAsText(), code,
                         message);
            };

            auto const on_drain = [this](auto* ws)
            {
                auto amount = ws->getBufferedAmount();
                if (amount)
                {
                    log_debug("{}: ====> buffered data: {}", id_, amount);
                }
            };

            auto const on_listen = [&](auto* listen_socket)
            {
                if (listen_socket)
                {
                    listen_socket_ = listen_socket;
                    log_debug("{}: Listening on port {}", id_, port_);
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

            log_info("{}: Listener fallthrough", id_);
        }
    };
} // namespace reverse
