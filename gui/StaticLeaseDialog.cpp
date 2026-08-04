#include "StaticLeaseDialog.h"
#include "Theme.h"

namespace gui {

StaticLeaseDialog::StaticLeaseDialog(const QString &mac, const QString &ip, const QString &hostname, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Add Static DHCP Lease");
    setMinimumWidth(320);

    QFormLayout *layout = new QFormLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    m_macEdit = new QLineEdit(this);
    m_macEdit->setText(mac);
    m_macEdit->setPlaceholderText("AA:BB:CC:DD:EE:FF");
    
    m_ipEdit = new QLineEdit(this);
    m_ipEdit->setText(ip);
    m_ipEdit->setPlaceholderText("192.168.1.100");
    
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setText(hostname);
    m_hostEdit->setPlaceholderText("my-device");

    layout->addRow("MAC Address:", m_macEdit);
    layout->addRow("IP Address:", m_ipEdit);
    layout->addRow("Hostname:", m_hostEdit);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    applyTheme();
}

void StaticLeaseDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    Theme::fadeIn(this, 200);
}

// Matches the app's dark blue/orange duotone theme instead of the old generic gray.
void StaticLeaseDialog::applyTheme() {
    setStyleSheet(
        "QDialog { background-color: #0d1117; color: #e8eaf0; }"
        "QLabel { color: #8a93b8; font-size: 12px; }"
        "QLineEdit { background-color: #161b26; color: #e8eaf0; border: 1px solid rgba(255,255,255,0.10); "
        "   border-radius: 8px; padding: 7px 10px; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid #4f7fff; }"
        "QPushButton { background-color: #1c2230; color: #e8eaf0; border: 1px solid rgba(255,255,255,0.10); "
        "   border-radius: 8px; padding: 7px 16px; min-width: 80px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: #232a3d; }"
        "QDialogButtonBox QPushButton[text=\"OK\"] { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
        "   stop:0 #4f7fff, stop:1 #6d5cff); color: white; border: none; }"
    );
}

} // namespace gui
