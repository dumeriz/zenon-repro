#include "node_connection.hpp"
#include "proxy.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace reverse
{
    struct proxy_opts
    {
        uint16_t public_port;
        std::string znn_node_url;
        uint16_t znn_node_port;
        uint16_t timeout;

        // only needed for wss-proxies
        std::string keyfile;
        std::string certfile;
    };

    enum class proto : bool
    {
        ws,
        wss
    };

    class proxy_fabric
    {
        std::list<proxy> proxies_;

        auto is_connected(client_handler_ptr const& ptr) { return ptr && static_cast<bool>(*ptr); }

        auto try_node_connector(client_handler_factory const& nc)
        {
            try
            {
                return is_connected(nc());
            }
            catch (connection_error const& ce)
            {
                return false;
            }
        }

        auto insert_and_start(proto method, proxy_opts const& opts, client_handler_factory nc)
        {
            try
            {
                proxies_.emplace_back(proxies_.size(), opts.public_port, nc);

                if (method == proto::wss)
                {
                    proxies_.back().wss(opts.timeout, opts.keyfile, opts.certfile);
                }
                else
                {
                    proxies_.back().ws(opts.timeout);
                }
                return std::make_pair(true, "");
            }
            catch (proxy_error const& err)
            {
                proxies_.pop_back();
                return std::make_pair(false, err.what());
            }
        }

    public:
        auto add_proxy(proto type, proxy_opts opts) -> std::pair<bool, size_t>
        {
            log_info("Starting {}-proxy for {}:{} <-> {}", type == proto::wss ? "wss" : "ws", opts.znn_node_url,
                     opts.znn_node_port, opts.public_port);

            uint16_t const connection_timeout_s{1};
            auto connector{make_node_connection_method(opts.znn_node_url, opts.znn_node_port, connection_timeout_s)};

            try_node_connector(connector);

            if (auto res{insert_and_start(type, opts, connector)}; res.first)
            {
                return std::make_pair(true, proxies_.size() - 1);
            }
            else
            {
                log_error("Adding Proxy {} failed: {}", proxies_.size(), res.second);
                return std::make_pair(false, 0);
            }
        }

        auto close()
        {
            log_info("Stopping {} listeners", proxies_.size());
            for (auto&& proxy : proxies_)
            {
                proxy.close();
            }
        }
    };

} // namespace reverse
