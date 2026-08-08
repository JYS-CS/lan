#include "FirewallManager.h"
#include <QProcess>
#include <QDebug>

namespace core {

FirewallManager::FirewallManager(const QString &iface, QObject *parent) 
    : QObject(parent), m_interface(iface) {
    initFirewall();
}

FirewallManager::~FirewallManager() {
    unblockAll();
}
void FirewallManager::initFirewall() {
    // Test if we have nft permissions
    int test = QProcess::execute("sh", QStringList() << "-c" << "nft list tables 2>/dev/null");
    if (test != 0) {
        qDebug() << "FirewallManager: nftables not available — firewall features disabled.";
        m_available = false;
        return;
    }
    m_available = true;

    // Clean up stale tables from previous runs — use QProcess::execute so failures are truly silent
    QProcess::execute("sh", {"-c", QString("nft delete table inet %1 2>/dev/null").arg(m_tableName)});
    QProcess::execute("sh", {"-c", QString("nft delete table netdev %1_layer2 2>/dev/null").arg(m_tableName)});
    QProcess::execute("sh", {"-c", QString("nft delete table ip %1_nat 2>/dev/null").arg(m_tableName)});

    // 1. Main inet table for filtering
    runNft({"add", "table", "inet", m_tableName});
    runNft({"add", "chain", "inet", m_tableName, "filter_input",
            "{ type filter hook input priority 0; }"});
    runNft({"add", "chain", "inet", m_tableName, "filter_forward",
            "{ type filter hook forward priority 0; }"});
    runNft({"add", "chain", "inet", m_tableName, "filter_output",
            "{ type filter hook output priority 0; }"});

    // 2. Netdev table for low-level interface monitoring (ingress only, no drops)
    if (!m_interface.isEmpty()) {
        runNft({"add", "table", "netdev", m_tableName + "_layer2"});
        runNft({"add", "chain", "netdev", m_tableName + "_layer2", "ingress",
                "{ type filter hook ingress device \"" + m_interface + "\" priority -500; }"});
    }

    // 3. Sets for DHCP gateway mode — allowed leases & admin whitelist
    runNft({"add", "set", "inet", m_tableName, "allowed_leases", "{ type ipv4_addr . ether_addr; }"});
    runNft({"add", "set", "inet", m_tableName, "whitelist", "{ type ether_addr; }"});

    runNft({"flush", "set", "inet", m_tableName, "allowed_leases"});

    // Whitelist: admin traffic is never interfered with
    runNft({"add", "rule", "inet", m_tableName, "filter_input",   "ether", "saddr", "@whitelist", "accept"});
    runNft({"add", "rule", "inet", m_tableName, "filter_forward", "ether", "saddr", "@whitelist", "accept"});
    runNft({"add", "rule", "inet", m_tableName, "filter_output",  "ether", "daddr", "@whitelist", "accept"});

    // Restore persisted state
    syncWhitelistedMACs();
    syncAllowedLeases();

    qInfo() << "[FirewallManager] Ready on interface" << (m_interface.isEmpty() ? "(none)" : m_interface);
}

bool FirewallManager::blockIP(const QString &ip) {
    return false;
}

bool FirewallManager::unblockIP(const QString &ip) {
    return false;
}

bool FirewallManager::blockMAC(const QString &mac) {
    if (mac.isEmpty() || mac == "Checking..." || !m_available) return false;
    QString lMac = mac.toLower();

    m_blockedMACs.insert(lMac);
    syncBlockedMACs();
    
    emit actionSuccess(QString("Blocked MAC: %1 (Host isolation active)").arg(lMac));
    return true;
}

void FirewallManager::setInterface(const QString &iface) {
    if (m_interface == iface) return;
    m_interface = iface;
    initFirewall();
}

void FirewallManager::setServerIP(const QString &ip) {
    if (m_serverIP == ip) return;
    m_serverIP = ip;
    // Internal host IP used for nftables rules — not a DHCP server address
}

bool FirewallManager::unblockMAC(const QString &mac) {
    QString lMac = mac.toLower();
    if (lMac.isEmpty()) return false;
    
    m_blockedMACs.remove(lMac);
    syncBlockedMACs();
    
    emit actionSuccess(QString("Unblocked MAC: %1").arg(lMac));
    return true;
}


bool FirewallManager::enableBlockPageForMAC(const QString &mac) {
    if (mac.isEmpty() || !m_available) return false;
    m_blockPageMACs.insert(mac.toLower());
    syncBlockPageMACs();
    return true;
}

bool FirewallManager::disableBlockPageForMAC(const QString &mac) {
    if (mac.isEmpty() || !m_available) return false;
    m_blockPageMACs.remove(mac.toLower());
    syncBlockPageMACs();
    return true;
}

bool FirewallManager::isMACBlocked(const QString &mac) const {
    return m_blockedMACs.contains(mac.toLower());
}

bool FirewallManager::isWhitelisted(const QString &mac) const {
    return m_whitelistedMACs.contains(mac.toLower());
}

bool FirewallManager::unblockAll() {
    m_blockedMACs.clear();
    int res = QProcess::execute("sh", {"-c", QString("nft delete table inet %1 2>/dev/null").arg(m_tableName)});
    if (!m_interface.isEmpty()) {
        QProcess::execute("sh", {"-c", QString("nft delete table netdev %1_layer2 2>/dev/null").arg(m_tableName)});
    }
    return res == 0;
}

bool FirewallManager::runNft(const QStringList &args) {
    QProcess proc;
    proc.start("nft", args);
    proc.waitForFinished(5000);

    int exitCode = proc.exitCode();
    if (exitCode != 0) {
        QString err = proc.readAllStandardError().trimmed();
        qDebug() << "[Firewall ERROR] nft" << args.join(" ");
        qDebug() << "[Firewall ERROR] stderr:" << err;
    }
    return (exitCode == 0);
}

bool FirewallManager::runCommand(const QString &cmd) {
    QProcess proc;
    proc.start("sh", QStringList() << "-c" << cmd + " 2>&1");
    proc.waitForFinished();
    
    int exitCode = proc.exitCode();
    QString output = proc.readAllStandardOutput().trimmed();
    
    if (exitCode != 0 && !output.isEmpty()) {
        qDebug() << "[Firewall ERROR] Command:" << cmd;
        qDebug() << "[Firewall ERROR] Output:" << output;
    }
    return (exitCode == 0);
}

bool FirewallManager::addAllowedLease(const QString &ip, const QString &mac) {
    if (!m_available || ip.isEmpty() || mac.isEmpty()) return false;
    m_allowedLeases.insert(ip, mac.toLower());
    syncAllowedLeases();
    return true;
}

bool FirewallManager::removeAllowedLease(const QString &ip, const QString &mac) {
    if (!m_available || ip.isEmpty() || mac.isEmpty()) return false;
    m_allowedLeases.remove(ip);
    syncAllowedLeases();
    return true;
}

bool FirewallManager::addWhitelistedMAC(const QString &mac) {
    if (!m_available || mac.isEmpty()) return false;
    m_whitelistedMACs.insert(mac.toLower());
    syncWhitelistedMACs();
    return true;
}

bool FirewallManager::removeWhitelistedMAC(const QString &mac) {
    if (!m_available || mac.isEmpty()) return false;
    m_whitelistedMACs.remove(mac.toLower());
    syncWhitelistedMACs();
    return true;
}

bool FirewallManager::setStrictMode(bool enable) {
    if (!m_available) return false;
    if (enable) {
        runNft({"flush", "chain", "inet", m_tableName, "filter_forward"});
        
        // 1. ALWAYS inject the global blocked_macs drop first so manual blocks override whitelists
        runNft({"add", "rule", "inet", m_tableName, "filter_forward", "ether", "saddr", "@blocked_macs", "drop"});

        // Use a single string for the concat-match to ensure nft parses it correctly
        runNft({"add", "rule", "inet", m_tableName, "filter_forward", "ip saddr . ether saddr @allowed_leases", "accept"});
        runNft({"add", "rule", "inet", m_tableName, "filter_forward", "ether", "saddr", "@whitelist", "accept"});
        
        // 2. Drop everything else (Strict Mode isolation)
        runNft({"add", "rule", "inet", m_tableName, "filter_forward", "drop"});
    } else {
        runNft({"flush", "chain", "inet", m_tableName, "filter_forward"});
        
        // Restore standard blocked MACs drop for regular Gateway mode
        runNft({"add", "rule", "inet", m_tableName, "filter_forward", "ether", "saddr", "@blocked_macs", "drop"});
    }
    return true;
}

void FirewallManager::flushArpCache(const QString &ip) {
    if (ip.isEmpty()) return;
    // Attempt to delete from the system ARP cache (requires root/capability)
    // ip neigh del <ip> dev <interface>
    runCommand(QString("ip neigh del %1 dev %2").arg(ip, m_interface));
    qDebug() << "FirewallManager: Flushed ARP cache for" << ip;
}

// ── Atomic Sync Helpers ──────────────────────────────────────────────────────

void FirewallManager::syncBlockedMACs() {
    if (!m_available) return;

    // 1. Clear the sets
    runCommand(QString("nft flush set inet %1 blocked_macs").arg(m_tableName));
    if (!m_interface.isEmpty()) {
        runCommand(QString("nft flush set netdev %1_layer2 blocked_macs").arg(m_tableName));
    }

    if (m_blockedMACs.isEmpty()) return;

    // 2. Repopulate (inet)
    QString elements = "{ " + QStringList(m_blockedMACs.values()).join(", ") + " }";
    runCommand(QString("nft add element inet %1 blocked_macs '%2'").arg(m_tableName, elements));

    // 3. Repopulate (netdev)
    if (!m_interface.isEmpty()) {
        runCommand(QString("nft add element netdev %1_layer2 blocked_macs '%2'").arg(m_tableName, elements));
    }
}

void FirewallManager::syncWhitelistedMACs() {
    if (!m_available) return;
    runCommand(QString("nft flush set inet %1 whitelist").arg(m_tableName));
    if (m_whitelistedMACs.isEmpty()) return;

    QString elements = "{ " + QStringList(m_whitelistedMACs.values()).join(", ") + " }";
    runCommand(QString("nft add element inet %1 whitelist '%2'").arg(m_tableName, elements));
}

void FirewallManager::syncAllowedLeases() {
    if (!m_available) return;
    runCommand(QString("nft flush set inet %1 allowed_leases").arg(m_tableName));
    if (m_allowedLeases.isEmpty()) return;

    QStringList flat;
    for (auto it = m_allowedLeases.begin(); it != m_allowedLeases.end(); ++it) {
        flat << QString("%1 . %2").arg(it.key(), it.value());
    }
    QString elements = "{ " + flat.join(", ") + " }";
    runCommand(QString("nft add element inet %1 allowed_leases '%2'").arg(m_tableName, elements));
}

void FirewallManager::syncBlockPageMACs() {
    if (!m_available) return;
    runCommand(QString("nft flush set inet %1 blockpage_macs").arg(m_tableName));
    if (m_blockPageMACs.isEmpty()) return;

    QString elements = "{ " + QStringList(m_blockPageMACs.values()).join(", ") + " }";
    runCommand(QString("nft add element inet %1 blockpage_macs '%2'").arg(m_tableName, elements));
}

} // namespace core
