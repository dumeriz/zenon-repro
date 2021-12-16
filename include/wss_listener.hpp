#include "App.h"
#include "WebSocket.h"
#include "WebSocketData.h"
#include "WebSocketProtocol.h"
#include "quill/detail/LogMacros.h"
#include "request_handler.hpp"

#include <cstdint>
#include <future>
#include <quill/Quill.h>
#include <string>
#include <thread>

namespace reverse
{
    struct unused_per_socket_data
    {
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

    template <typename App>
    inline auto listener(std::string_view mount_path, uint16_t port, uint16_t node_port, uint16_t timeout,
                         App&& uws_app)
    {
        constexpr auto is_ssl = detail::ssl_bool<App>::ssl;
        using TApp = typename uWS::TemplatedApp<is_ssl>;

        auto* logger{quill::get_logger()};

        proxy::handler::handler handler("ws://localhost", node_port);

        if (!handler)
        {
            LOG_ERROR_NOFN(logger, "No connection to the node");
            return;
        }

        // keep the lambdas more readable by preventing clang-format from putting the brace at the line end
        // clang-format off

        // on-message: forward-copy the received message to the asynchronously called handler

        auto const on_message = [&handler, logger, timeout](auto ws, std::string_view message, uWS::OpCode opcode)
        {
            if (opcode != uWS::TEXT)
            {
                LOG_WARNING_NOFN(logger, "Ignoring non-TEXT message (opcode {})", static_cast<int>(opcode));
                return;
            }

            if (!handler)
            {
                LOG_ERROR_NOFN(logger, "Handler in invalid state; discarding message");
                return;
            }

            std::string msg{message};
            auto result = std::async(std::launch::async, [m = std::move(msg), &handler] { return handler(m); });

            if (std::future_status::ready != result.wait_for(std::chrono::milliseconds(timeout)))
            {
                LOG_ERROR_NOFN(logger, "TIMEOUT after {}ms awaiting the handler result\n"
                        "If this happens often increase the timeout value", timeout);
                return;
            }

            auto const response = result.get();

            LOG_INFO_NOFN(logger, "Received response {}", response);

            using result_t = detail::uws_result_t<detail::ssl_bool<App>::ssl>;
            if (auto code = ws->send(response, uWS::OpCode::TEXT); code != result_t::SUCCESS)
            {
                LOG_ERROR_NOFN(logger, "SEND returned {}", code);
            }
        };

        // the other callbacks are just logging handlers currently
        
        auto const on_open = [logger](auto* ws)
        {
            LOG_INFO_NOFN(logger, "Connection from {}", ws->getRemoteAddressAsText());
        };

        auto const on_close = [logger](auto* ws, int code, std::string_view message)
        {
            LOG_INFO_NOFN(logger, "CLOSE with remote={}. Code={}, message={}", ws->getRemoteAddressAsText(), code,
                          message);
        };

        auto const on_drain = [logger](auto* ws)
        {
            auto amount = ws->getBufferedAmount();
            if (amount)
            {
                LOG_DEBUG_NOFN(logger, "====> buffered data: {}", amount);
            }
        };

        auto const on_listen = [&](auto* listen_socket)
        {
            if (listen_socket)
            {
                LOG_INFO_NOFN(logger, "Listening on port {}", port);
            }
        };
        // clang-format on

        using ws_behavior_t = typename TApp::template WebSocketBehavior<unused_per_socket_data>;

        ws_behavior_t behavior = {.compression =
                                      uWS::CompressOptions(uWS::DEDICATED_COMPRESSOR_4KB | uWS::DEDICATED_DECOMPRESSOR),
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

        uws_app.template ws<unused_per_socket_data>(mount_path.data(), std::move(behavior))
            .listen(port, on_listen)
            .run();
    }
} // namespace reverse
