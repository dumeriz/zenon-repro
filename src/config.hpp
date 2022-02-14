#pragma once

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
#ifdef __APPLE__
            return std::filesystem::path(get_home_folder()) / "Library" / "znn-repro";
#elif __linux__
            return std::filesystem::path(get_home_folder()) / ".config" / "znn-repro";
#endif
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

    inline auto operator<<(std::ostream& os, proxy const& proxy) -> std::ostream&
    {
        os << "Node=" << proxy.node << ", WSS=" << std::boolalpha << proxy.wss << ", Port=" << proxy.port
           << ", Timeout=" << proxy.timeout;
        return os;
    }

    inline auto operator<<(std::ostream& os, options const& opt) -> std::ostream&
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

    inline auto to_string(options const& opts)
    {
        std::ostringstream oss;
        oss << opts;
        return oss.str();
    }

    inline auto to_json(nlohmann::json& js, proxy const& p)
    {
        js["node"] = p.node;
        js["wss"] = p.wss;
        js["port"] = p.port;
        js["timeout"] = p.timeout;
    }

    inline auto from_json(nlohmann::json const& js, proxy& p)
    {
        p.node = detail::get_or_throw<std::string>(js, "node");
        p.wss = detail::get_or_throw<bool>(js, "wss");
        p.port = detail::get_or_throw<uint16_t>(js, "port");
        p.timeout = detail::get_or_throw<uint16_t>(js, "timeout");
    }

    inline auto to_json(nlohmann::json& js, options const& opts)
    {
        js["proxies"] = opts.proxies;
        js["certificates"] = opts.certificates;
    }

    inline auto from_json(nlohmann::json const& js, options& opts)
    {
        opts.proxies = std::vector<proxy>{detail::get_or_throw<nlohmann::json>(js, "proxies")};
        opts.certificates = detail::get_or_throw<std::string>(js, "certificates");
    }

    inline auto default_proxy() -> proxy
    {
        return proxy{.node = "ws://localhost:35998", .wss = false, .port = 35999, .timeout = 100};
    }

    inline auto default_options() -> options { return options{.proxies = {5, default_proxy()}, .certificates = ""}; }

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

    inline auto write_config_file(options const& options)
    {
        std::ofstream ofs(detail::get_config_file());
        if (!ofs.is_open())
        {
            throw exception{"Could not write-access configuration file"};
        }
        nlohmann::json js = options;
        int indent{4};
        ofs << js.dump(indent);
    }

    inline auto create_config_if_not_exists()
    {
        auto config_file{detail::get_config_file()};
        if (!std::filesystem::is_regular_file(config_file))
        {
            auto directory{config_file};
            std::filesystem::create_directories(directory.remove_filename());

            write_config_file(default_options());
        }
    }

    inline auto read_config_file() -> options
    {
        std::ifstream ifs(detail::get_config_file());

        if (ifs.is_open())
        {
            options opts = nlohmann::json::parse(ifs);

            if (opts.certificates.empty() && any_wss(opts))
            {
                throw exception{"Key 'certificates' empty but wss requested"};
            }

            return opts;
        }

        throw exception{"Could not read configuration file"};
    }

} // namespace reverse::config
