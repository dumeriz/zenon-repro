#pragma once

#include "quill/LogLevel.h"
#include <exception>
#include <ios>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace reverse::config
{
    class exception : public std::exception
    {
        std::string const reason_;

    public:
        explicit exception(std::string_view reason) : std::exception{}, reason_{reason} {}

        auto what() const noexcept -> char const* override { return reason_.data(); }
    };

    struct proxy
    {
        std::string node;
        bool wss;
        uint16_t port;
        uint16_t timeout;
    };

    struct options
    {
        std::vector<proxy> proxies;
        std::string certificates;
    };

    auto operator<<(std::ostream& os, proxy const& proxy) -> std::ostream&
    {
        os << "Node=" << proxy.node << ", WSS=" << std::boolalpha << proxy.wss << ", Port=" << proxy.port
           << ", Timeout=" << proxy.timeout;
        return os;
    }

    auto operator<<(std::ostream& os, options const& opt) -> std::ostream&
    {
        os << "Proxies: " << std::endl;
        for (size_t i{}; i < opt.proxies.size(); i++)
        {
            os << " " << i << ": " << opt.proxies[i] << std::endl;
        }
        if (!opt.certificates.empty())
        {
            os << opt.certificates << std::endl;
        }
        return os;
    }

    auto to_string(options const& opts)
    {
        std::ostringstream oss;
        oss << opts;
        return oss.str();
    }

    namespace detail
    {

        inline auto get_home_folder() -> std::filesystem::path
        {
            if (auto const* home = getenv("HOME"); home)
            {
                return home;
            }
            return getpwuid(getuid())->pw_dir;
        }

        inline auto get_config_folder() -> std::filesystem::path
        {
            return std::filesystem::path(get_home_folder()) / ".config" / "znn-repro";
        }

        inline auto get_config_file() -> std::filesystem::path { return get_config_folder() / "config.json"; }

        template <typename T> inline auto get_or_throw(nlohmann::json const& object, std::string_view key) -> T
        {
            if (object.contains(key))
            {
                return object.at(key.data()).get<T>(); // throws if wrong type
            }

            throw exception{std::string("Key ") + key.data() + " missing"};
        }
    } // namespace detail

    inline auto any_wss(options const& opts)
    {
        return std::any_of(opts.proxies.begin(), opts.proxies.end(), [](auto&& proxy) { return proxy.wss; });
    }

    inline auto node_url(proxy const& proxy)
    {
        auto end = proxy.node.find_last_of(':');
        return end != std::string::npos ? proxy.node.substr(0, end) : "";
    }

    inline auto node_port(proxy const& proxy) -> uint16_t
    {
        auto end = proxy.node.find_last_of(':');
        return end != std::string::npos && end < proxy.node.size() ? std::stol(proxy.node.substr(end + 1)) : 1;
    }

    inline auto read_config_file() -> options
    {
        std::ifstream ifs(detail::get_config_file());

        if (ifs.is_open())
        {
            options opts;
            auto const json = nlohmann::json::parse(ifs);
            auto const proxies = detail::get_or_throw<nlohmann::json>(json, "proxies");
            for (auto&& proxy : proxies)
            {
                auto const node = detail::get_or_throw<std::string>(proxy, "node");
                auto const wss = detail::get_or_throw<bool>(proxy, "wss");
                auto const port = detail::get_or_throw<uint16_t>(proxy, "port");
                auto const timeout = detail::get_or_throw<uint16_t>(proxy, "timeout");

                opts.proxies.push_back({.node = node, .wss = wss, .port = port, .timeout = timeout});
            }

            opts.certificates = detail::get_or_throw<std::string>(json, "certificates");

            if (opts.certificates.empty() && any_wss(opts))
            {
                throw exception{"Key 'certificates' empty but wss requested"};
            }

            return opts;
        }

        throw exception{"Could not read configuration file"};
    }

    inline auto quill_log_level(options const&)
    {
        static const std::unordered_map<std::string, quill::LogLevel> mapping = {{"debug", quill::LogLevel::Debug},
                                                                                 {"info", quill::LogLevel::Info},
                                                                                 {"warning", quill::LogLevel::Warning},
                                                                                 {"error", quill::LogLevel::Error},
                                                                                 {"all", quill::LogLevel::TraceL3}};

        return mapping.at("info");
    }
} // namespace reverse::config
