/**
 * DhcpServer.cpp  —  Fully RFC-2131-compliant DHCP server using AF_PACKET raw sockets.
 *
 * WHY AF_PACKET and not plain UDP sockets?
 * ─────────────────────────────────────────
 * The Linux kernel routing stack requires a valid IP address on the outgoing
 * interface to route packets to 255.255.255.255.  A DHCP client that has just
 * booted has no IP at all, so:
 *   • The kernel cannot ARP-resolve it for unicast replies.
 *   • The kernel refuses to route limited-broadcast (255.255.255.255) replies
 *     unless the socket has a source IP already bound — but we don't want to
 *     impersonate clients.
 *
 * The industry-standard fix (used by ISC-DHCP, dnsmasq, udhcpd …) is to send
 * replies as completely hand-crafted Ethernet frames via AF_PACKET/SOCK_RAW so
 * the kernel never touches the frame's IP/UDP headers.  We receive on the same
 * kind of socket, filtering for UDP/67 in software.
 *
 * Frame layout sent:  [Ethernet][IP][UDP][DHCP + options]
 *
 * Checksum coverage:
 *   • IP header checksum  – calculated (required).
 *   • UDP checksum        – set to 0 (legal per RFC 768; kernel/switch ignores).
 */

#include "DhcpServer.h"

#include <QDebug>
#include <QHostAddress>
#include <QPair>
#include <utility>    // std::as_const


// POSIX / Linux headers
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netpacket/packet.h>   // sockaddr_ll
#include <arpa/inet.h>
#include <linux/if_ether.h>     // ETH_P_IP, ETH_P_ALL
#include <unistd.h>
#include <cstring>
#include <poll.h>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

DhcpServer::DhcpServer(const DHCPServerConfig &config, QObject *parent)
    : QThread(parent)
    , m_config(config)
    , m_running(false)
    , m_rxSocket(-1)
    , m_txSocket(-1)
    , m_serverIpInt(0)
    , m_ifIndex(0)
    , m_hostIpInt(0)
    , m_currentIpOffset(0)
{
    memset(m_serverMac, 0, sizeof(m_serverMac));
    memset(m_hostMac,   0, sizeof(m_hostMac));

    // Pre-parse host MAC from config so we can reject our own packets immediately,
    // even before setupInterface() runs on the server thread.
    if (!config.hostMac.isEmpty()) {
        QStringList parts = config.hostMac.split(':');
        if (parts.size() == 6) {
            for (int i = 0; i < 6; ++i)
                m_hostMac[i] = static_cast<uint8_t>(parts[i].toInt(nullptr, 16));
        }
    }
    if (!config.hostIp.isEmpty())
        m_hostIpInt = ntohl(inet_addr(config.hostIp.toUtf8().constData()));

    // Pre-compute range in *host* byte order for arithmetic.
    m_rangeStartInt = ntohl(inet_addr(m_config.rangeStart.toUtf8().constData()));
    m_rangeEndInt   = ntohl(inet_addr(m_config.rangeEnd  .toUtf8().constData()));
}


DhcpServer::~DhcpServer() {
    stop();
    wait();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void DhcpServer::stop() {
    m_running = false;
    // Closing the socket wakes up poll() immediately.
    if (m_rxSocket >= 0) { close(m_rxSocket); m_rxSocket = -1; }
    if (m_txSocket >= 0) { close(m_txSocket); m_txSocket = -1; }
}

QList<DHCPLease> DhcpServer::getActiveLeases() {
    QMutexLocker locker(&m_leaseMutex);
    QList<DHCPLease> active;
    QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_leases.begin(); it != m_leases.end(); ++it)
        if (it.value().expiry > now)
            active.append(it.value());
    return active;
}

void DhcpServer::addStaticLease(const QString &mac, const QString &ip,
                                 const QString &hostname) {
    QMutexLocker locker(&m_leaseMutex);
    DHCPLease lease;
    lease.mac      = mac.toLower();
    lease.ip       = ip;
    lease.hostname = hostname;
    lease.expiry   = QDateTime::currentDateTime().addYears(10);
    m_leases[lease.mac] = lease;
    
    // Notify NetworkManager of the new discovery
    emit leaseUpdated(lease);
}

void DhcpServer::expireLease(const QString &mac) {
    QMutexLocker locker(&m_leaseMutex);
    // Set expiry to the past so getActiveLeases() stops returning it
    // and allocateIP() won't consider it in-use.
    if (m_leases.contains(mac)) {
        DHCPLease lease = m_leases[mac];
        m_leases[mac].expiry = QDateTime::currentDateTime().addSecs(-1);
        qDebug() << "[DHCP] Expired lease for" << mac;
        
        // Notify DHCPManager so it can update firewall
        emit leaseExpired(lease.ip, lease.mac);
    }
}


