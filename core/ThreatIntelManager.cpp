#include "ThreatIntelManager.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QHostAddress>

namespace core {

ThreatIntelManager::ThreatIntelManager(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_refreshTimer = new QTimer(this);
    // Upstream regenerates this list every ~2 hours; refreshing every 4
    // hours keeps us reasonably current without hammering GitHub.
    m_refreshTimer->setInterval(4 * 60 * 60 * 1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &ThreatIntelManager::refreshNow);
}

void ThreatIntelManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (enabled) {
        refreshNow();
        m_refreshTimer->start();
    } else {
        m_refreshTimer->stop();
    }
    emit statusChanged();
}

void ThreatIntelManager::refreshNow() {
    if (!m_enabled) return;
    emit refreshStarted();

    QNetworkRequest req{QUrl(kBlocklistUrl)};
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, &ThreatIntelManager::onReplyFinished);
}

void ThreatIntelManager::onReplyFinished() {
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit refreshFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QStringList entries;
    static const QRegularExpression validLine("^[0-9]{1,3}(\\.[0-9]{1,3}){3}(/[0-9]{1,2})?$");

    for (const QByteArray &lineBytes : data.split('\n')) {
        QString line = QString::fromLatin1(lineBytes).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (!validLine.match(line).hasMatch()) continue; // defensively skip anything malformed
        entries << line;
    }

    if (entries.isEmpty()) {
        emit refreshFailed("Blocklist fetch succeeded but contained no valid entries.");
        return;
    }

    m_entryCount = entries.size();
    m_lastUpdated = QDateTime::currentDateTime();

    m_ranges.clear();
    m_ranges.reserve(entries.size());
    for (const QString &e : entries) {
        QStringList parts = e.split('/');
        QHostAddress addr(parts[0]);
        quint32 base = addr.toIPv4Address();
        int prefixLen = parts.size() > 1 ? parts[1].toInt() : 32;
        if (prefixLen < 0 || prefixLen > 32) prefixLen = 32;
        quint32 mask = prefixLen == 0 ? 0 : (0xFFFFFFFFu << (32 - prefixLen));
        m_ranges.append({base & mask, mask});
    }

    emit blocklistUpdated(entries);
    emit statusChanged();
}

bool ThreatIntelManager::isKnownMalicious(const QString &ip) const {
    QHostAddress addr(ip);
    quint32 target = addr.toIPv4Address();
    if (target == 0) return false;
    for (const auto &r : m_ranges) {
        if ((target & r.mask) == r.base) return true;
    }
    return false;
}

} // namespace core
