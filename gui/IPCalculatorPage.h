#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include "../core/IPCalculator.h"

namespace gui {

class IPCalculatorPage : public QWidget {
    Q_OBJECT

public:
    explicit IPCalculatorPage(QWidget *parent = nullptr);
    virtual ~IPCalculatorPage() = default;

private slots:
    void onCalculateForensic();
    void onCalculateSubnets();
    void onAggregateCIDR();

private:
    void setupUi();
    void setupForensicTab(QWidget *tab);
    void setupSubnetTab(QWidget *tab);
    void setupAggregationTab(QWidget *tab);
    void setupReferenceTab(QWidget *tab);
    void updateBitVisualizer(uint32_t address, int prefix);
    void applyTheme();

    QTabWidget *m_tabs;

    // Forensic Tab widgets
    QLineEdit *m_forensicIp;
    QSpinBox  *m_forensicPrefix;
    QPushButton *m_importBtn;
    QPushButton *m_generateBtn;
    QTableWidget *m_forensicResults;
    QGridLayout  *m_cardLayout;
    QWidget      *m_bitVisualizer;
    QWidget      *m_bitwiseCard;
    QWidget      *m_breakdownCard;

    // Last calculation result for exports
    core::NetworkInfo m_lastResult;

    // Subnet Tab widgets
    QLineEdit *m_subnetIp;
    QSpinBox  *m_subnetPrefix;
    QSpinBox  *m_subnetCount;
    QTableWidget *m_subnetResults;

    // Aggregation Tab widgets
    QTextEdit *m_aggInput;
    QWidget   *m_aggResultContainer;
    QLabel    *m_aggResultValue;
    QLabel    *m_aggResultSubtitle;
};

} // namespace gui
