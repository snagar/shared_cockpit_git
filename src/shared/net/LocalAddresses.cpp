// shared_cockpit - this computer's network addresses (shown to the host)

#include "shared/net/LocalAddresses.h"

#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::net {

namespace {

// What kind of network an address belongs to (by its range and the adapter's name).
std::string KindOf_cai(std::uint32_t ip, const std::string& adapter)
{
    const std::string name = Utils::Lower_cai(adapter);
    const std::uint8_t a = static_cast<std::uint8_t>(ip >> 24);
    const std::uint8_t b = static_cast<std::uint8_t>(ip >> 16);
    if (Utils::Contains_cai(name, "zerotier") || name.rfind("zt", 0) == 0)
        return "ZeroTier";
    if (Utils::Contains_cai(name, "tailscale") || (a == 100 && (b & 0xC0) == 64))   // 100.64.0.0/10
        return "Tailscale";
    if (a == 10 || (a == 172 && (b & 0xF0) == 16) || (a == 192 && b == 168))
        return "LAN";
    if (a == 169 && b == 254)
        return "link-local";
    return "Internet / other";
}

// Sort order: LAN, ZeroTier, Tailscale, the rest.
int Rank_cai(const std::string& kind)
{
    if (kind == "LAN") return 0;
    if (kind == "ZeroTier") return 1;
    if (kind == "Tailscale") return 2;
    if (kind == "link-local") return 4;
    return 3;
}

// Adds one address (host byte order).
void Add_cai(std::vector<LocalAddress>& out, std::uint32_t ip, const std::string& adapter)
{
    if ((ip >> 24) == 127)
        return;
    const std::string text = fmt::format("{}.{}.{}.{}", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    for (const LocalAddress& known : out)
        if (known.ip == text)
            return;
    out.push_back({text, adapter, KindOf_cai(ip, adapter)});
}

} // namespace

// The IPv4 addresses of the adapters that are up.
std::vector<LocalAddress> LocalAddresses_cai()
{
    std::vector<LocalAddress> out;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buffer(size);
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) {
        LOG_WARN("GetAdaptersAddresses failed ({})", static_cast<unsigned long>(result));
        return out;
    }
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        std::string name;
        if (adapter->FriendlyName) {
            const int length = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (length > 1) {
                name.resize(static_cast<std::size_t>(length - 1));
                WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, name.data(), length, nullptr, nullptr);
            }
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const sockaddr* address = unicast->Address.lpSockaddr;
            if (!address || address->sa_family != AF_INET)
                continue;
            const auto* in = reinterpret_cast<const sockaddr_in*>(address);
            Add_cai(out, ntohl(in->sin_addr.s_addr), name);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        LOG_WARN("getifaddrs failed");
        return out;
    }
    for (ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP) ||
            (entry->ifa_flags & IFF_LOOPBACK))
            continue;
        const auto* in = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        Add_cai(out, ntohl(in->sin_addr.s_addr), entry->ifa_name ? entry->ifa_name : "");
    }
    freeifaddrs(list);
#endif
    auto lmbda_byRank = [](const LocalAddress& a, const LocalAddress& b) { return Rank_cai(a.kind) < Rank_cai(b.kind); };
    std::stable_sort(out.begin(), out.end(), lmbda_byRank);
    std::string text;
    for (const LocalAddress& a : out)
        text += fmt::format("{}{} ({}, {})", text.empty() ? "" : ", ", a.ip, a.kind, a.adapter);
    LOG_DEBUG("local addresses: {}", text.empty() ? std::string("none") : text);
    return out;
}

} // namespace sc::net
