#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QList>
#include "Device.h"
#include "Types.h"

namespace core {

// A single blocked-device record, as stored in the blacklist table.
struct BlacklistEntry {
    QString mac;
    QString reason;
    QString blockedAt; // ISO 8601
};

class DatabaseManager : public QObject {
    Q_OBJECT

public:
    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }

    bool init(const QString &dbPath = "lan_monitor.db");

    // Device persistence
    void saveDevice(const Device &d);
    void removeDevice(const QString &ip);
    QList<Device> getAllDevices();
    void updateAlias(const QString &mac, const QString &alias);

    // Event persistence
    void saveEvent(const core::NetworkEvent &e);
    QList<core::NetworkEvent> getAllEvents(int limit = 500);

    // Blacklist / Whitelist
    void addToBlacklist(const QString &mac, const QString &reason);
    void removeFromBlacklist(const QString &mac);
    bool isBlacklisted(const QString &mac);
    QList<BlacklistEntry> getBlacklist();
    
    void addToWhitelist(const QString &mac);
    void removeFromWhitelist(const QString &mac);
    bool isWhitelisted(const QString &mac);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool setupSchema();
    QSqlDatabase m_db;
};

} // namespace core
