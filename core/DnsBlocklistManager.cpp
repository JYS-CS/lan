#include "DnsBlocklistManager.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace core {

DnsBlocklistManager::DnsBlocklistManager(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_refreshTimer = new QTimer(this);
    // StevenBlack/hosts is regenerated roughly daily; refreshing every 12
    // hours is more than current enough without hammering GitHub for an
    // ~80k-line file.
    m_refreshTimer->setInterval(12 * 60 * 60 * 1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &DnsBlocklistManager::refreshNow);
}

void DnsBlocklistManager::setEnabled(bool enabled) {
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

void DnsBlocklistManager::refreshNow() {
    if (!m_enabled) return;
    emit refreshStarted();

    QNetworkRequest req{QUrl(kBlocklistUrl)};
    req.setTransferTimeout(60000); // larger file (~80k lines) than the IP list
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, &DnsBlocklistManager::onReplyFinished);
}

void DnsBlocklistManager::onReplyFinished() {
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit refreshFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QStringList domains;
    domains.reserve(90000);

    for (const QByteArray &lineBytes : data.split('\n')) {
        QString line = QString::fromLatin1(lineBytes).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        // Standard hosts-file blocklist format: "0.0.0.0 domain.tld"
        // (some sources use 127.0.0.1 instead — accept both).
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;
        if (parts[0] != "0.0.0.0" && parts[0] != "127.0.0.1") continue;

        QString domain = parts[1].toLower();
        if (domain.isEmpty() || domain == "0.0.0.0" || domain == "localhost" ||
            domain == "localhost.localdomain" || domain == "local" || domain == "broadcasthost") continue;

        domains << domain;
    }

    if (domains.isEmpty()) {
        emit refreshFailed("Blocklist fetch succeeded but contained no valid domains.");
        return;
    }

    m_entryCount = domains.size();
    m_lastUpdated = QDateTime::currentDateTime();
    emit blocklistUpdated(domains);
    emit statusChanged();
}

} // namespace core
