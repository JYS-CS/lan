#pragma once

#include <QDialog>
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

class IPCalculatorDialog : public QDialog {
    Q_OBJECT

public:
    explicit IPCalculatorDialog(QWidget *parent = nullptr);
    virtual ~IPCalculatorDialog() = default;

private slots:
    void onCalculateForensic();
    void onCalculateSubnets();
    void onAggregateCIDR();

private:
    void setupUi();
    void setupForensicTab(QWidget *tab);
    void setupSubnetTab(QWidget *tab);
    void setupAggregationTab(QWidget *tab);
    void applyTheme();

    QTabWidget *m_tabs;

    // Forensic Tab widgets
    QLineEdit *m_forensicIp;
    QSpinBox  *m_forensicPrefix;
    QTableWidget *m_forensicResults;

    // Subnet Tab widgets
    QLineEdit *m_subnetIp;
    QSpinBox  *m_subnetPrefix;
    QSpinBox  *m_subnetCount;
    QTableWidget *m_subnetResults;

    // Aggregation Tab widgets
    QTextEdit *m_aggInput;
    QLabel    *m_aggResultLabel;
};

} // namespace gui
