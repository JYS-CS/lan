#include "IPCalculatorPage.h"
#include "Theme.h"
#include <QHeaderView>
#include <QDialog>
#include <QMenu>
#include <QAction>
#include <QNetworkInterface>
#include <QCursor>

namespace gui {

IPCalculatorPage::IPCalculatorPage(QWidget *parent) : QWidget(parent) {
    setupUi();
    applyTheme();
}

void IPCalculatorPage::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    QLabel *titleLabel = new QLabel("IP Network Calculator", this);
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: white;");
    mainLayout->addWidget(titleLabel);

    m_tabs = new QTabWidget(this);

    QWidget *forensicTab = new QWidget();
    setupForensicTab(forensicTab);
    m_tabs->addTab(forensicTab, Theme::tintedIcon(":/resources/forensics.svg", 16, Theme::AccentBlue), " Network Forensics");

    QWidget *subnetTab = new QWidget();
    setupSubnetTab(subnetTab);
    m_tabs->addTab(subnetTab, Theme::tintedIcon(":/resources/subnet.svg", 16, Theme::AccentBlue), " Subnet Planner");

    QWidget *aggTab = new QWidget();
    setupAggregationTab(aggTab);
    m_tabs->addTab(aggTab, Theme::tintedIcon(":/resources/merger.svg", 16, Theme::AccentOrange), " Network Merger");

    QWidget *refTab = new QWidget();
    setupReferenceTab(refTab);
    m_tabs->addTab(refTab, Theme::tintedIcon(":/resources/reference.svg", 16, Theme::AccentBlue), " Reference Sheet");

    mainLayout->addWidget(m_tabs);
}

