/**
 * @file addressResolver.hpp
 * @brief Family-agnostic host:port resolution for the transports.
 *
 * The TCP and UDP transports both used `inet_pton(AF_INET, ...)`, which parses
 * a numeric IPv4 literal and nothing else: "localhost" failed, every hostname
 * failed, and IPv6 was unreachable (C24). Both now resolve through
 * `getaddrinfo`, which handles names, IPv4 and IPv6 literals, and returns
 * candidates in the system's preference order.
 *
 * Resolution is a blocking call that can touch DNS. It belongs to setup
 * (start()/configure()), never to a send or receive path.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace axonvex {
namespace interfaces {
namespace detail {

#if defined(AXONVEX_PLATFORM_LINUX)

/// One resolved endpoint. Holds sockaddr_storage so it fits either family.
struct ResolvedAddress {
    sockaddr_storage storage{};
    socklen_t length{0};
    int family{AF_UNSPEC};
    int socktype{0};
    int protocol{0};

    const sockaddr* addr() const noexcept {
        return reinterpret_cast<const sockaddr*>(&storage);
    }
};

/// Resolve @p host and @p port into every candidate address the system offers.
///
/// @param socktype  SOCK_STREAM or SOCK_DGRAM.
/// @param passive   true for an address to bind() to. An empty @p host then
///                  means "any local interface"; without it an empty host is
///                  loopback.
/// @param out       Candidates in the system's preference order. Callers should
///                  try them in order and keep the first that works — a host
///                  with both an A and an AAAA record yields both, and only one
///                  may be reachable.
/// @param error     Human-readable reason on failure.
/// @return false if nothing resolved.
inline bool resolveAddresses(const std::string& host, uint16_t port, int socktype, bool passive,
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
