#include "BlockHttpServer.h"
#include <QDebug>
#include <QTextStream>
#include <QFile>
#include <QProcess>
#include <QIODevice>

namespace core {

BlockHttpServer::BlockHttpServer(QObject *parent) : QObject(parent) {
    m_server = new QTcpServer(this);
    m_httpsServer = new QSslServer(this);
}

BlockHttpServer::~BlockHttpServer() {
    stop();
}

bool BlockHttpServer::start() {
    if (m_running) stop();

    // 1. Setup SSL
    setupSsl();

    // 2. Bind HTTP (80)
    if (!m_server->listen(QHostAddress::AnyIPv4, 80)) {
        qWarning() << "[BlockHttpServer] Failed to listen on port 80:" << m_server->errorString();
    } else {
        connect(m_server, &QTcpServer::newConnection, this, &BlockHttpServer::onNewConnection);
        qDebug() << "[BlockHttpServer] Listening on port 80 (HTTP)";
    }

    // 3. Bind HTTPS (443)
    if (!m_httpsServer->listen(QHostAddress::AnyIPv4, 443)) {
        qWarning() << "[BlockHttpServer] Failed to listen on port 443:" << m_httpsServer->errorString();
    } else {
        connect(m_httpsServer, &QSslServer::pendingConnectionAvailable, this, &BlockHttpServer::onNewSslConnection);
        qDebug() << "[BlockHttpServer] Listening on port 443 (HTTPS - TLS Isolation Active)";
    }

    m_running = true;
    return true;
}

void BlockHttpServer::stop() {
    if (!m_running) return;
    m_server->close();
    m_httpsServer->close();
    m_running = false;
}

void BlockHttpServer::setupSsl() {
    QString certPath = "server.crt";
    QString keyPath = "server.key";

    // Generate self-signed cert if missing
    if (!QFile::exists(certPath) || !QFile::exists(keyPath)) {
        qDebug() << "[BlockHttpServer] Generating self-signed SSL certificate...";
        QProcess proc;
        proc.start("openssl", {
            "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "365", "-nodes",
            "-keyout", keyPath, "-out", certPath,
            "-subj", "/C=US/ST=Security/L=LAN/O=LAN Monitor/OU=Security Authority/CN=LAN_Monitor_Block_Page"
        });
        proc.waitForFinished();
    }

    QSslConfiguration config;
    
    QFile certFile(certPath);
    if (certFile.open(QIODevice::ReadOnly)) {
        QSslCertificate cert(&certFile, QSsl::Pem);
        config.setLocalCertificate(cert);
    }

    QFile keyFile(keyPath);
    if (keyFile.open(QIODevice::ReadOnly)) {
        QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
        config.setPrivateKey(key);
    }

    config.setProtocol(QSsl::AnyProtocol);
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    
    m_httpsServer->setSslConfiguration(config);
}

void BlockHttpServer::onNewSslConnection() {
    while (m_httpsServer->hasPendingConnections()) {
        QSslSocket *socket = qobject_cast<QSslSocket*>(m_httpsServer->nextPendingConnection());
        if (socket) {
            connect(socket, &QSslSocket::readyRead, this, &BlockHttpServer::onReadyRead);
            connect(socket, &QSslSocket::disconnected, socket, &QTcpSocket::deleteLater);
            connect(socket, &QSslSocket::sslErrors, this, &BlockHttpServer::onSslErrors);
        }
    }
}

void BlockHttpServer::onSslErrors(const QList<QSslError> &errors) {
    for (const auto &err : errors) {
        // Ignore "Self Signed" and "Host Name Mismatch" - we ARE a MITM for the block page
        if (err.error() == QSslError::SelfSignedCertificate || 
            err.error() == QSslError::HostNameMismatch) {
            qobject_cast<QSslSocket*>(sender())->ignoreSslErrors();
            return;
        }
    }
}

void BlockHttpServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &BlockHttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void BlockHttpServer::onReadyRead() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray data = socket->readAll();
    QString request(data);
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) return;

    QString firstLine = lines[0];
    QStringList parts = firstLine.split(" ");
    if (parts.size() < 2) return;

    QString path = parts[1];
    QString host;
    for (const QString &line : lines) {
        if (line.toLower().startsWith("host:")) {
            host = line.mid(5).trimmed();
            break;
        }
    }

    QString localIp = socket->localAddress().toString();
    if (localIp.startsWith("::ffff:")) localIp = localIp.mid(7);

    // 1. Handle Captive Portal API (RFC 8908)
    if (path == "/.well-known/captive-portal") {
        QByteArray json = QString("{\"captive\": true, \"user-portal-url\": \"http://%1/\"}").arg(localIp).toUtf8();
        QByteArray response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/captive+json\r\n"
                              "Content-Length: " + QByteArray::number(json.size()) + "\r\n"
                              "Connection: close\r\n"
                              "\r\n" + json;
        socket->write(response);
    }
    // 2. Mock Apple/Android Success (for CNA popup)
    else if (path.contains("generate_204") || path.contains("hotspot-detect.html") || path.contains("success.html")) {
        // To trigger the popup, we respond with a 302 redirect to the portal.
        // Modern OSs see this redirect during the "Canary" check and pop the "Sign in to network" window.
        QByteArray response = "HTTP/1.1 302 Found\r\n"
                              "Location: http://" + localIp.toUtf8() + "/\r\n"
                              "Content-Length: 0\r\n"
                              "Connection: close\r\n\r\n";
        socket->write(response);
    }
    // 3. Main Portal Page
    else if (path == "/" || host.startsWith(localIp)) {
        QString html = buildHtmlPage();
        QByteArray response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html\r\n"
                              "Connection: close\r\n"
                              "Content-Length: " + QByteArray::number(html.toUtf8().size()) + "\r\n"
                              "\r\n" + html.toUtf8();
        socket->write(response);
    }
    // 4. Global Redirection (302)
    else {
        qDebug() << "[BlockHttpServer] Redirecting" << host + path << "to local portal (via 302)";
        QByteArray response = "HTTP/1.1 302 Found\r\n"
                              "Location: http://" + localIp.toUtf8() + "/\r\n"
                              "Content-Length: 0\r\n"
                              "Connection: close\r\n\r\n";
        socket->write(response);
    }

    socket->disconnectFromHost();
}

