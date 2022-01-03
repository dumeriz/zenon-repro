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

    public:
        auto add_proxy(proto type, proxy_opts opts) -> std::pair<bool, size_t>
        {
            logging::info("Starting {}-proxy for {}:{} <-> {}", type == proto::wss ? "wss" : "ws", opts.znn_node_url,
                          opts.znn_node_port, opts.public_port);

            proxies_.emplace_back(proxies_.size(), opts.public_port, opts.znn_node_url, opts.znn_node_port);

            try
            {
                if (type == proto::wss)
                {
                    proxies_.back().wss(opts.timeout, opts.keyfile, opts.certfile);
                }
                else
                {
                    proxies_.back().ws(opts.timeout);
                }
            }
            catch (proxy_error const& err)
            {
                std::cerr << err.what() << std::endl;
                proxies_.pop_back();
                return std::make_pair(false, 0);
            }

            return std::make_pair(true, proxies_.size() - 1);
        }

        auto close()
        {
            logging::info("Stopping all listeners");
            for (auto&& proxy : proxies_)
            {
                proxy.close();
            }
        }
    };

} // namespace reverse
