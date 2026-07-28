#include "DHCPServerDialog.h"
#include <QHBoxLayout>
#include <QFrame>

namespace gui {

DHCPServerDialog::DHCPServerDialog(QWidget *parent)
    : DHCPServerDialog("", "", "", parent) {}

DHCPServerDialog::DHCPServerDialog(const QString &detectedGateway,
                                   const QString &detectedInterface,
                                   const QString &detectedSubnet,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("DHCP Server Configuration");
    setMinimumWidth(460);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setSpacing(12);

    // ── Info banner ──────────────────────────────────────────────────────────
    QLabel *info = new QLabel(
        "Configure this device as your network's DHCP server.\n"
        "After starting, disable DHCP on your router to hand over control.", this);
    info->setWordWrap(true);
    info->setStyleSheet("color: #aaaaaa; font-size: 11px; padding: 4px;");
    root->addWidget(info);

    // ── Server Settings ──────────────────────────────────────────────────────
    QGroupBox *box = new QGroupBox("Server Settings", this);
    QFormLayout *form = new QFormLayout(box);
    form->setSpacing(8);
    form->setLabelAlignment(Qt::AlignRight);

    m_ifaceEdit      = new QLineEdit(detectedInterface, this);
    m_rangeStartEdit = new QLineEdit(this);
    m_rangeEndEdit   = new QLineEdit(this);
    m_subnetEdit     = new QLineEdit(detectedSubnet.isEmpty() ? "255.255.255.0" : detectedSubnet, this);
    m_gatewayEdit    = new QLineEdit(detectedGateway, this);
    m_dnsEdit        = new QLineEdit("8.8.8.8,8.8.4.4", this);
    m_leaseEdit      = new QLineEdit("24h", this);

    // Auto-calculate a sensible DHCP range from the detected gateway
    // e.g. gateway = 192.168.8.1 → range 192.168.8.100 – 192.168.8.200
    if (!detectedGateway.isEmpty()) {
        QStringList parts = detectedGateway.split(".");
        if (parts.size() == 4) {
            QString prefix = parts[0] + "." + parts[1] + "." + parts[2] + ".";
            m_rangeStartEdit->setText(prefix + "100");
            m_rangeEndEdit->setText(prefix + "200");
        }
    } else {
        m_rangeStartEdit->setText("192.168.8.100");
        m_rangeEndEdit->setText("192.168.8.200");
    }

    form->addRow("Interface:", m_ifaceEdit);
    form->addRow("Range Start:", m_rangeStartEdit);
    form->addRow("Range End:", m_rangeEndEdit);
    form->addRow("Subnet Mask:", m_subnetEdit);
    form->addRow("Gateway IP:", m_gatewayEdit);
    form->addRow("DNS Servers:", m_dnsEdit);
    form->addRow("Lease Duration:", m_leaseEdit);

    root->addWidget(box);

    // ── Status label ─────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Server not running.", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 11px; padding: 4px;");
    root->addWidget(m_statusLabel);

    // ── Buttons ───────────────────────────────────────────────────────────────
    QHBoxLayout *btnRow = new QHBoxLayout();

    m_startBtn = new QPushButton("▶  Start DHCP Server", this);
    m_startBtn->setStyleSheet("background-color: #2d7d46; color: white; font-weight: bold; padding: 8px;");

    m_stopBtn = new QPushButton("■  Stop Server", this);
    m_stopBtn->setStyleSheet("background-color: #7d2d2d; color: white; font-weight: bold; padding: 8px;");
    m_stopBtn->setEnabled(false);

    QPushButton *closeBtn = new QPushButton("Close", this);

    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_stopBtn);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_startBtn, &QPushButton::clicked, this, &DHCPServerDialog::onStartClicked);
    connect(m_stopBtn,  &QPushButton::clicked, this, &DHCPServerDialog::stopServer);
    connect(m_stopBtn,  &QPushButton::clicked, this, [this]() {
        m_serverStarted = false;
        m_startBtn->setEnabled(true);
        m_stopBtn->setEnabled(false);
        setStatus("Server stopped.");
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    applyTheme();
}

void DHCPServerDialog::onStartClicked() {
    core::DHCPServerConfig cfg;
    cfg.interface     = m_ifaceEdit->text().trimmed();
    cfg.rangeStart    = m_rangeStartEdit->text().trimmed();
    cfg.rangeEnd      = m_rangeEndEdit->text().trimmed();
    cfg.subnetMask    = m_subnetEdit->text().trimmed();
    cfg.gateway       = m_gatewayEdit->text().trimmed();
    cfg.dns           = m_dnsEdit->text().trimmed();
    cfg.leaseDuration = m_leaseEdit->text().trimmed();

    if (cfg.rangeStart.isEmpty() || cfg.rangeEnd.isEmpty()) {
        setStatus("Range Start and End are required.", true);
        return;
    }

    setStatus("Starting DHCP server...");
    m_startBtn->setEnabled(false);

    emit startServer(cfg);
}

void DHCPServerDialog::onStatusUpdate(const QString &msg) {
    m_serverStarted = true;
    m_stopBtn->setEnabled(true);
    setStatus(msg);
}

void DHCPServerDialog::onError(const QString &msg) {
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    setStatus(msg, true);
}

core::DHCPServerConfig DHCPServerDialog::getConfig() const {
    core::DHCPServerConfig cfg;
    cfg.interface     = m_ifaceEdit->text().trimmed();
    cfg.rangeStart    = m_rangeStartEdit->text().trimmed();
    cfg.rangeEnd      = m_rangeEndEdit->text().trimmed();
    cfg.subnetMask    = m_subnetEdit->text().trimmed();
    cfg.gateway       = m_gatewayEdit->text().trimmed();
    cfg.dns           = m_dnsEdit->text().trimmed();
    cfg.leaseDuration = m_leaseEdit->text().trimmed();
    return cfg;
}

void DHCPServerDialog::setStatus(const QString &msg, bool isError) {
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(
        isError
        ? "color: #e05252; font-size: 11px; padding: 4px;"
        : "color: #76b900; font-size: 11px; padding: 4px;"
    );
}

void DHCPServerDialog::applyTheme() {
    setStyleSheet(
        "QDialog { background-color: #2b2b2b; color: #ffffff; }"
        "QGroupBox { color: #ffffff; border: 1px solid #555555; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QLabel { color: #cccccc; }"
        "QLineEdit { background-color: #3c3f41; color: #ffffff; border: 1px solid #555555; padding: 5px; border-radius: 3px; }"
        "QLineEdit:focus { border: 1px solid #76b900; }"
        "QPushButton { background-color: #4e5254; color: white; border: 1px solid #555555; padding: 6px 12px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #5e6264; }"
        "QPushButton:disabled { background-color: #323537; color: #777777; }"
    );
}

} // namespace gui
