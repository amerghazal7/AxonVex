#include <axonvex_net/detail/addressResolver.hpp>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <cstring>
#endif

namespace axonvex {
namespace interfaces {
namespace detail {

#if defined(AXONVEX_PLATFORM_LINUX)

bool resolveAddresses(const std::string& host, uint16_t port, int socktype, bool passive,
                      std::vector<ResolvedAddress>& out, std::string& error) {
    out.clear();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC; // IPv4 and IPv6 both acceptable
    hints.ai_socktype = socktype;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }

    const std::string portText = std::to_string(port);
    addrinfo* results = nullptr;
    const char* node = host.empty() ? nullptr : host.c_str();

    const int rc = ::getaddrinfo(node, portText.c_str(), &hints, &results);
    if (rc != 0) {
        error = "cannot resolve '" + (host.empty() ? std::string("(any)") : host) + ":" + portText +
                "': " + ::gai_strerror(rc);
        return false;
    }

    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
        if (it->ai_addrlen > sizeof(sockaddr_storage)) {
            continue; // cannot happen for INET/INET6; skip rather than overflow
        }
        ResolvedAddress entry;
        std::memcpy(&entry.storage, it->ai_addr, it->ai_addrlen);
        entry.length = static_cast<socklen_t>(it->ai_addrlen);
        entry.family = it->ai_family;
        entry.socktype = it->ai_socktype;
        entry.protocol = it->ai_protocol;
        out.push_back(entry);
    }
    ::freeaddrinfo(results);

    if (out.empty()) {
        error = "no usable address for '" + host + ":" + portText + "'";
        return false;
    }
    return true;
}

#endif // AXONVEX_PLATFORM_LINUX

} // namespace detail
} // namespace interfaces
} // namespace axonvex
