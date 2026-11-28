// shared_cockpit - this computer's network addresses (shown to the host)
//
// So the host can tell the others what to type: the LAN address for people in
// the same house, the ZeroTier / Tailscale address for people far away. IPv4
// only (ENet 1.3 speaks IPv4). getifaddrs on Linux / macOS, GetAdaptersAddresses
// on Windows. No ENet here.

#pragma once

#include <string>
#include <vector>

namespace sc::net {

struct LocalAddress {
    std::string ip;          // "192.168.1.20"
    std::string adapter;     // "eth0", "Wi-Fi", "ZeroTier One [8056c2e21c]"
    std::string kind;        // "LAN", "Tailscale", "ZeroTier", "Internet / other"
};

// The IPv4 addresses of the adapters that are up (loopback left out), LAN first.
std::vector<LocalAddress> LocalAddresses_cai();

// This computer's name ("saar-pc"; empty when unknown). Others may join with it where the network
// resolves names (most home LANs: the name, or name.local on macOS / Linux; Tailscale MagicDNS).
std::string MachineName_cai();

} // namespace sc::net
