#include "IPCalculatorDialog.h"
#include <QHeaderView>
#include <QGroupBox>

namespace gui {

IPCalculatorDialog::IPCalculatorDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("IP Network Calculator");
    setMinimumSize(700, 600);
    setupUi();
    applyTheme();
}

void IPCalculatorDialog::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    // 1. Forensic Tab
    QWidget *forensicTab = new QWidget();
    setupForensicTab(forensicTab);
    m_tabs->addTab(forensicTab, "🔍 Network Forensics");

    // 2. Subnetting Tab
    QWidget *subnetTab = new QWidget();
    setupSubnetTab(subnetTab);
    m_tabs->addTab(subnetTab, "🏗  Subnet Planner");

    // 3. CIDR Aggregation Tab
    QWidget *aggTab = new QWidget();
    setupAggregationTab(aggTab);
    m_tabs->addTab(aggTab, "🧬 CIDR Aggregator");

    mainLayout->addWidget(m_tabs);
}

void IPCalculatorDialog::setupForensicTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    
    QGroupBox *inputBox = new QGroupBox("Input Parameters", tab);
    QHBoxLayout *hLayout = new QHBoxLayout(inputBox);
    m_forensicIp = new QLineEdit("192.168.1.0", tab);
    m_forensicPrefix = new QSpinBox(tab);
    m_forensicPrefix->setRange(0, 128);
    m_forensicPrefix->setValue(24);
    QPushButton *calcBtn = new QPushButton("Calculate", tab);
    calcBtn->setStyleSheet("background-color: #2b5d7d; color: white; padding: 5px 15px; font-weight: bold;");
    
    hLayout->addWidget(new QLabel("IP Address:"));
    hLayout->addWidget(m_forensicIp);
    hLayout->addWidget(new QLabel("Prefix (CIDR):"));
    hLayout->addWidget(m_forensicPrefix);
    hLayout->addWidget(calcBtn);
    layout->addWidget(inputBox);

    m_forensicResults = new QTableWidget(0, 2, tab);
    m_forensicResults->setHorizontalHeaderLabels({"Parameter", "Result"});
    m_forensicResults->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_forensicResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_forensicResults);

    connect(calcBtn, &QPushButton::clicked, this, &IPCalculatorDialog::onCalculateForensic);
}

void IPCalculatorDialog::setupSubnetTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    
    QGroupBox *inputBox = new QGroupBox("Network Specification", tab);
    QFormLayout *form = new QFormLayout(inputBox);
    m_subnetIp = new QLineEdit("10.0.0.0", tab);
    m_subnetPrefix = new QSpinBox(tab); m_subnetPrefix->setRange(0, 32); m_subnetPrefix->setValue(8);
    m_subnetCount = new QSpinBox(tab); m_subnetCount->setRange(2, 256); m_subnetCount->setValue(4);
    QPushButton *calcBtn = new QPushButton("Generate Subnets", tab);
    calcBtn->setStyleSheet("background-color: #2d7d46; color: white; padding: 8px; font-weight: bold;");
    
    form->addRow("Base Network:", m_subnetIp);
    form->addRow("Base Prefix:", m_subnetPrefix);
    form->addRow("Divide into (count):", m_subnetCount);
    form->addRow(calcBtn);
    layout->addWidget(inputBox);

    m_subnetResults = new QTableWidget(0, 1, tab);
    m_subnetResults->setHorizontalHeaderLabels({"Calculated Subnet CIDRs"});
    m_subnetResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_subnetResults);

    connect(calcBtn, &QPushButton::clicked, this, &IPCalculatorDialog::onCalculateSubnets);
}