void IPCalculatorPage::setupForensicTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);
    
    QWidget *inputBar = new QWidget(tab);
    QHBoxLayout *hLayout = new QHBoxLayout(inputBar);
    hLayout->setContentsMargins(0, 0, 0, 0);
    
    m_forensicIp = new QLineEdit("192.168.1.1", tab);
    
    m_forensicPrefix = new QSpinBox(tab);
    m_forensicPrefix->setRange(0, 32);
    m_forensicPrefix->setValue(24);
    m_forensicPrefix->setAlignment(Qt::AlignCenter);
    
    QPushButton *btnMinus = new QPushButton("−", tab); btnMinus->setObjectName("SpinBtn");
    QPushButton *btnPlus = new QPushButton("+", tab); btnPlus->setObjectName("SpinBtn");
    connect(btnMinus, &QPushButton::clicked, this, [this]() { m_forensicPrefix->setValue(m_forensicPrefix->value() - 1); });
    connect(btnPlus, &QPushButton::clicked, this, [this]() { m_forensicPrefix->setValue(m_forensicPrefix->value() + 1); });
    
    QHBoxLayout *prefixLayout = new QHBoxLayout();
    prefixLayout->setSpacing(2);
    prefixLayout->addWidget(btnMinus);
    prefixLayout->addWidget(m_forensicPrefix);
    prefixLayout->addWidget(btnPlus);
    
    m_importBtn = new QPushButton("Auto-detect Local", tab);
    m_importBtn->setObjectName("Ghost");
    
    QPushButton *clearBtn = new QPushButton("Clear", tab);
    clearBtn->setObjectName("Ghost");
    
    QPushButton *calcBtn = new QPushButton("Analyze", tab);
    calcBtn->setObjectName("Primary");
    
    hLayout->addWidget(m_forensicIp, 2);
    QLabel *slash = new QLabel("/", tab);
    slash->setStyleSheet("color: #7c8299; font-size: 16px; margin: 0 5px;");
    hLayout->addWidget(slash);
    hLayout->addLayout(prefixLayout);
    hLayout->addSpacing(10);
    hLayout->addWidget(m_importBtn);
    hLayout->addWidget(clearBtn);
    hLayout->addWidget(calcBtn);
    layout->addWidget(inputBar);

    m_cardLayout = new QGridLayout();
    m_cardLayout->setSpacing(10);
    layout->addLayout(m_cardLayout);

    m_bitwiseCard = new QWidget(tab);
    m_bitwiseCard->setObjectName("Card");
    m_bitwiseCard->setVisible(false);
    QVBoxLayout *bitL = new QVBoxLayout(m_bitwiseCard);
    
    QLabel *bitTitle = new QLabel("BITWISE VISUALIZER", tab);
    bitTitle->setObjectName("SectionLabel");
    bitL->addWidget(bitTitle);
    bitL->addSpacing(5);
    
    m_bitVisualizer = new QWidget(tab);
    m_bitVisualizer->setMinimumHeight(35);
    bitL->addWidget(m_bitVisualizer);
    
    QHBoxLayout *legendL = new QHBoxLayout();
    QLabel *netBox = new QLabel("■", tab); netBox->setStyleSheet("color: rgba(79,127,255,0.7);");
    QLabel *netTxt = new QLabel("Network bits", tab); netTxt->setStyleSheet("color: #7c8299; font-size: 11px;");
    QLabel *hostBox = new QLabel("■", tab); hostBox->setStyleSheet("color: rgba(45,217,143,0.7);");
    QLabel *hostTxt = new QLabel("Host bits", tab); hostTxt->setStyleSheet("color: #7c8299; font-size: 11px;");
    legendL->addWidget(netBox); legendL->addWidget(netTxt);
    legendL->addSpacing(15);
    legendL->addWidget(hostBox); legendL->addWidget(hostTxt);
    legendL->addStretch();
    bitL->addLayout(legendL);
    
    layout->addWidget(m_bitwiseCard);

    m_breakdownCard = new QWidget(tab);
    m_breakdownCard->setObjectName("Card");
    m_breakdownCard->setVisible(false);
    QVBoxLayout *breakL = new QVBoxLayout(m_breakdownCard);
    
    QLabel *breakTitle = new QLabel("FULL BREAKDOWN", tab);
    breakTitle->setObjectName("SectionLabel");
    breakL->addWidget(breakTitle);
    
    m_forensicResults = new QTableWidget(5, 2, tab);
    m_forensicResults->horizontalHeader()->hide();
    m_forensicResults->verticalHeader()->hide();
    m_forensicResults->setSelectionMode(QAbstractItemView::NoSelection);
    m_forensicResults->setShowGrid(false);
    m_forensicResults->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_forensicResults->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_forensicResults->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_forensicResults->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_forensicResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_forensicResults->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    breakL->addWidget(m_forensicResults);
    
    m_generateBtn = new QPushButton("Generate Audit List", tab);
    m_generateBtn->setObjectName("GreenAction");
    m_generateBtn->setEnabled(false);
    breakL->addWidget(m_generateBtn);
    
    layout->addWidget(m_breakdownCard);
    layout->addStretch();

    connect(calcBtn, &QPushButton::clicked, this, &IPCalculatorPage::onCalculateForensic);
    
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_forensicIp->clear();
        m_forensicPrefix->setValue(24);
        m_bitwiseCard->setVisible(false);
        m_breakdownCard->setVisible(false);
        
        QLayoutItem *child;
        while ((child = m_cardLayout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }
    });
    
    connect(m_importBtn, &QPushButton::clicked, this, [this]() {
        auto ifaces = core::IPCalculator::getLocalInterfaces();
        if (ifaces.empty()) return;

        if (ifaces.size() == 1) {
            m_forensicIp->setText(ifaces[0].ip);
            m_forensicPrefix->setValue(ifaces[0].prefix);
            onCalculateForensic();
        } else {
            QMenu *menu = new QMenu(this);
            for (const auto &iface : ifaces) {
                QAction *act = menu->addAction(QString("%1 (%2)").arg(iface.name).arg(iface.ip));
                connect(act, &QAction::triggered, this, [this, iface]() {
                    m_forensicIp->setText(iface.ip);
                    m_forensicPrefix->setValue(iface.prefix);
                    onCalculateForensic();
                });
            }
            menu->exec(QCursor::pos());
        }
    });

    connect(m_generateBtn, &QPushButton::clicked, this, [this]() {
        if (m_lastResult.isValid && !m_lastResult.firstHost.isEmpty()) {
            QStringList range = core::IPCalculator::generateRange(m_lastResult.firstHost, m_lastResult.lastHost);
            QDialog *dlg = new QDialog(this);
            dlg->setWindowTitle("Generated Range: " + m_lastResult.cidrNotation);
            dlg->setMinimumSize(400, 500);
            dlg->setStyleSheet("background-color: #111318; color: #e8eaf0;");
            QVBoxLayout *l = new QVBoxLayout(dlg);
            QTextEdit *edit = new QTextEdit(dlg);
            edit->setStyleSheet("background: #1e2230; color: #e8eaf0; border: 0.5px solid rgba(255,255,255,0.12); font-family: monospace;");
            edit->setPlainText(range.join("\n"));
            edit->setReadOnly(true);
            l->addWidget(new QLabel(QString("Found %1 usable hosts:").arg(range.size())));
            l->addWidget(edit);
            QPushButton *btn = new QPushButton("Close", dlg);
            btn->setObjectName("Ghost");
            connect(btn, &QPushButton::clicked, dlg, &QDialog::accept);
            l->addWidget(btn);
            dlg->exec();
            dlg->deleteLater();
        }
    });
}