QString BlockHttpServer::buildHtmlPage() {
    return R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ACCESS RESTRICTED | LAN Monitor</title>
    <style>
        body {
            margin: 0;
            padding: 0;
            background-color: #0d0f14;
            color: #e8eaf0;
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            overflow: hidden;
        }

        .background {
            position: absolute;
            top: 0; left: 0; right: 0; bottom: 0;
            background: radial-gradient(circle at 50% 50%, #1e2230 0%, #0d0f14 100%);
            z-index: -1;
        }

        .glow {
            position: absolute;
            width: 400px;
            height: 400px;
            background: #4f7fff;
            filter: blur(150px);
            opacity: 0.1;
            border-radius: 50%;
            animation: pulse 8s infinite alternate;
        }

        @keyframes pulse {
            from { transform: scale(1); opacity: 0.1; }
            to { transform: scale(1.5); opacity: 0.15; }
        }

        .card {
            background: rgba(24, 27, 34, 0.8);
            backdrop-filter: blur(20px);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 24px;
            padding: 48px;
            text-align: center;
            max-width: 420px;
            box-shadow: 0 40px 100px rgba(0, 0, 0, 0.5);
        }

        .logo {
            width: 80px;
            height: 80px;
            margin-bottom: 24px;
        }

        h1 {
            font-size: 24px;
            font-weight: 700;
            letter-spacing: -0.02em;
            margin: 0 0 12px 0;
            color: #f05252;
        }

        p {
            color: #7c8299;
            font-size: 16px;
            line-height: 1.6;
            margin: 0 0 32px 0;
        }

        .status-pill {
            background: rgba(240, 82, 82, 0.1);
            color: #f05252;
            padding: 8px 16px;
            border-radius: 100px;
            font-size: 13px;
            font-weight: 600;
            display: inline-block;
            margin-bottom: 32px;
            border: 1px solid rgba(240, 82, 82, 0.2);
        }

        .divider {
            height: 1px;
            background: rgba(255, 255, 255, 0.05);
            margin: 32px 0;
        }

        .footer {
            font-size: 12px;
            color: #4a5068;
        }

        svg {
            fill: #f05252;
            margin-bottom: 24px;
        }
    </style>
</head>
<body>
    <div class="background"></div>
    <div class="glow"></div>
    <div class="card">
        <svg width="80" height="80" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
            <line x1="12" y1="9" x2="12" y2="13"></line>
            <line x1="12" y1="17" x2="12.01" y2="17"></line>
        </svg>
        <h1>Security Isolation Active</h1>
        <div class="status-pill">ACCESS RESTRICTED</div>
        <p>This device has been isolated from the network by the LAN Monitor Administrator. Your internet access is temporarily suspended for security reasons.</p>
        <div class="divider"></div>
        <div class="footer">
            NETWORK SECURITY PROTOCOL v1.0<br>
            Managed by NETWATCH Infrastructure
        </div>
    </div>
</body>
</html>
    )";
}

} // namespace core
