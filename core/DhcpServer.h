#ifndef DHCPSERVER_H
#define DHCPSERVER_H

#include <QThread>
#include <QObject>
#include <QDateTime>
#include <QMutex>
#include <QMap>
#include <QString>
#include <cstdint>
#include <netinet/in.h>
#include <net/ethernet.h>

#include "DHCPManager.h" // For DHCPServerConfig and DHCPLease

namespace core {

// -------------------------------------------------------------------------
// DHCP wire format structs
// -------------------------------------------------------------------------
#pragma pack(push, 1)

struct DhcpHeader {
    uint8_t  op;          // 1 = BOOTREQUEST, 2 = BOOTREPLY
    uint8_t  htype;       // 1 = Ethernet
    uint8_t  hlen;        // 6 = MAC length
    uint8_t  hops;        // Relay agent hops
    uint32_t xid;         // Transaction ID
    uint16_t secs;        // Seconds elapsed
    uint16_t flags;       // Broadcast flag
    uint32_t ciaddr;      // Client IP (already configured)
    uint32_t yiaddr;      // "Your" IP (offered)
    uint32_t siaddr;      // Next-server IP (0 or server IP)
    uint32_t giaddr;      // Gateway/relay IP
    uint8_t  chaddr[16];  // Client hardware address
    uint8_t  sname[64];   // Server host name
    uint8_t  file[128];   // Boot file name
    uint32_t magic;       // Magic Cookie 0x63825363
    // Options follow...
};

// Raw frame headers for AF_PACKET sending
struct EthHeader {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;   // htons(0x0800) for IPv4
};

struct IpHeader {
    uint8_t  ver_ihl;     // 0x45 (version 4, IHL 5*4=20 bytes)
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;    // 17 = UDP
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
};

struct UdpHeader {
    uint16_t source;      // 67
    uint16_t dest;        // 68
    uint16_t len;
    uint16_t check;       // 0 = disabled for simplicity (valid per RFC 768)
};

#pragma pack(pop)

// -------------------------------------------------------------------------

class DhcpServer : public QThread {
    Q_OBJECT
public:
    explicit DhcpServer(const DHCPServerConfig &config, QObject *parent = nullptr);
    ~DhcpServer();

    void stop();
    QList<DHCPLease> getActiveLeases();
    void addStaticLease(const QString &mac, const QString &ip, const QString &hostname);
    void expireLease(const QString &mac);         // Immediately expire a lease
    void kickClient(const QString &mac,
                    const QString &ip);           // Expire + send unicast DHCP NAK → forces client to release IP now
    void setBlockedMACs(const QSet<QString> &blocked);

signals:
    void leaseUpdated(const core::DHCPLease &lease);
    void leaseExpired(const QString &ip, const QString &mac);

protected:
    void run() override;

private:
    DHCPServerConfig m_config;
    volatile bool    m_running;

    // Two sockets:
    //   m_rxSocket  - AF_PACKET SOCK_RAW to receive all Ethernet frames (port 67)
    //   m_txSocket  - AF_PACKET SOCK_RAW to send full crafted frames
    int m_rxSocket;
    int m_txSocket;

    // Our server's own MAC & IP on the serving interface (populated by setupInterface())
    uint8_t  m_serverMac[6];
    uint32_t m_serverIpInt;  // host byte order
    int      m_ifIndex;

    // Host laptop's own identity — pre-populated at construction from DHCPServerConfig.
    // Used to reject our own DHCP requests before setupInterface() even runs.
    uint8_t  m_hostMac[6];  // parsed from config.hostMac
    uint32_t m_hostIpInt;   // parsed from config.hostIp (host byte order)

    QMutex              m_leaseMutex;
    QMap<QString, DHCPLease> m_leases;  // MAC -> Lease
    QSet<QString>       m_blockedMACs; // MACs denied any lease

    uint32_t m_currentIpOffset;
    uint32_t m_rangeStartInt;  // host byte order
    uint32_t m_rangeEndInt;    // host byte order

    // Socket / interface setup
    bool setupInterface();
    bool setupSockets();

    // Packet processing
    void processRawFrame(uint8_t *buf, ssize_t len);
    void processDhcpPacket(DhcpHeader *req, ssize_t dhcpLen,
                           const uint8_t *clientMacL2);

    // Reply builders
    void sendOffer(DhcpHeader *req, uint8_t *reqOpts, ssize_t optsLen,
                   const uint8_t *clientMacL2);
    void sendAck  (DhcpHeader *req, uint8_t *reqOpts, ssize_t optsLen,
                   const uint8_t *clientMacL2);
    void sendNak  (DhcpHeader *req, const uint8_t *clientMacL2);

    // Raw frame transmission
    void sendRawDhcpReply(const uint8_t *dstMac, uint32_t dstIp,
                          uint8_t *dhcpPayload, size_t dhcpLen);


    // Helpers
    QString   macToString(const uint8_t *mac);
    QString   allocateIP(const QString &mac);
    uint8_t   getOption(uint8_t *options, ssize_t optionsLen, uint8_t code,
                        uint8_t *outVal = nullptr, size_t maxValLen = 0);
    uint16_t  ipChecksum(const void *data, size_t len);
};

} // namespace core

#endif // DHCPSERVER_H
