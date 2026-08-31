#include "SettingsPage.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>

namespace gui {

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    setStyleSheet(
        "gui--SettingsPage { background: #0a0d12; }"
        "QWidget { background: #0a0d12; }"
        "QFrame#SectionCard { background: #0f141b; border: 1px solid #1c232c; border-radius: 10px; }"
        "QScrollArea { background: #0a0d12; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.08); border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header
    auto *header = new QWidget(this);
    header->setObjectName("HeaderBar");
    header->setStyleSheet("QWidget#HeaderBar { background: transparent; border-bottom: 1px solid #1c232c; }");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(24, 20, 24, 20);
    auto *titleLabel = new QLabel("Settings", header);
    titleLabel->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    hl->addWidget(titleLabel);
    hl->addStretch();
    root->addWidget(header);

    // ── Scrollable content
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(24, 24, 24, 24);
    cv->setSpacing(20);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    AppSettings *cfg = AppSettings::instance();

    // ── Device Table section
    cv->addWidget(makeSection("DEVICE TABLE"));

    ToggleSwitch *swSparklines = nullptr;
    cv->addWidget(makeRow(
        "Bandwidth Sparklines",
        "Show animated spike charts in the upload and download columns instead of plain text values.",
        &swSparklines
    ));
    swSparklines->setChecked(cfg->showSparklines());
    connect(swSparklines, &ToggleSwitch::toggled, cfg, &AppSettings::setShowSparklines);

    ToggleSwitch *swUpload = nullptr;
    cv->addWidget(makeRow(
        "Show Upload Column",
        "Display the upload speed column in the device table. Disable to reduce visual clutter.",
        &swUpload
    ));
    swUpload->setChecked(cfg->showUploadColumn());
    connect(swUpload, &ToggleSwitch::toggled, cfg, &AppSettings::setShowUploadColumn);

    ToggleSwitch *swDownload = nullptr;
    cv->addWidget(makeRow(
        "Show Download Column",
        "Display the download speed column in the device table.",
        &swDownload
    ));
    swDownload->setChecked(cfg->showDownloadColumn());
    connect(swDownload, &ToggleSwitch::toggled, cfg, &AppSettings::setShowDownloadColumn);

    cv->addStretch();
}

QWidget* SettingsPage::makeSection(const QString &title) {
    auto *lbl = new QLabel(title, this);
    lbl->setStyleSheet(
        "font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold;"
        " color: #4d5666; letter-spacing: 0.15em; background: transparent;");
    return lbl;
}

QWidget* SettingsPage::makeRow(const QString &label, const QString &desc, ToggleSwitch **sw) {
    auto *card = new QFrame(this);
    card->setObjectName("SectionCard");
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(20, 18, 20, 18);
    row->setSpacing(16);

    // Text side
    auto *textCol = new QVBoxLayout();
    textCol->setSpacing(4);
    auto *lbl = new QLabel(label, card);
    lbl->setStyleSheet("color: #dbe4ee; font-size: 14px; font-weight: 600; font-family: 'Inter', sans-serif; background: transparent;");
    auto *descLbl = new QLabel(desc, card);
    descLbl->setWordWrap(true);
    descLbl->setStyleSheet("color: #7c8798; font-size: 12px; font-family: 'Inter', sans-serif; background: transparent;");
    textCol->addWidget(lbl);
    textCol->addWidget(descLbl);

    // Toggle side
    *sw = new ToggleSwitch(false, card);

    row->addLayout(textCol, 1);
    row->addWidget(*sw, 0, Qt::AlignVCenter);
    return card;
}

} // namespace gui
