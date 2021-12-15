#pragma once

#include "connection_ws.hpp" // zenon-sdk-cpp
#include "quill/detail/LogMacros.h"

#include <quill/Quill.h>

#include <cstdint>
#include <string>

namespace proxy::handler
{
    class handler
    {
        sdk::ws_connector client_;
        quill::Logger* logger_;

    public:
        handler(std::string const& host, uint16_t port) : client_{host, port}, logger_{quill::get_logger()} {}

        inline auto operator()(std::string request)
        {
            auto response = client_.Send(request);
            LOG_DEBUG(logger_, "{} => {}", request, response);
            return response;
            // return client_.Send(request);
        }
    };
} // namespace proxy::handler