void IPCalculatorPage::setupSubnetTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);
    
    QWidget *inputCard = new QWidget(tab);
    inputCard->setObjectName("Card");
    QVBoxLayout *inputLayout = new QVBoxLayout(inputCard);
    
    QLabel *subTitle = new QLabel("SUBNET DIVISION", tab);
    subTitle->setObjectName("SectionLabel");
    inputLayout->addWidget(subTitle);
    
    QGridLayout *grid = new QGridLayout();
    grid->setVerticalSpacing(15);
    
    auto createLbl = [](QString txt) {
        QLabel *l = new QLabel(txt);
        l->setStyleSheet("color: #7c8299; text-align: right; min-width: 130px;");
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };
    
    m_subnetIp = new QLineEdit("10.0.0.0", tab);
    m_subnetPrefix = new QSpinBox(tab); m_subnetPrefix->setRange(0, 32); m_subnetPrefix->setValue(8);
    m_subnetCount = new QSpinBox(tab); m_subnetCount->setRange(2, 256); m_subnetCount->setValue(4);
    
    grid->addWidget(createLbl("Base Network"), 0, 0);
    grid->addWidget(m_subnetIp, 0, 1);
    
    grid->addWidget(createLbl("Base Prefix"), 1, 0);
    grid->addWidget(m_subnetPrefix, 1, 1);
    
    grid->addWidget(createLbl("Divide into (count)"), 2, 0);
    grid->addWidget(m_subnetCount, 2, 1);
    
    inputLayout->addLayout(grid);
    
    QPushButton *calcBtn = new QPushButton("Generate Subnets", tab);
    calcBtn->setObjectName("Primary");
    
    QPushButton *clearSubnetBtn = new QPushButton("Clear", tab);
    clearSubnetBtn->setObjectName("Ghost");
    
    QHBoxLayout *subBtnL = new QHBoxLayout();
    subBtnL->addStretch();
    subBtnL->addWidget(clearSubnetBtn);
    subBtnL->addWidget(calcBtn);
    
    inputLayout->addSpacing(10);
    inputLayout->addLayout(subBtnL);
    
    layout->addWidget(inputCard);

    m_subnetResults = new QTableWidget(0, 4, tab);
    m_subnetResults->setHorizontalHeaderLabels({"#", "CIDR", "RANGE", "HOSTS"});
    m_subnetResults->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_subnetResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_subnetResults->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_subnetResults);

    connect(calcBtn, &QPushButton::clicked, this, &IPCalculatorPage::onCalculateSubnets);
    
    connect(clearSubnetBtn, &QPushButton::clicked, this, [this]() {
        m_subnetResults->setRowCount(0);
        m_subnetIp->clear();
    });
}

