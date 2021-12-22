#pragma once

#include "connection_ws.hpp" // zenon-sdk-cpp
#include "quill/detail/LogMacros.h"

#include <chrono>
#include <cstdint>
#include <quill/Quill.h>
#include <string>
#include <thread>

namespace reverse
{
    class connection_error : std::exception
    {
        std::string const reason_;

    public:
        connection_error(std::string_view reason) : std::exception{}, reason_{reason} {}
        auto what() const noexcept -> const char* override { return reason_.data(); }
    };

    class handler
    {
        std::unique_ptr<sdk::ws_connector> client_;
        quill::Logger* logger_;

    public:
        handler(std::string const& host, uint16_t port)
            : client_{std::make_unique<sdk::ws_connector>(host, port)}, logger_{quill::get_logger()}
        {
            if (!client_->connected())
            {
                throw connection_error{"Could not connect to " + host + ":" + std::to_string(port)};
            }
        }

        handler(std::string const& host, uint16_t port, uint16_t timeout_s) : logger_{quill::get_logger()}
        {
            auto timeout{std::chrono::system_clock::now() + std::chrono::seconds(timeout_s)};

            while (std::chrono::system_clock::now() < timeout)
            {
                client_ = std::make_unique<sdk::ws_connector>(host, port);
                if (client_->connected()) break;

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            if (!client_->connected())
            {
                throw connection_error{"Could not connect to " + host + ":" + std::to_string(port)};
            }
        }

        inline auto operator()(std::string request)
        {
            auto response = client_->Send(request);
            LOG_DEBUG(logger_, "{} => {}", request, response);
            return response;
            // return client_.Send(request);
        }

        operator bool() const { return client_->connected(); }
    };
} // namespace reverse