void IPCalculatorDialog::setupAggregationTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->addWidget(new QLabel("Enter list of networks (one per line, e.g. 192.168.1.0/24):"));
    m_aggInput = new QTextEdit(tab);
    m_aggInput->setPlaceholderText("10.0.0.0/24\n10.0.1.0/24\n10.0.2.0/24\n10.0.3.0/24");
    layout->addWidget(m_aggInput);
    
    QPushButton *calcBtn = new QPushButton("Aggregate into Supernet", tab);
    calcBtn->setStyleSheet("background-color: #7d5e2b; color: white; padding: 10px; font-weight: bold;");
    layout->addWidget(calcBtn);

    m_aggResultLabel = new QLabel("Aggregated Result: -", tab);
    m_aggResultLabel->setStyleSheet("font-size: 18px; color: #76b900; font-weight: bold; padding: 20px; border: 1px dashed #555;");
    m_aggResultLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_aggResultLabel);

    connect(calcBtn, &QPushButton::clicked, this, &IPCalculatorDialog::onAggregateCIDR);
}

void IPCalculatorDialog::onCalculateForensic() {
    core::NetworkInfo info = core::IPCalculator::calculate(m_forensicIp->text().trimmed(), m_forensicPrefix->value());
    m_forensicResults->setRowCount(0);
    if (!info.isValid) return;

    auto addRow = [&](QString param, QString val) {
        int r = m_forensicResults->rowCount();
        m_forensicResults->insertRow(r);
        m_forensicResults->setItem(r, 0, new QTableWidgetItem(param));
        m_forensicResults->setItem(r, 1, new QTableWidgetItem(val));
    };

    addRow("IP Version", info.ipVersion);
    addRow("Network Address", info.networkAddress);
    addRow("Broadcast Address", info.broadcastAddress);
    addRow("Usable Range", QString("%1 - %2").arg(info.firstHost).arg(info.lastHost));
    addRow("Total Hosts", QString::number(info.totalHosts));
    addRow("Usable Hosts", QString::number(info.usableHosts));
    addRow("Subnet Mask", info.subnetMask);
    addRow("IP Class", info.ipClass);
    addRow("Type", info.isPrivate ? "Private (RFC 1918)" : "Public");
    addRow("Reserved", info.reservedType.isEmpty() ? "No" : info.reservedType);
    addRow("Binary Representation", info.binaryRepresentation);
    addRow("Hex Representation", info.hexRepresentation);
}

void IPCalculatorDialog::onCalculateSubnets() {
    auto subnets = core::IPCalculator::createSubnets(m_subnetIp->text().trimmed(), m_subnetPrefix->value(), m_subnetCount->value());
    m_subnetResults->setRowCount(0);
    for (const auto &s : subnets) {
        int r = m_subnetResults->rowCount();
        m_subnetResults->insertRow(r);
        m_subnetResults->setItem(r, 0, new QTableWidgetItem(s));
    }
}

void IPCalculatorDialog::onAggregateCIDR() {
    QStringList lines = m_aggInput->toPlainText().split("\n", Qt::SkipEmptyParts);
    QString result = core::IPCalculator::aggregate(lines);
    m_aggResultLabel->setText(result.isEmpty() ? "Invalid Input" : "Aggregated Result: " + result);
}

void IPCalculatorDialog::applyTheme() {
    setStyleSheet(
        "QDialog { background-color: #2b2b2b; color: #ffffff; }"
        "QTabWidget::pane { border: 1px solid #444; background: #2b2b2b; }"
        "QTabBar::tab { background: #333; color: #aaa; padding: 10px 20px; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #2b5d7d; color: white; }"
        "QGroupBox { color: #ffffff; border: 1px solid #555; border-radius: 4px; margin-top: 10px; padding-top: 10px; }"
        "QLabel { color: #cccccc; }"
        "QLineEdit, QSpinBox, QTextEdit { background-color: #1e1e1e; color: #ffffff; border: 1px solid #444; padding: 5px; }"
        "QTableWidget { background-color: #1e1e1e; color: #ffffff; gridline-color: #333; border: none; }"
        "QHeaderView::section { background-color: #333; color: white; border: 1px solid #111; }"
    );
}

} // namespace gui
