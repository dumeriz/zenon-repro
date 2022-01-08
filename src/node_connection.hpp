#pragma once

#include "logging.h"
#include "proxy.hpp"
#include <connection_ws.hpp> // zenon-sdk-cpp

#include <chrono>
#include <cstdint>
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

    class node_connection : public client_handler
    {
        std::unique_ptr<sdk::ws_connector> client_;

    public:
        node_connection(std::string const& host, uint16_t port)
            : client_{std::make_unique<sdk::ws_connector>(host, port)}
        {
            if (!client_->connected())
            {
                throw connection_error{"Could not connect to " + host + ":" + std::to_string(port)};
            }
        }

        node_connection(std::string const& host, uint16_t port, uint16_t timeout_s)
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

        ~node_connection() override = default;

        inline auto operator()(std::string request) -> std::string override
        {
            auto response = client_->Send(request);
            log_debug("{} => {}", request, response);
            return response;
            // return client_.Send(request);
        }

        operator bool() const override { return client_->connected(); }
    };

    auto make_node_connection_method(std::string host, uint16_t port, uint16_t timeout) -> client_handler_factory
    {
        auto nc = [=]() -> client_handler_ptr { return std::make_unique<node_connection>(host, port, timeout); };
        return nc;
    }
} // namespace reverse
