#include "PortScanDialog.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>

namespace gui {

PortScanDialog::PortScanDialog(const QString &ip, QWidget *parent)
    : QDialog(parent), m_ip(ip) {
    setWindowTitle("Audit Services — " + ip);
    resize(450, 500);
    setupUi();
    applyTheme();

    m_scanner = new core::PortScanner(ip, this);
    connect(m_scanner, &core::PortScanner::resultFound, this, &PortScanDialog::onResultFound);
    connect(m_scanner, &core::PortScanner::progress, this, &PortScanDialog::onProgress);
    connect(m_scanner, &core::PortScanner::finished, this, &PortScanDialog::onFinished);

    // Common ports to probe
    m_scanner->start({21, 22, 23, 25, 53, 80, 110, 139, 443, 445, 1433, 3306, 3389, 5432, 8080});
}

void PortScanDialog::setupUi() {
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(20, 20, 20, 20);
    main->setSpacing(15);

    m_statusLabel = new QLabel("Scanning common ports for " + m_ip + "...", this);
    m_statusLabel->setStyleSheet("color: #e8eaf0; font-size: 13px; font-weight: 500;");
    main->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setTextVisible(false);
    main->addWidget(m_progressBar);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({"PORT", "SERVICE"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    main->addWidget(m_table);

    QPushButton *closeBtn = new QPushButton("Close", this);
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    main->addLayout(btnLayout);
}

void PortScanDialog::applyTheme() {
    setStyleSheet(
        "QDialog { background-color: #111318; }"
        "QTableWidget { background: #181b22; border-radius: 8px; color: #e8eaf0; gridline-color: transparent; border: 1px solid rgba(255,255,255,0.05); }"
        "QHeaderView::section { background: transparent; color: #4a5068; font-size: 10px; font-weight: bold; border: none; padding: 10px; }"
        "QProgressBar { background: #1e2230; border-radius: 3px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #4f7fff, stop:1 #ff9142); border-radius: 3px; }"
        "QPushButton { background: #1e2230; color: #e8eaf0; border-radius: 6px; padding: 8px 16px; border: 0.5px solid rgba(255,255,255,0.1); }"
        "QPushButton:hover { background: #252a3d; }"
    );
}

void PortScanDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    Theme::fadeIn(this, 200);
}

void PortScanDialog::onResultFound(const core::PortResult &res) {
    int row = m_table->rowCount();
    m_table->insertRow(row);
    
    QTableWidgetItem *portItem = new QTableWidgetItem(QString::number(res.port));
    portItem->setForeground(QColor("#4f7fff"));
    portItem->setFont(QFont("monospace", 10, QFont::Bold));
    
    QTableWidgetItem *svcItem = new QTableWidgetItem(res.service);
    svcItem->setForeground(QColor("#2dd98f"));
    
    m_table->setItem(row, 0, portItem);
    m_table->setItem(row, 1, svcItem);
}

void PortScanDialog::onProgress(int current, int total) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
}

void PortScanDialog::onFinished() {
    m_progressBar->setValue(m_progressBar->maximum());
    m_statusLabel->setText("Audit complete. " + QString::number(m_table->rowCount()) + " open services found.");
    Theme::pulse(m_statusLabel);
}

} // namespace gui