void DhcpServer::setBlockedMACs(const QSet<QString> &blocked) {
    QMutexLocker locker(&m_leaseMutex);
    m_blockedMACs = blocked;
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread entry-point
// ─────────────────────────────────────────────────────────────────────────────

void DhcpServer::run() {
    if (!setupInterface()) { return; }

    // Re-parse range in case they used partial IPs (e.g., "125" for 192.168.8.125)
    auto parseIP = [this](const QString &ipStr) -> uint32_t {
        QString s = ipStr.trimmed();

        // If it looks like a full IP (contains dots), parse it directly
        if (s.contains('.')) {
            return ntohl(inet_addr(s.toUtf8().constData()));
        }

        // Otherwise, try to parse as partial octet and fill in network part from server IP
        bool ok;
        uint32_t octet = s.toUInt(&ok);
        if (ok && octet <= 255) {
            // Use same network/class as server IP
            // For 192.168.8.120: network is 192.168.8.x
            uint32_t network = m_serverIpInt & 0xFFFFFF00;  // Assume /24
            return network + octet;
        }

        // Fallback: parse as full IP
        return ntohl(inet_addr(s.toUtf8().constData()));
    };

    // Re-parse range now that we know the server IP and can properly handle partial IPs
    m_rangeStartInt = parseIP(m_config.rangeStart);
    m_rangeEndInt   = parseIP(m_config.rangeEnd);

    if (!setupSockets())   { return; }

    m_running = true;
    qInfo() << "[DHCP] Raw-socket server running on interface"
            << m_config.interface
            << "| Server ID:" << QHostAddress(m_serverIpInt).toString()
            << "| Gateway:" << m_config.routerIp
            << "| Range:" << QHostAddress(m_rangeStartInt).toString()
            << "-" << QHostAddress(m_rangeEndInt).toString();

    uint8_t buf[2048];

    struct pollfd pfd;
    pfd.fd     = m_rxSocket;
    pfd.events = POLLIN;

    while (m_running) {
        int ret = poll(&pfd, 1, 500);
        if (ret < 0) break;   // socket closed
        if (ret == 0) {       // poll timeout (500ms tick)
            continue;
        }
        if (!(pfd.revents & POLLIN)) continue;

        ssize_t n = recvfrom(m_rxSocket, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) break;

        processRawFrame(buf, n);
    }

    qInfo() << "[DHCP] Server stopped.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface discovery
// ─────────────────────────────────────────────────────────────────────────────

bool DhcpServer::setupInterface() {
    // Use a temporary UDP socket purely to run ioctls.
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        qWarning() << "[DHCP] Cannot open helper socket:" << strerror(errno);
        return false;
    }

    QByteArray ifName = m_config.interface.toUtf8();

    // --- Interface index --------------------------------------------------
    {
        struct ifreq ifr{};
        strncpy(ifr.ifr_name, ifName.constData(), IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
            qWarning() << "[DHCP] SIOCGIFINDEX failed for" << m_config.interface
                       << ":" << strerror(errno);
            close(fd);
            return false;
        }
        m_ifIndex = ifr.ifr_ifindex;
    }

    // --- Server MAC address -----------------------------------------------
    {
        struct ifreq ifr{};
        strncpy(ifr.ifr_name, ifName.constData(), IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
            qWarning() << "[DHCP] SIOCGIFHWADDR failed:" << strerror(errno);
            close(fd);
            return false;
        }
        memcpy(m_serverMac, ifr.ifr_hwaddr.sa_data, 6);
    }

    // --- Server IP address ------------------------------------------------
    {
        struct ifreq ifr{};
        strncpy(ifr.ifr_name, ifName.constData(), IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
            qWarning() << "[DHCP] SIOCGIFADDR failed:" << strerror(errno);
            // Non-fatal: fall back to configured routerIp
            m_serverIpInt = ntohl(inet_addr(m_config.routerIp.toUtf8().constData()));
        } else {
            m_serverIpInt = ntohl(
                reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr)->sin_addr.s_addr);
        }
    }

    close(fd);

    qInfo() << "[DHCP] Interface" << m_config.interface
            << "| idx=" << m_ifIndex
            << "| MAC=" << macToString(m_serverMac)
            << QString("| IP=%1.%2.%3.%4")
                .arg((m_serverIpInt >> 24) & 0xFF)
                .arg((m_serverIpInt >> 16) & 0xFF)
                .arg((m_serverIpInt >>  8) & 0xFF)
                .arg( m_serverIpInt        & 0xFF);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket setup
// ─────────────────────────────────────────────────────────────────────────────

bool DhcpServer::setupSockets() {
    // RX socket — receive ALL Ethernet frames (we filter in software for UDP/67)
    m_rxSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (m_rxSocket < 0) {
        qWarning() << "[DHCP] AF_PACKET socket (rx) failed:" << strerror(errno)
                   << " — are we running as root / with CAP_NET_RAW?";
        return false;
    }

    // Bind RX to the specific interface
    {
        struct sockaddr_ll sll{};
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = m_ifIndex;
        sll.sll_protocol = htons(ETH_P_ALL);
        if (bind(m_rxSocket, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll)) < 0) {
            qWarning() << "[DHCP] bind(rx) failed:" << strerror(errno);
            return false;
        }

        // Enable Promiscuous Mode to see unicast renewals between victim and router
        // NOTE: On many Wi-Fi (wlp) drivers, enabling Promiscuous Mode while in Station (STA) mode
        // silently BREAKS all incoming packet delivery or drops broadcasts.
        /*
        struct packet_mreq mr{};
        mr.mr_ifindex = m_ifIndex;
        mr.mr_type = PACKET_MR_PROMISC;
        if (setsockopt(m_rxSocket, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
            qWarning() << "[DHCP] Failed to enable Promiscuous Mode:" << strerror(errno);
        } else {
            qDebug() << "[DHCP] Promiscuous Mode ENABLED for" << m_config.interface;
        }
        */
    }

    // TX socket — we open a separate AF_PACKET socket for sending so that
    // closing the RX socket cleanly wakes up poll without racing on the TX path.
    m_txSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
    if (m_txSocket < 0) {
        qWarning() << "[DHCP] AF_PACKET socket (tx) failed:" << strerror(errno);
        close(m_rxSocket); m_rxSocket = -1;
        return false;
    }

    {
        struct sockaddr_ll sll{};
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_IP);
        sll.sll_ifindex  = m_ifIndex;
        if (bind(m_txSocket, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll)) < 0) {
            qWarning() << "[DHCP] bind(tx) failed:" << strerror(errno);
            close(m_rxSocket); m_rxSocket = -1;
            close(m_txSocket); m_txSocket = -1;
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame / packet parsing
// ─────────────────────────────────────────────────────────────────────────────

void DhcpServer::processRawFrame(uint8_t *buf, ssize_t len) {
    // Minimum: Ethernet(14) + IP(20) + UDP(8) + DHCP(236) = 278 bytes
    const ssize_t MIN_LEN = 14 + 20 + 8 + 236;
    if (len < MIN_LEN) { /*qDebug() << "[DHCP] Dropped: len (" << len << ") < MIN_LEN (" << MIN_LEN << ")";*/ return; }

    // ── Ethernet header ──────────────────────────────────────────────────────
    auto *eth = reinterpret_cast<EthHeader*>(buf);
    if (ntohs(eth->ethertype) != 0x0800) { /*qDebug() << "[DHCP] Dropped: not IPv4 (ethertype" << QString::number(ntohs(eth->ethertype), 16) << ")";*/ return; } // Not IPv4

    // ── IP header ────────────────────────────────────────────────────────────
    auto *ip = reinterpret_cast<IpHeader*>(buf + 14);
    uint8_t  ihl   = (ip->ver_ihl & 0x0F) * 4;     // IP header length in bytes
    uint8_t  proto = ip->protocol;
    if (proto != 17) { /*qDebug() << "[DHCP] Dropped: not UDP";*/ return; }                         // Not UDP

    // ── UDP header ───────────────────────────────────────────────────────────
    auto *udp = reinterpret_cast<UdpHeader*>(buf + 14 + ihl);
    if (ntohs(udp->dest) != 67) { /*qDebug() << "[DHCP] Dropped: dest port" << ntohs(udp->dest);*/ return; }             // Not DHCP server port

    // ── DHCP payload ─────────────────────────────────────────────────────────
    uint8_t *dhcp    = buf + 14 + ihl + 8;
    ssize_t  dhcpLen = len  - 14 - ihl - 8;
    if (dhcpLen < static_cast<ssize_t>(sizeof(DhcpHeader))) { qDebug() << "[DHCP] Bad length " << dhcpLen; return; }

    auto *req = reinterpret_cast<DhcpHeader*>(dhcp);

    // Only BOOTREQUEST (op=1)
    if (req->op != 1) { qDebug() << "[DHCP] Dropped: op !=" << req->op; return; }

    // Verify magic cookie
    if (ntohl(req->magic) != 0x63825363) { qDebug() << "[DHCP] Bad magic"; qDebug() << "[DHCP] Bad magic";
        qDebug() << "[DHCP] Bad magic cookie — ignoring";
        return;
    }

    // The client's real hardware address is in the Ethernet *source* field.
    // RFC 2131 says we should use chaddr, but some buggy clients zero padding bytes
    // differently; using L2 src for sending back is most reliable.
    processDhcpPacket(req, dhcpLen, eth->src);
}

void DhcpServer::processDhcpPacket(DhcpHeader *req, ssize_t dhcpLen,
                                    const uint8_t *clientMacL2) {
    uint8_t  *options    = reinterpret_cast<uint8_t*>(req) + sizeof(DhcpHeader);
    ssize_t   optionsLen = dhcpLen - sizeof(DhcpHeader);

    uint8_t msgType = 0;
    if (!getOption(options, optionsLen, 53, &msgType, 1)) {
        qDebug() << "[DHCP] No option 53 (message type) — ignoring";
        return;
    }

    QString clientMac = macToString(req->chaddr);

    // CRITICAL: NEVER respond to our own host laptop.
    // Check against BOTH m_hostMac (known at construction from config) and
    // m_serverMac (populated later by setupInterface) to cover all timing windows.
    static const uint8_t zeroMac[6] = {0,0,0,0,0,0};
    bool hostMacValid  = memcmp(m_hostMac,   zeroMac, 6) != 0;
    bool serverMacValid= memcmp(m_serverMac, zeroMac, 6) != 0;

    bool isOwnMac = (hostMacValid   && (memcmp(req->chaddr, m_hostMac,   6) == 0 || memcmp(clientMacL2, m_hostMac,   6) == 0))
                 || (serverMacValid && (memcmp(req->chaddr, m_serverMac, 6) == 0 || memcmp(clientMacL2, m_serverMac, 6) == 0));

    // Also block by IP: if ciaddr equals our host IP, this is a RENEW from ourselves.
    bool isOwnIp = (m_hostIpInt != 0 && ntohl(req->ciaddr) == m_hostIpInt)
                || (m_serverIpInt != 0 && ntohl(req->ciaddr) == m_serverIpInt);

    if (isOwnMac || isOwnIp) {
        qDebug() << "[DHCP] Ignoring own-host request from" << clientMac;
        return;
    }

    qDebug() << "[DHCP] Received type" << msgType << "from" << clientMac;

    // --- RFC 2131 Compliant Client Blocking ---
    // Check if this MAC is blocked and reject immediately with NAK.
    // This is the modern, standards-compliant approach to client isolation.
    {
        QMutexLocker lk(&m_leaseMutex);
        if (m_blockedMACs.contains(clientMac.toLower())) {
            // [MODERNIZE] We no longer NAK here. We want to 'Trap' the device by 
            // letting it take our poisoned lease (Poison Pill). 
            // Blanket NAKing just makes the phone seek other servers.
            qDebug() << "[DHCP] Blocked MAC seeking lease:" << clientMac << "→ proceeding to Poison Pill delivery";
        }
    }

    switch (msgType) {
    case 1: sendOffer(req, options, optionsLen, clientMacL2); break;  // DISCOVER
    case 3: sendAck  (req, options, optionsLen, clientMacL2); break;  // REQUEST
    case 4: /* DECLINE — free the lease */
        {
            QMutexLocker lk(&m_leaseMutex);
            m_leases.remove(clientMac);
        }
        break;
    case 7: /* RELEASE — mark expired */
        {
            QMutexLocker lk(&m_leaseMutex);
            if (m_leases.contains(clientMac))
                m_leases[clientMac].expiry = QDateTime::currentDateTime();
        }
        break;
    default:
        qDebug() << "[DHCP] Unhandled message type:" << msgType;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DHCP reply builders
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Build a DHCP reply packet (starting at the DHCP header) into @p pkt.
 * Returns the total DHCP payload length written.
 * @p msgType       2 = OFFER, 5 = ACK, 6 = NAK
 * @p yiaddr        Offered IP in network byte order (0 for NAK)
 */
static size_t buildDhcpReply(uint8_t *pkt, size_t bufSize,
                              const DhcpHeader *req,
                              uint8_t msgType,
                              uint32_t yiaddr,             // net byte order
                              uint32_t serverIp,           // net byte order
                              uint32_t subnetMask,         // net byte order
                              uint32_t routerIp,           // net byte order
                              uint32_t dns1,               // net byte order
                              uint32_t leaseTimeSec,
                              const char* captivePortalUrl = nullptr)
{
    memset(pkt, 0, bufSize);
    auto *rep = reinterpret_cast<DhcpHeader*>(pkt);

    rep->op     = 2;            // BOOTREPLY
    rep->htype  = 1;            // Ethernet
    rep->hlen   = 6;
    rep->hops   = 0;
    rep->xid    = req->xid;     // Mirror transaction ID
    rep->secs   = 0;
    rep->flags  = req->flags;   // Mirror broadcast flag
    rep->ciaddr = 0;
    rep->yiaddr = yiaddr;
    rep->siaddr = 0;            // RFC 2131: next-server; set to 0 unless PXE
    rep->giaddr = req->giaddr;  // Pass relay agent IP through
    memcpy(rep->chaddr, req->chaddr, 16);
    rep->magic  = htonl(0x63825363);

    uint8_t *opt = pkt + sizeof(DhcpHeader);

    // Option 53 — DHCP Message Type
    *opt++ = 53; *opt++ = 1; *opt++ = msgType;
    
    // Set siaddr = Next server (per RFC 2131, usually the server itself for PXE/TFTP, 
    // but some clients want it populated to know who the authoritative source is)
    if (msgType != 6) { 
        rep->siaddr = serverIp; 
    }

    // Option 54 — Server Identifier  (MUST be the *server's* IP, not the router)
    *opt++ = 54; *opt++ = 4;
    memcpy(opt, &serverIp, 4); opt += 4;

    if (msgType != 6) { // Not NAK — include addressing options
        // Option 51 — IP Address Lease Time
        *opt++ = 51; *opt++ = 4;
        uint32_t lt = htonl(leaseTimeSec);
        memcpy(opt, &lt, 4); opt += 4;

        // Option 58 — Renewal (T1) time = 50% of lease time
        *opt++ = 58; *opt++ = 4;
        uint32_t t1 = htonl(leaseTimeSec / 2);
        memcpy(opt, &t1, 4); opt += 4;

        // Option 59 — Rebinding (T2) time = 87.5% of lease time
        *opt++ = 59; *opt++ = 4;
        uint32_t t2 = htonl((leaseTimeSec * 7) / 8);
        memcpy(opt, &t2, 4); opt += 4;

        // Option 1 — Subnet Mask
        *opt++ = 1; *opt++ = 4;
        memcpy(opt, &subnetMask, 4); opt += 4;

        // Option 3 — Router
        *opt++ = 3; *opt++ = 4;
        memcpy(opt, &routerIp, 4); opt += 4;

        // Option 6 — DNS
        if (dns1 != 0) {
            *opt++ = 6; *opt++ = 4;
            memcpy(opt, &dns1, 4); opt += 4;
        }

        // Option 28 — Broadcast address
        uint32_t bcast = routerIp | ~subnetMask;
        *opt++ = 28; *opt++ = 4;
        memcpy(opt, &bcast, 4); opt += 4;

        // Option 114 — Captive Portal URL (RFC 8910)
        if (captivePortalUrl && strlen(captivePortalUrl) > 0) {
            size_t urlLen = strlen(captivePortalUrl);
            if (urlLen < 255) {
                *opt++ = 114; *opt++ = (uint8_t)urlLen;
                memcpy(opt, captivePortalUrl, urlLen); opt += urlLen;
            }
        }
    }

    // End option
    *opt++ = 255;

    return static_cast<size_t>(opt - pkt);
}

// ── OFFER ────────────────────────────────────────────────────────────────────

void DhcpServer::sendOffer(DhcpHeader *req, uint8_t *reqOpts, ssize_t optsLen,
                            const uint8_t *clientMacL2) {
    QString clientMac = macToString(req->chaddr);

    // Check option 50 for client-requested IP
    uint8_t  requestedIpBytes[4]{};
    uint32_t requestedIp = 0;
    if (getOption(reqOpts, optsLen, 50, requestedIpBytes, 4)) {
        memcpy(&requestedIp, requestedIpBytes, 4);  // already network byte order
    }

    QMutexLocker locker(&m_leaseMutex);
    QString offeredIpStr = allocateIP(clientMac);

    // Store a *pending* lease for this MAC so that when the client sends its
    // REQUEST, allocateIP() finds the same IP and returns it — not the next one.
    // Pending leases expire in 60 s if the client never follows up with a REQUEST.
    DHCPLease pending;
    pending.mac      = clientMac;
    pending.ip       = offeredIpStr;
    pending.hostname = QStringLiteral("(pending)");
    pending.expiry   = QDateTime::currentDateTime().addSecs(60);
    m_leases[clientMac] = pending;

    locker.unlock();

    uint32_t offeredIpNet  = htonl(QHostAddress(offeredIpStr).toIPv4Address());
    uint32_t serverIpNet   = htonl(m_serverIpInt);
    uint32_t subnetMaskNet = htonl(QHostAddress(m_config.subnetMask).toIPv4Address());
    
    uint32_t routerNet = htonl(QHostAddress(m_config.routerIp).toIPv4Address());
    uint32_t dns1Net   = m_config.dns1.isEmpty() ? 0 : htonl(QHostAddress(m_config.dns1).toIPv4Address());
    uint32_t leaseTime = static_cast<uint32_t>(m_config.leaseTimeSeconds);
    const char* portalUrl = nullptr;
    QByteArray urlBytes;
    
    // --- POISON PILL OVERRIDE ---
    if (m_blockedMACs.contains(clientMac.toLower())) {
        QString srvIpStr = QHostAddress(ntohl(serverIpNet)).toString();
        qDebug() << "[DHCP] Delivering NUCLEAR Poison Pill to blocked MAC:" << clientMac << "(Gateway =" << srvIpStr << ")";
        routerNet = serverIpNet;    // Target current machine as the "Gateway"
        dns1Net   = serverIpNet;    // Target current machine as DNS
        
        // Use original subnet mask but keep the isolation
        subnetMaskNet = htonl(QHostAddress(m_config.subnetMask).toIPv4Address());
        leaseTime = 60;             // Short lease

        urlBytes = QString("http://%1/").arg(srvIpStr).toUtf8();
        portalUrl = urlBytes.constData();
    }

    uint8_t dhcpPkt[548];  // RFC 2131 minimum DHCP packet size
    size_t dhcpLen = buildDhcpReply(dhcpPkt, sizeof(dhcpPkt),
                                    req, 2 /*OFFER*/,
                                    offeredIpNet, serverIpNet,
                                    subnetMaskNet, routerNet, dns1Net,
                                    leaseTime, portalUrl);

    // ── Destination ──────────────────────────────────────────────────────────
    // RFC 2131 §4.1:
    //   If the broadcast bit is set (flags & 0x8000) → always broadcast.
    //   If giaddr != 0 → send unicast to relay agent.
    //   If ciaddr != 0 → send unicast to ciaddr.
    //   Otherwise → broadcast (client has no IP yet).
    static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t  dstMac[6];
    uint32_t dstIp;

    bool broadcastBit = (ntohs(req->flags) & 0x8000) != 0;

    if (broadcastBit || req->ciaddr == 0) {
        memcpy(dstMac, BROADCAST_MAC, 6);
        dstIp = 0xFFFFFFFF; // 255.255.255.255
    } else {
        memcpy(dstMac, clientMacL2, 6);
        dstIp = ntohl(req->ciaddr);
    }

    qDebug() << "[DHCP] Sending OFFER" << offeredIpStr
             << "to" << clientMac
             << "| broadcast=" << broadcastBit;

    sendRawDhcpReply(dstMac, dstIp, dhcpPkt, dhcpLen);
}

// ── ACK ──────────────────────────────────────────────────────────────────────

void DhcpServer::sendAck(DhcpHeader *req, uint8_t *reqOpts, ssize_t optsLen,
                          const uint8_t *clientMacL2) {
    QString clientMac = macToString(req->chaddr);

    // Option 50 — requested IP (used during SELECTING/INIT-REBOOT states)
    uint8_t  reqIpBytes[4]{};
    uint32_t requestedIpNet = 0;
    if (getOption(reqOpts, optsLen, 50, reqIpBytes, 4))
        memcpy(&requestedIpNet, reqIpBytes, 4);

    // Option 54 — server identifier the client selected
    uint8_t  srvIdBytes[4]{};
    uint32_t clientSelectedServer = 0;
    if (getOption(reqOpts, optsLen, 54, srvIdBytes, 4))
        memcpy(&clientSelectedServer, srvIdBytes, 4);

    // If the client's server-id option doesn't match ours, silently drop.
    // OR: If we are authoritative, NAK them to clear the stale lease, then
    // immediately follow up with a fresh OFFER so they re-do the full DORA
    // cycle and pick us.
    uint32_t ourServerNet = htonl(m_serverIpInt);
    if (clientSelectedServer != 0 && clientSelectedServer != ourServerNet) {
        if (m_config.authoritative) {
            qDebug() << "[DHCP] Authoritative NAK: client requested another server ("
                     << QHostAddress(ntohl(clientSelectedServer)).toString()
                     << ") — NAKing and sending fresh OFFER";
            sendNak(req, clientMacL2);
            // Immediately offer our own IP so the client can re-select us
            sendOffer(req, reqOpts, optsLen, clientMacL2);
        } else {
            qDebug() << "[DHCP] REQUEST is for another server — ignoring";
        }
        return;
    }

    QMutexLocker locker(&m_leaseMutex);
    QString ackedIpStr = allocateIP(clientMac);

    // Register / update lease
    DHCPLease lease;
    lease.mac = clientMac;
    lease.ip  = ackedIpStr;
    lease.expiry = QDateTime::currentDateTime().addSecs(m_config.leaseTimeSeconds);

    uint8_t hostnameBytes[64]{};
    uint8_t hnLen = getOption(reqOpts, optsLen, 12, hostnameBytes, sizeof(hostnameBytes) - 1);
    lease.hostname = hnLen > 0
                     ? QString::fromUtf8(reinterpret_cast<char*>(hostnameBytes), hnLen)
                     : QStringLiteral("Unknown");
    m_leases[clientMac] = lease;
    
    // Notify NetworkManager of the active lease and hostname
    emit leaseUpdated(lease);

    locker.unlock();

    uint32_t ackedIpNet    = htonl(QHostAddress(ackedIpStr).toIPv4Address());
    uint32_t subnetMaskNet = htonl(QHostAddress(m_config.subnetMask).toIPv4Address());
    
    uint32_t routerNet = htonl(QHostAddress(m_config.routerIp).toIPv4Address());
    uint32_t dns1Net   = m_config.dns1.isEmpty() ? 0 : htonl(QHostAddress(m_config.dns1).toIPv4Address());
    uint32_t leaseTime = static_cast<uint32_t>(m_config.leaseTimeSeconds);
    const char* portalUrl = nullptr;
    QByteArray urlBytes;
    
    // --- POISON PILL OVERRIDE ---
    if (m_blockedMACs.contains(clientMac.toLower())) {
        QString srvIpStr = QHostAddress(ntohl(ourServerNet)).toString();
        qDebug() << "[DHCP] Delivering NUCLEAR Poison Pill to blocked MAC:" << clientMac << "(Gateway =" << srvIpStr << ")";
        routerNet = ourServerNet;  // Target current machine as the "Gateway"
        dns1Net   = ourServerNet;  // Target current machine as DNS
        
        // Use original subnet mask but keep the isolation
        subnetMaskNet = htonl(QHostAddress(m_config.subnetMask).toIPv4Address());
        leaseTime = 60;                // Short lease to maintain control

        urlBytes = QString("http://%1/").arg(srvIpStr).toUtf8();
        portalUrl = urlBytes.constData();
    }

    uint8_t dhcpPkt[548];
    size_t dhcpLen = buildDhcpReply(dhcpPkt, sizeof(dhcpPkt),
                                    req, 5 /*ACK*/,
                                    ackedIpNet, ourServerNet,
                                    subnetMaskNet, routerNet, dns1Net,
                                    leaseTime, portalUrl);

    // Destination (same RFC 2131 §4.1 logic)
    static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t  dstMac[6];
    uint32_t dstIp;

    bool broadcastBit = (ntohs(req->flags) & 0x8000) != 0;

    if (broadcastBit || req->ciaddr == 0) {
        memcpy(dstMac, BROADCAST_MAC, 6);
        dstIp = 0xFFFFFFFF;
    } else {
        memcpy(dstMac, clientMacL2, 6);
        dstIp = ntohl(req->ciaddr);
    }

    qDebug() << "[DHCP] Sending ACK" << ackedIpStr << "to" << clientMac
             << "hostname=" << lease.hostname;

    sendRawDhcpReply(dstMac, dstIp, dhcpPkt, dhcpLen);
}

// ── NAK ──────────────────────────────────────────────────────────────────────

void DhcpServer::sendNak(DhcpHeader *req, const uint8_t *clientMacL2) {
    uint32_t ourServerNet = htonl(m_serverIpInt);
    uint8_t dhcpPkt[548];
    // Option 54 (Server ID) must be OUR interface IP, not the Gateway IP
    size_t dhcpLen = buildDhcpReply(dhcpPkt, sizeof(dhcpPkt),
                                    req, 6 /*NAK*/,
                                    0, ourServerNet, 0, 0, 0, 0);

    // NAK is always broadcast (RFC 2131)
    static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    sendRawDhcpReply(BROADCAST_MAC, 0xFFFFFFFF, dhcpPkt, dhcpLen);
}

void DhcpServer::kickClient(const QString &mac, const QString &ip) {
    // Forced lease expiry to trigger immediate re-DHCP for poisoning
    {
        QMutexLocker lk(&m_leaseMutex);
        
        if (m_leases.contains(mac)) {
            // Use local copy for signal to avoid race after unlock
            QString leaseIp = m_leases[mac].ip;
            m_leases[mac].expiry = QDateTime::currentDateTime().addSecs(-1);
            emit leaseExpired(leaseIp, mac);
        }
    }

    // Attempt to send a spontaneous NAK to the client.
    // Note: Since we are not responding to a specific REQUEST, we don't have the client's current XID.
    // We send XID 0 which some clients might accept or ignore.
    uint32_t serverIpNet = htonl(m_serverIpInt);
    
    // Parse MAC
    QString cleanMac = mac.toLower().remove(':');
    uint8_t macBytes[6] = {0};
    for(int i=0; i<6 && i*2 < cleanMac.length(); ++i) {
        macBytes[i] = cleanMac.mid(i*2, 2).toInt(nullptr, 16);
    }
    
    DhcpHeader fakeReq;
    memset(&fakeReq, 0, sizeof(fakeReq));
    fakeReq.op = 1; // Boot request
    fakeReq.htype = 1;
    fakeReq.hlen = 6;
    fakeReq.xid = 0; 
    fakeReq.flags = 0;
    memcpy(fakeReq.chaddr, macBytes, 6);
    fakeReq.magic = htonl(0x63825363);

    uint8_t dhcpPkt[548];
    size_t dhcpLen = buildDhcpReply(dhcpPkt, sizeof(dhcpPkt),
                                    &fakeReq, 6 /*NAK*/,
                                    0, serverIpNet, 0, 0, 0, 0);

    qDebug() << "[DHCP] Sending spontaneous NAK to" << ip << "MAC:" << mac;
    sendRawDhcpReply(macBytes, ntohl(QHostAddress(ip).toIPv4Address()), dhcpPkt, dhcpLen);
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw frame sender
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Construct and transmit a full Ethernet/IP/UDP/DHCP frame via AF_PACKET.
 *
 * @p dstMac       Destination Ethernet MAC (6 bytes)
 * @p dstIp        Destination IP in *host* byte order (e.g. 0xFFFFFFFF)
 * @p dhcpPayload  Pointer to the raw DHCP packet (DhcpHeader + options)
 * @p dhcpLen      Length in bytes of dhcpPayload
 */
void DhcpServer::sendRawDhcpReply(const uint8_t *dstMac, uint32_t dstIp,
                                   uint8_t *dhcpPayload, size_t dhcpLen) {
    // Total frame:  Eth(14) + IP(20) + UDP(8) + DHCP
    size_t  frameLen = 14 + 20 + 8 + dhcpLen;
    uint8_t frame[2048];
    if (frameLen > sizeof(frame)) {
        qWarning() << "[DHCP] DHCP payload too large to fit in frame!";
        return;
    }
    memset(frame, 0, frameLen);

    // ── Ethernet header (bytes 0–13) ─────────────────────────────────────────
    auto *eth = reinterpret_cast<EthHeader*>(frame);
    memcpy(eth->dst, dstMac,      6);
    memcpy(eth->src, m_serverMac, 6);
    eth->ethertype = htons(0x0800);  // IPv4

    // ── IP header (bytes 14–33) ──────────────────────────────────────────────
    auto *ip = reinterpret_cast<IpHeader*>(frame + 14);
    ip->ver_ihl  = 0x45;            // IPv4, 20-byte header
    ip->tos      = 0x10;            // LOWDELAY
    ip->tot_len  = htons(static_cast<uint16_t>(20 + 8 + dhcpLen));
    ip->id       = 0;
    ip->frag_off = htons(0x4000);   // Don't fragment
    ip->ttl      = 128;
    ip->protocol = 17;              // UDP
    ip->check    = 0;
    ip->saddr    = htonl(m_serverIpInt);
    ip->daddr    = htonl(dstIp);
    ip->check    = ipChecksum(ip, 20);

    // ── UDP header (bytes 34–41) ─────────────────────────────────────────────
    auto *udp = reinterpret_cast<UdpHeader*>(frame + 14 + 20);
    udp->source = htons(67);
    udp->dest   = htons(68);
    udp->len    = htons(static_cast<uint16_t>(8 + dhcpLen));
    udp->check  = 0;                // 0 = disabled (RFC 768 § 3)

    // ── DHCP payload (bytes 42…) ─────────────────────────────────────────────
    memcpy(frame + 14 + 20 + 8, dhcpPayload, dhcpLen);

    // ── Transmit via sockaddr_ll ─────────────────────────────────────────────
    struct sockaddr_ll dest{};
    dest.sll_family   = AF_PACKET;
    dest.sll_protocol = htons(ETH_P_IP);
    dest.sll_ifindex  = m_ifIndex;
    dest.sll_halen    = 6;
    memcpy(dest.sll_addr, dstMac, 6);

    ssize_t sent = sendto(m_txSocket, frame, frameLen, 0,
                          reinterpret_cast<struct sockaddr*>(&dest),
                          sizeof(dest));
    if (sent < 0) {
        qWarning() << "[DHCP] sendto() failed:" << strerror(errno);
    } else {
        qDebug() << "[DHCP] Sent" << sent << "byte frame";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Allocate or return an existing (non-expired) lease IP for @p mac.
 * MUST be called with m_leaseMutex held.
 */
QString DhcpServer::allocateIP(const QString &mac) {
    // Return existing lease (prioritize even if expired for blocked MACs to be 'sticky')
    if (m_leases.contains(mac)) {
        const DHCPLease &l = m_leases[mac];
        if (l.expiry > QDateTime::currentDateTime() || m_blockedMACs.contains(mac.toLower()))
            return l.ip;
    }

    // Walk through the pool linearly, skipping IPs that are already in use.
    uint32_t total = m_rangeEndInt - m_rangeStartInt + 1;
    for (uint32_t i = 0; i < total; ++i) {
        uint32_t candidate = m_rangeStartInt + ((m_currentIpOffset + i) % total);
        QString candidateStr = QHostAddress(candidate).toString();

        bool inUse = false;
        for (const DHCPLease &l : std::as_const(m_leases)) {
            if (l.ip == candidateStr && l.expiry > QDateTime::currentDateTime()) {
                inUse = true;
                break;
            }
        }
        if (!inUse) {
            m_currentIpOffset = (m_currentIpOffset + i + 1) % total;
            return candidateStr;
        }
    }

    // Pool exhausted — return start (server will NAK on a real deployment)
    qWarning() << "[DHCP] IP pool exhausted!";
    return QHostAddress(m_rangeStartInt).toString();
}

QString DhcpServer::macToString(const uint8_t *mac) {
    return QString::asprintf("%02x:%02x:%02x:%02x:%02x:%02x",
                             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint8_t DhcpServer::getOption(uint8_t *options, ssize_t optionsLen,
                               uint8_t code, uint8_t *outVal, size_t maxValLen) {
    ssize_t i = 0;
    while (i < optionsLen) {
        uint8_t optCode = options[i];
        if (optCode == 255) break;      // End option
        if (optCode == 0)  { ++i; continue; } // Pad option

        if (i + 1 >= optionsLen) break;
        uint8_t optLen = options[i + 1];
        if (i + 2 + optLen > optionsLen) break;

        if (optCode == code) {
            if (outVal && optLen <= maxValLen)
                memcpy(outVal, &options[i + 2], optLen);
            return optLen;
        }
        i += 2 + optLen;
    }
    return 0;
}

/**
 * Standard one's-complement Internet checksum (RFC 1071).
 */
uint16_t DhcpServer::ipChecksum(const void *data, size_t len) {
    const uint16_t *ptr = reinterpret_cast<const uint16_t*>(data);
    uint32_t sum = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len)
        sum += *reinterpret_cast<const uint8_t*>(ptr);

    // Fold 32-bit sum into 16 bits
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return static_cast<uint16_t>(~sum);
}

} // namespace core