void IPCalculatorPage::setupAggregationTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);
    
    QWidget *inputCard = new QWidget(tab);
    inputCard->setObjectName("Card");
    QVBoxLayout *inputLayout = new QVBoxLayout(inputCard);
    
    QLabel *aggTitle = new QLabel("LIST OF NETWORKS TO COMBINE", tab);
    aggTitle->setObjectName("SectionLabel");
    inputLayout->addWidget(aggTitle);
    
    m_aggInput = new QTextEdit(tab);
    m_aggInput->setPlainText("192.168.1.0/24\n192.168.2.0/24\n192.168.3.0/24"); // Predefined examples
    m_aggInput->setStyleSheet("font-family: monospace; font-size: 12px; background: #1e2230; border: 0.5px solid rgba(255,255,255,0.12); border-radius: 6px; line-height: 1.6; min-height: 100px;");
    inputLayout->addWidget(m_aggInput);
    
    QPushButton *calcBtn = new QPushButton("Combine into Single Network", tab);
    calcBtn->setObjectName("Primary");
    
    QPushButton *clearAggBtn = new QPushButton("Clear", tab);
    clearAggBtn->setObjectName("Ghost");
    
    QHBoxLayout *aggBtnL = new QHBoxLayout();
    aggBtnL->addStretch();
    aggBtnL->addWidget(clearAggBtn);
    aggBtnL->addWidget(calcBtn);
    
    inputLayout->addSpacing(10);
    inputLayout->addLayout(aggBtnL);
    
    layout->addWidget(inputCard);

    m_aggResultContainer = new QWidget(tab);
    m_aggResultContainer->setStyleSheet("background: rgba(45,217,143,0.12); border: 0.5px solid rgba(45,217,143,0.2); border-radius: 10px; padding: 20px;");
    QVBoxLayout *resLayout = new QVBoxLayout(m_aggResultContainer);
    resLayout->setAlignment(Qt::AlignCenter);
    
    m_aggResultValue = new QLabel("-", tab);
    m_aggResultValue->setStyleSheet("font-size: 22px; font-weight: 500; color: #2dd98f; font-family: monospace; background: transparent; border: none;");
    m_aggResultValue->setAlignment(Qt::AlignCenter);
    
    m_aggResultSubtitle = new QLabel("Waiting for input...", tab);
    m_aggResultSubtitle->setStyleSheet("font-size: 12px; color: #7c8299; background: transparent; border: none;");
    m_aggResultSubtitle->setAlignment(Qt::AlignCenter);
    
    resLayout->addWidget(m_aggResultValue);
    resLayout->addWidget(m_aggResultSubtitle);
    
    layout->addWidget(m_aggResultContainer);
    layout->addStretch();

    connect(calcBtn, &QPushButton::clicked, this, &IPCalculatorPage::onAggregateCIDR);
    
    connect(clearAggBtn, &QPushButton::clicked, this, [this]() {
        m_aggInput->clear();
        m_aggResultValue->setText("-");
        m_aggResultValue->setStyleSheet("font-size: 22px; font-weight: 500; color: #2dd98f; font-family: monospace; background: transparent; border: none;");
        m_aggResultSubtitle->setText("Waiting for input...");
    });
}

