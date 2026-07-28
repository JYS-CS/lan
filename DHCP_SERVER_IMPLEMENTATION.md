# Custom C++ DHCP Server — Implementation Reference

**Project:** LAN Monitor Suite  
**Language:** C++20 with Qt6  
**Location:** `core/DhcpServer.h` and `core/DhcpServer.cpp`  
**Status:** ✅ Fully working — verified with real Android client (DORA handshake confirmed via tcpdump)

---

## Summary

This document describes how a fully RFC-2131-compliant DHCP server was implemented in C++ as part of a Qt6 LAN monitoring application. The server runs as a `QThread`, uses `AF_PACKET` raw sockets to send and receive Ethernet frames directly, and successfully performs the full DORA handshake (Discover → Offer → Request → Acknowledge) with real clients.

---

## Why the First Attempt Failed (Standard UDP Sockets)

The original implementation used a standard `SOCK_DGRAM` UDP socket bound to port 67. This approach has a fundamental, non-obvious flaw:

### The Core Problem: The Linux Kernel Cannot Route DHCP Replies

When a DHCP client boots, it has **no IP address**. It sends:
- Source IP: `0.0.0.0`
- Destination IP: `255.255.255.255` (limited broadcast)

The server then needs to reply. With a standard UDP socket the kernel must:
1. Look up a route for `255.255.255.255` → **fails** unless the interface has a configured IP bound to the socket.
2. Or send a unicast reply to the offered IP (e.g. `192.168.8.121`) → **fails** because the client doesn't have that IP yet, so ARP resolution fails.

The kernel silently drops the outgoing packet. The client never receives the OFFER or ACK.

### Additional Bugs in the Original Code

| Bug | Description |
|-----|-------------|
| Wrong `siaddr` | Set to the **router IP** instead of the **server's own IP** |
| Wrong Option 54 (Server Identifier) | Also set to the router IP — clients use this to filter REQUEST packets |
| Reply destination logic | Tried to unicast to `yiaddr` (offered IP) which the client doesn't have yet |
| No `SO_BINDTODEVICE` | Replies could go out on the wrong interface |
| OFFER/ACK IP mismatch | OFFER gave `.121`, ACK gave `.122` because no pending lease was stored |

---

## The Correct Solution: AF_PACKET Raw Sockets

This is the **industry-standard approach** used by `dnsmasq`, `isc-dhcp-server`, and `udhcpd`.

Instead of using the kernel's IP/UDP stack, we open `AF_PACKET / SOCK_RAW` sockets and **construct the entire Ethernet frame ourselves**:

```
[ Ethernet Header (14 bytes) ]
[ IP Header (20 bytes)       ]
[ UDP Header (8 bytes)       ]
[ DHCP Payload (variable)    ]
```

By doing this, the kernel never touches the frame's IP/UDP headers — it is injected directly onto the wire, bypassing all routing and ARP requirements.

---

## Architecture

### Two Sockets

| Socket | Type | Purpose |
|--------|------|---------|
| `m_rxSocket` | `AF_PACKET, SOCK_RAW, ETH_P_ALL` | Receive all Ethernet frames; filter UDP/67 in software |
| `m_txSocket` | `AF_PACKET, SOCK_RAW, ETH_P_IP` | Send hand-crafted Ethernet/IP/UDP/DHCP frames |

Both are bound to the specific interface index via `sockaddr_ll` so they only see traffic on the serving interface (e.g. `wlp2s0`).

### Class Structure

```
DhcpServer : public QThread
├── run()                  — main poll() loop
├── setupInterface()       — ioctl: get MAC, IP, interface index
├── setupSockets()         — create & bind AF_PACKET sockets
├── processRawFrame()      — parse Ethernet→IP→UDP→DHCP, filter non-DHCP
├── processDhcpPacket()    — dispatch on DHCP message type (1/3/4/7)
├── sendOffer()            — handle DHCPDISCOVER, build OFFER frame
├── sendAck()              — handle DHCPREQUEST, build ACK frame
├── sendNak()              — send NAK (e.g. wrong server selected)
├── sendRawDhcpReply()     — construct & transmit full Ethernet frame
├── allocateIP()           — pool allocator with lease tracking
├── ipChecksum()           — RFC 1071 one's-complement checksum
└── getOption()            — parse TLV DHCP options
```

---

## Key Implementation Details

### 1. Interface Discovery (setupInterface)

Uses three `ioctl()` calls on a temporary UDP socket:

```cpp
ioctl(fd, SIOCGIFINDEX, &ifr);   // → m_ifIndex (needed for sockaddr_ll)
ioctl(fd, SIOCGIFHWADDR, &ifr);  // → m_serverMac[6]
ioctl(fd, SIOCGIFADDR,   &ifr);  // → m_serverIpInt (host byte order)
```

### 2. Receiving Packets

`poll()` with 500ms timeout on `m_rxSocket`. On data:
- Parse Ethernet header → check `ethertype == 0x0800` (IPv4)
- Parse IP header → check `protocol == 17` (UDP)
- Parse UDP header → check `dest == 67` (DHCP server port)
- The **Ethernet source MAC** is used as the authoritative client MAC for replies (more reliable than `chaddr` for some buggy clients)

### 3. Building DHCP Reply Packets

The static `buildDhcpReply()` function fills a `DhcpHeader` struct and appends TLV options:

