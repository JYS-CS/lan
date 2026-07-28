#include "StaticLeaseDialog.h"

namespace gui {

StaticLeaseDialog::StaticLeaseDialog(const QString &mac, const QString &ip, const QString &hostname, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Add Static DHCP Lease");
    setMinimumWidth(300);

    QFormLayout *layout = new QFormLayout(this);

    m_macEdit = new QLineEdit(this);
    m_macEdit->setText(mac);
    
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

// Minimal theme application inside dialog
void StaticLeaseDialog::applyTheme() {
    setStyleSheet(
        "QDialog { background-color: #2b2b2b; color: #ffffff; }"
        "QLabel { color: #ffffff; }"
        "QLineEdit { background-color: #3c3f41; color: #ffffff; border: 1px solid #555555; padding: 4px; }"
        "QPushButton { background-color: #4e5254; color: white; border: 1px solid #555555; padding: 5px; min-width: 80px; }"
        "QPushButton:hover { background-color: #5e6264; }"
    );
}

} // namespace gui