void IPCalculatorPage::setupReferenceTab(QWidget *tab) {
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(20, 20, 20, 20);
    
    QTableWidget *table = new QTableWidget(32, 4, tab);
    table->setHorizontalHeaderLabels({"PREFIX", "SUBNET MASK", "USABLE HOSTS", "TOTAL IPS"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);

    for (int i = 1; i <= 32; ++i) {
        uint32_t mask = (i == 0) ? 0 : (0xFFFFFFFF << (32 - i));
        long long total = std::pow(2, 32 - i);
        long long usable = std::max(0LL, total - 2);

        QTableWidgetItem *pItem = new QTableWidgetItem("/" + QString::number(i));
        pItem->setForeground(QColor("#4f7fff"));
        pItem->setFont(QFont("monospace", 11));
        
        QTableWidgetItem *hItem = new QTableWidgetItem(QString::number(usable));
        hItem->setForeground(QColor("#2dd98f"));
        
        table->setItem(i-1, 0, pItem);
        table->setItem(i-1, 1, new QTableWidgetItem(QHostAddress(mask).toString()));
        table->setItem(i-1, 2, hItem);
        table->setItem(i-1, 3, new QTableWidgetItem(QString::number(total)));
    }
    
    layout->addWidget(table);
}

void IPCalculatorPage::onCalculateForensic() {
    m_lastResult = core::IPCalculator::calculate(m_forensicIp->text().trimmed(), m_forensicPrefix->value());
    
    QLayoutItem *child;
    while ((child = m_cardLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    if (!m_lastResult.isValid) {
        m_bitwiseCard->setVisible(false);
        m_breakdownCard->setVisible(false);
        return;
    }

    auto addCard = [&](int row, int col, QString title, QString value, QString valClass) {
        QWidget *card = new QWidget();
        card->setObjectName("MetricCard");
        QVBoxLayout *l = new QVBoxLayout(card);
        l->setContentsMargins(14, 12, 14, 12);
        
        QLabel *titleL = new QLabel(title.toUpper());
        titleL->setObjectName("MetricLabel");
        
        QLabel *valL = new QLabel(value);
        valL->setObjectName(valClass);
        
        l->addWidget(titleL);
        l->addWidget(valL);
        m_cardLayout->addWidget(card, row, col);
    };

    addCard(0, 0, "Network Address", m_lastResult.networkAddress, "MetricNetwork");
    addCard(0, 1, "Usable Hosts", QString::number(m_lastResult.usableHosts), "MetricHosts");
    addCard(0, 2, "Broadcast", m_lastResult.broadcastAddress, "MetricValue");

    Theme::fadeIn(m_bitwiseCard);
    Theme::fadeIn(m_breakdownCard);
    
    m_bitwiseCard->setVisible(true);
    m_breakdownCard->setVisible(true);

    QHostAddress addr(m_forensicIp->text().trimmed());
    updateBitVisualizer(addr.toIPv4Address(), m_forensicPrefix->value());

    auto setRow = [&](int row, QString key, QString val) {
        QTableWidgetItem *kItem = new QTableWidgetItem(key);
        kItem->setForeground(QColor("#7c8299"));
        QTableWidgetItem *vItem = new QTableWidgetItem(val);
        vItem->setFont(QFont("monospace", 11));
        vItem->setForeground(QColor("#e8eaf0"));
        m_forensicResults->setItem(row, 0, kItem);
        m_forensicResults->setItem(row, 1, vItem);
    };
    
    setRow(0, "CIDR Notation", m_lastResult.cidrNotation);
    setRow(1, "Subnet Mask", m_lastResult.subnetMask);
    setRow(2, "Hexadecimal", m_lastResult.hexRepresentation);
    setRow(3, "Binary", m_lastResult.binaryRepresentation);
    setRow(4, "IP Class", m_lastResult.ipClass);

    if (m_lastResult.usableHosts > 0 && m_lastResult.usableHosts <= 65536) {
        m_generateBtn->setEnabled(true);
        m_generateBtn->setText(QString("Generate Audit List for %1 Hosts").arg(m_lastResult.usableHosts));
    } else {
        m_generateBtn->setEnabled(false);
        m_generateBtn->setText("Range too large for automated audit (Max /16)");
    }
}

void IPCalculatorPage::updateBitVisualizer(uint32_t address, int prefix) {
    if (m_bitVisualizer->layout()) {
        QLayoutItem *item;
        while ((item = m_bitVisualizer->layout()->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    } else {
        QHBoxLayout *l = new QHBoxLayout(m_bitVisualizer);
        l->setSpacing(2);
        l->setContentsMargins(0, 0, 0, 0);
    }

    QHBoxLayout *l = static_cast<QHBoxLayout*>(m_bitVisualizer->layout());
    
    for (int i = 31; i >= 0; --i) {
        bool isNetwork = (31 - i) < prefix;
        bool isSet = (address >> i) & 1;
        
        QLabel *bit = new QLabel(isSet ? "1" : "0");
        bit->setAlignment(Qt::AlignCenter);
        bit->setFixedSize(18, 24);
        bit->setStyleSheet(QString("font-family: monospace; font-size: 10px; border-radius: 3px; border: none; %1")
                           .arg(isNetwork 
                                ? (isSet ? "background: rgba(79,127,255,0.3); color: #a0b8ff;" : "background: rgba(79,127,255,0.1); color: #4a5068;")
                                : (isSet ? "background: rgba(45,217,143,0.3); color: #7eefc4;" : "background: rgba(45,217,143,0.08); color: #4a5068;")));
        
        l->addWidget(bit);
        if (i % 8 == 0 && i != 0) {
            QLabel *sep = new QLabel(".");
            sep->setStyleSheet("color: #4a5068; font-weight: bold; padding: 0 2px; font-size: 14px; background: transparent; border: none;");
            l->addWidget(sep);
        }
    }
    l->addStretch();
}

void IPCalculatorPage::onCalculateSubnets() {
    auto subnets = core::IPCalculator::createSubnets(m_subnetIp->text().trimmed(), m_subnetPrefix->value(), m_subnetCount->value());
    m_subnetResults->setRowCount(0);
    
    for (int i = 0; i < subnets.size(); ++i) {
        QString s = subnets[i];
        m_subnetResults->insertRow(i);
        
        m_subnetResults->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        
        QString cidr = s;
        if (s.contains("(")) {
            cidr = s.split(" ")[0];
        }
        
        QTableWidgetItem *cidrItem = new QTableWidgetItem(cidr);
        auto res = core::IPCalculator::calculate(cidr.split("/")[0], cidr.split("/").length() > 1 ? cidr.split("/")[1].toInt() : 0);
        
        cidrItem->setForeground(QColor("#4f7fff"));
        cidrItem->setFont(QFont("monospace", 11));
        m_subnetResults->setItem(i, 1, cidrItem);
        
        if (res.isValid) {
            m_subnetResults->setItem(i, 2, new QTableWidgetItem(QString("%1 - %2").arg(res.firstHost).arg(res.lastHost)));
            QTableWidgetItem *hItem = new QTableWidgetItem(QString::number(res.usableHosts));
            hItem->setForeground(QColor("#2dd98f"));
            m_subnetResults->setItem(i, 3, hItem);
        } else {
            m_subnetResults->setItem(i, 2, new QTableWidgetItem(s));
            m_subnetResults->setItem(i, 3, new QTableWidgetItem("-"));
        }
    }
    Theme::fadeIn(m_subnetResults);
}

void IPCalculatorPage::onAggregateCIDR() {
    QStringList lines = m_aggInput->toPlainText().split("\n", Qt::SkipEmptyParts);
    QString result = core::IPCalculator::aggregate(lines);
    if (result.isEmpty()) {
        m_aggResultValue->setText("Invalid Input");
        m_aggResultValue->setStyleSheet("font-size: 22px; font-weight: 500; color: #f05252; font-family: monospace; background: transparent; border: none;");
        m_aggResultSubtitle->setText("Check your CIDR blocks");
    } else {
        m_aggResultValue->setText(result);
        m_aggResultValue->setStyleSheet("font-size: 22px; font-weight: 500; color: #2dd98f; font-family: monospace; background: transparent; border: none;");
        m_aggResultSubtitle->setText("Optimal Combined Route");
    }
    Theme::pulse(m_aggResultContainer);
}

void IPCalculatorPage::applyTheme() {
    setStyleSheet(
        "gui--IPCalculatorPage { background-color: #111318; }"
        "QWidget#Card { background: #181b22; border: 0.5px solid rgba(255,255,255,0.07); border-radius: 10px; padding: 14px 16px; }"
        "QWidget#MetricCard { background: #1e2230; border-radius: 8px; }"
        "QLabel { color: #e8eaf0; border: none; background: transparent; }"
        "QLabel#SectionLabel { font-size: 11px; font-weight: bold; color: #4a5068; letter-spacing: 0.05em; padding-bottom: 5px; text-transform: uppercase; }"
        "QLabel#MetricLabel { font-size: 11px; color: #4a5068; padding-bottom: 2px; text-transform: uppercase; }"
        "QLabel#MetricValue { font-size: 15px; font-weight: 500; font-family: monospace; color: #e8eaf0; }"
        "QLabel#MetricNetwork { font-size: 15px; font-weight: 500; font-family: monospace; color: #4f7fff; }"
        "QLabel#MetricHosts { font-size: 15px; font-weight: 500; font-family: monospace; color: #2dd98f; }"
        
        "QTabWidget::pane { border: none; border-top: 0.5px solid rgba(255,255,255,0.07); }"
        "QTabBar { border-bottom: 0.5px solid rgba(255,255,255,0.07); }"
        "QTabBar::tab { background: #1e2230; color: #4a5068; padding: 8px 16px; border-radius: 6px 6px 0 0; border: 0.5px solid transparent; border-bottom: none; font-size: 12px; }"
        "QTabBar::tab:hover { color: #7c8299; background: #1e2230; }"
        "QTabBar::tab:selected { color: #e8eaf0; background: #181b22; border: 0.5px solid rgba(255,255,255,0.12); border-bottom: none; margin-bottom: -0.5px; }"
        
        "QLineEdit, QTextEdit { background: #1e2230; border: 0.5px solid rgba(255,255,255,0.12); color: #e8eaf0; padding: 7px 10px; border-radius: 6px; }"
        "QLineEdit:focus, QTextEdit:focus { border-color: #4f7fff; }"
        
        "QSpinBox { background: #1e2230; border: 0.5px solid rgba(255,255,255,0.12); color: #e8eaf0; padding: 7px 10px; border-radius: 6px; }"
        "QSpinBox:focus { border-color: #4f7fff; }"
        "QSpinBox::up-button, QSpinBox::down-button { width: 0px; border: none; background: transparent; }" 
        
        "QPushButton { border-radius: 6px; font-size: 12px; font-weight: 500; border: none; padding: 7px 15px; outline: none; }"
        "QPushButton#Primary { background: #4f7fff; color: #fff; }"
        "QPushButton#Primary:hover { background: #6b90ff; }"
        "QPushButton#Ghost { background: #1e2230; color: #7c8299; border: 0.5px solid rgba(255,255,255,0.12); }"
        "QPushButton#Ghost:hover { color: #e8eaf0; }"
        "QPushButton#GreenAction { background: rgba(45,217,143,0.12); color: #2dd98f; border: 0.5px solid rgba(45,217,143,0.2); }"
        "QPushButton#GreenAction:hover { background: rgba(45,217,143,0.18); }"
        "QPushButton:disabled { background: #1e2230; color: #4a5068; border: 0.5px solid rgba(255,255,255,0.07); }" 
        "QPushButton#SpinBtn { background: transparent; color: #7c8299; border: none; font-size: 14px; font-weight: bold; padding: 4px 8px; }"
        "QPushButton#SpinBtn:hover { color: #e8eaf0; }"
        
        "QTableWidget { background: transparent; border: none; gridline-color: rgba(255,255,255,0.07); color: #e8eaf0; }"
        "QHeaderView::section { background: transparent; color: #4a5068; font-size: 11px; font-weight: 500; border: none; border-bottom: 0.5px solid rgba(255,255,255,0.07); padding: 0px 10px 8px; }"
        "QTableWidget::item { border-bottom: 0.5px solid rgba(255,255,255,0.07); padding: 5px; }"
        "QTableWidget::item:hover { background: #1e2230; }"
        "QTableWidget::item:selected { background: #1e2230; }"
        "QScrollBar:vertical { background: #111318; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #1e2230; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }"
    );
}

} // namespace gui