| Option | Code | Value |
|--------|------|-------|
| Message Type | 53 | 2=OFFER, 5=ACK, 6=NAK |
| Server Identifier | 54 | **Server's own IP** (not the router!) |
| Lease Time | 51 | Configured value (e.g. 86400s) |
| Renewal T1 | 58 | 50% of lease time |
| Rebinding T2 | 59 | 87.5% of lease time |
| Subnet Mask | 1 | e.g. `255.255.255.0` |
| Router | 3 | Gateway IP |
| DNS | 6 | DNS server IP |
| Broadcast | 28 | `routerIp \| ~subnetMask` |
| End | 255 | Always last |

### 4. Sending Raw Frames (sendRawDhcpReply)

```
frame = [EthHeader][IpHeader][UdpHeader][DHCP payload]
```

- **Ethernet dst** = `FF:FF:FF:FF:FF:FF` (broadcast) or client MAC
- **Ethernet src** = server MAC (from `m_serverMac`)
- **IP src** = server IP (`m_serverIpInt`)
- **IP dst** = `255.255.255.255` (broadcast) or client's `ciaddr`
- **IP checksum** = calculated with `ipChecksum()` (RFC 1071)
- **UDP checksum** = `0` (disabled — legal per RFC 768, widely accepted)
- **UDP src port** = 67, **dst port** = 68

Frame is sent via `sendto()` with a `sockaddr_ll` struct specifying the interface index and destination MAC.

### 5. Reply Destination Logic (RFC 2131 §4.1)

```
if (broadcast_bit_set || ciaddr == 0):
    → dst MAC = FF:FF:FF:FF:FF:FF, dst IP = 255.255.255.255
else:
    → dst MAC = client MAC (from Ethernet header), dst IP = ciaddr
```

This covers: initial boot (no IP), renewing (has IP), and broadcast-flag requests.

### 6. IP Pool Allocation

```
m_rangeStartInt .. m_rangeEndInt  (both in host byte order)
```

`allocateIP(mac)`:
1. If MAC has an existing **non-expired** lease → return same IP (sticky allocation)
2. Otherwise walk the pool linearly with wraparound, skipping IPs already active
3. Returns the candidate IP string

### 7. The OFFER/ACK IP Consistency Fix

**Problem:** OFFER called `allocateIP()` but did not store a lease. ACK then called `allocateIP()` again, which advanced the offset and returned the next IP — OFFER said `.121`, ACK said `.122`.

**Fix:** OFFER now writes a **pending lease** (60-second TTL) immediately after allocation:

```cpp
DHCPLease pending;
pending.mac      = clientMac;
pending.ip       = offeredIpStr;
pending.hostname = "(pending)";
pending.expiry   = QDateTime::currentDateTime().addSecs(60);
m_leases[clientMac] = pending;
```

When ACK calls `allocateIP()` milliseconds later, it finds the existing entry and returns the same IP. If the client never sends a REQUEST, the pending lease expires automatically in 60 seconds.

---

## Required Permissions

AF_PACKET raw sockets require **root** or the `CAP_NET_RAW` Linux capability:

```bash
sudo ./LANMonitor
# or grant the capability to the binary:
sudo setcap cap_net_raw+ep ./LANMonitor
```

---

## Build Instructions

```bash
cd "/home/falcon/LAN /lan-monitor/build"
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
sudo ./LANMonitor
```

Dependencies: Qt6 (Widgets, Network, Svg, Sql), libcrafter, libpcap, pthread.

---

## Verification

Verify with tcpdump on the serving interface:

```bash
sudo tcpdump -i wlp2s0 -vnn port 67 or port 68
# or on any interface:
sudo tcpdump -i any -vnn port 67 or port 68
```

A successful DORA handshake looks like:

```
Client → 0.0.0.0:68  > 255.255.255.255:67  DHCP Discover
Server → 192.168.8.120:67 > 255.255.255.255:68  DHCP Offer    Your-IP 192.168.8.121
Client → 0.0.0.0:68  > 255.255.255.255:67  DHCP Request  Requested-IP 192.168.8.121
Server → 192.168.8.120:67 > 255.255.255.255:68  DHCP ACK     Your-IP 192.168.8.121 ✅
```

OFFER and ACK must show the **same** `Your-IP`.

---

## Confirmed Working Test

```
Interface:  wlp2s0  (Wi-Fi, Linux)
Server IP:  192.168.8.120
Pool:       192.168.8.121 – 192.168.8.254
Router:     192.168.8.1
DNS:        8.8.8.8
Client:     Android 12 phone "JYS" (MAC fe:ea:02:26:14:e0)
Result:     Assigned 192.168.8.121, ACK in ~20ms ✅
```

---

## Files

| File | Purpose |
|------|---------|
| `core/DhcpServer.h` | Class declaration, packed structs (DhcpHeader, EthHeader, IpHeader, UdpHeader) |
| `core/DhcpServer.cpp` | Full implementation — raw socket DHCP server |
| `core/DHCPManager.h` | `DHCPServerConfig` and `DHCPLease` structs; `DHCPManager` Qt wrapper |
| `core/DHCPManager.cpp` | Lifecycle management (start/stop/configure) |

---

## RFC References

- **RFC 2131** — Dynamic Host Configuration Protocol (the DHCP spec)
- **RFC 2132** — DHCP Options and BOOTP Vendor Extensions
- **RFC 768** — UDP (UDP checksum = 0 is valid)
- **RFC 1071** — Computing the Internet Checksum
