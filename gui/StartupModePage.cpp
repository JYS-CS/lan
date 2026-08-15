#include "StartupModePage.h"
#include "../core/NetworkManager.h"
#include "Theme.h"
#include <QFrame>
#include <QFile>
#include <QProcess>
#include <QHostAddress>
#include <QButtonGroup>
#include <QPainter>
#include <QSvgRenderer>
#include <QRegularExpression>
#include <QGridLayout>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QProgressBar>
#include <QStyle>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEnterEvent>
#endif
#include <functional>

namespace gui {

namespace {

QLabel *makeIconLabel(QWidget *parent, const QString &resourcePath, int size, const QColor &color) {
    QLabel *lbl = new QLabel(parent);
    lbl->setPixmap(Theme::tintedSvgPixmap(resourcePath, size, color));
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setFixedHeight(size);
    return lbl;
}

class OptionCardButton : public QPushButton {
public:
    explicit OptionCardButton(QWidget *parent = nullptr) : QPushButton(parent) {}
    QSize sizeHint() const override {
        if (layout()) return layout()->sizeHint();
        return QPushButton::sizeHint();
    }
    QSize minimumSizeHint() const override {
        if (layout()) return layout()->minimumSize();
        return QPushButton::minimumSizeHint();
    }
};

void setupOptionCard(QPushButton *btn, const QString &title, const QString &desc) {
    btn->setText(""); // Clear standard button text

    QVBoxLayout *layout = new QVBoxLayout(btn);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(8);
    QHBoxLayout *badgeLayout = new QHBoxLayout();
    QLabel *tLabel = new QLabel(title, btn);
    tLabel->setObjectName("OptionCardTitle");
    tLabel->setAlignment(Qt::AlignCenter);
    tLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    badgeLayout->addStretch();
    badgeLayout->addWidget(tLabel);
    badgeLayout->addStretch();
    
    QLabel *dLabel = new QLabel(desc, btn);
    dLabel->setObjectName("OptionCardDesc");
    dLabel->setAlignment(Qt::AlignCenter);
    dLabel->setWordWrap(true);
    dLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    
    layout->addStretch();
    layout->addLayout(badgeLayout);
    layout->addWidget(dLabel);
    layout->addStretch();
}

const QColor kAccent   = QColor("#facc15");
const QColor kOrange   = gui::Theme::AccentOrange;
const QColor kNeutral  = gui::Theme::TextSecondary;

class HoverCardWrapper : public QWidget {
public:
    explicit HoverCardWrapper(QWidget *innerWidget, QWidget *parent = nullptr)
        : QWidget(parent), m_inner(innerWidget) {
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(innerWidget);

        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(180);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_anim, &QVariantAnimation::valueChanged, [this](const QVariant &value) {
            int m = value.toInt();
            layout()->setContentsMargins(m, m, m, m);
        });

        setCursor(Qt::PointingHandCursor);
        m_inner->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    void setOnClick(std::function<void()> callback) {
        m_onClick = callback;
    }

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override {
#else
    void enterEvent(QEvent *event) override {
#endif
        m_anim->stop();
        m_anim->setStartValue(layout()->contentsMargins().left());
        m_anim->setEndValue(6);
        m_anim->start();
        m_inner->setProperty("hovered", true);
        m_inner->style()->unpolish(m_inner);
        m_inner->style()->polish(m_inner);
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        m_anim->stop();
        m_anim->setStartValue(layout()->contentsMargins().left());
        m_anim->setEndValue(0);
        m_anim->start();
        m_inner->setProperty("hovered", false);
        m_inner->style()->unpolish(m_inner);
        m_inner->style()->polish(m_inner);
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_anim->stop();
            m_anim->setStartValue(layout()->contentsMargins().left());
            m_anim->setEndValue(12);
            m_anim->start();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_anim->stop();
            m_anim->setStartValue(layout()->contentsMargins().left());
            if (rect().contains(event->pos())) {
                m_anim->setEndValue(6);
                if (m_onClick) m_onClick();
            } else {
                m_anim->setEndValue(0);
            }
            m_anim->start();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QWidget *m_inner;
    QVariantAnimation *m_anim;
    std::function<void()> m_onClick;
};

class AnimatedCheckbox : public QWidget {
public:
    explicit AnimatedCheckbox(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(40);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(300);
        m_anim->setEasingCurve(QEasingCurve::OutBack);
        QObject::connect(m_anim, &QVariantAnimation::valueChanged, [this](const QVariant &value) {
            m_progress = value.toReal();
            update();
        });
    }

    bool isChecked() const { return m_checked; }

    void setOnToggled(std::function<void(bool)> callback) {
        m_onToggled = callback;
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            m_checked = !m_checked;
            m_anim->stop();
            m_anim->setStartValue(m_progress);
            m_anim->setEndValue(m_checked ? 1.0 : 0.0);
            m_anim->start();
            if (m_onToggled) m_onToggled(m_checked);
        }
        QWidget::mouseReleaseEvent(event);
    }
    
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        QRectF box(0, (height() - 22) / 2.0, 22, 22);
        
        QColor borderColor(255, 255, 255, 40);
        QColor bgColor(0, 0, 0, 50);
        
        if (m_progress > 0) {
            QColor accent = kOrange; 
            borderColor = QColor(
                borderColor.red()   + (accent.red()   - borderColor.red())   * m_progress,
                borderColor.green() + (accent.green() - borderColor.green()) * m_progress,
                borderColor.blue()  + (accent.blue()  - borderColor.blue())  * m_progress,
                borderColor.alpha() + (255 - borderColor.alpha()) * m_progress
            );
            bgColor = QColor(
                bgColor.red()   + (accent.red()   - bgColor.red())   * m_progress,
                bgColor.green() + (accent.green() - bgColor.green()) * m_progress,
                bgColor.blue()  + (accent.blue()  - bgColor.blue())  * m_progress,
                bgColor.alpha() + (accent.alpha()*0.2 - bgColor.alpha()) * m_progress
            );
        }
        
        p.setPen(QPen(borderColor, 1.5));
        p.setBrush(bgColor);
        p.drawRoundedRect(box, 6, 6);
        
        if (m_progress > 0) {
            QPainterPath path;
            path.moveTo(box.left() + 5.5, box.top() + 11);
            path.lineTo(box.left() + 9.5, box.top() + 15);
            path.lineTo(box.left() + 16.5, box.top() + 7);
            
            p.save();
            p.translate(box.center());
            p.scale(m_progress, m_progress);
            p.setOpacity(m_progress);
            p.translate(-box.center());
            p.setPen(QPen(kOrange, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
            p.restore();
        }
        
        p.setPen(QColor(232, 234, 240));
        QFont f = font();
        f.setPointSize(10);
        p.setFont(f);
        QRectF textRect(34, 0, width() - 34, height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    QString m_text;
    bool m_checked = false;
    qreal m_progress = 0.0;
    QVariantAnimation *m_anim;
    std::function<void(bool)> m_onToggled;
};

} // namespace

StartupModePage::StartupModePage(core::NetworkManager *networkManager, QWidget *parent)
    : QWidget(parent), m_networkManager(networkManager)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    buildUi();
    applyTheme();
}

void StartupModePage::buildUi() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(36, 56, 36, 36);
    root->setSpacing(18);
    // No alignment constraint — let the layout fill the full widget area
    // so the page expands properly inside the QStackedWidget.

    m_stepLabel = new QLabel("STEP 1", this);
    m_stepLabel->setObjectName("StepLabel");
    m_stepLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_stepLabel);

    m_steps = new QStackedWidget(this);
    m_steps->setObjectName("StepStack");
    m_steps->setMaximumWidth(820);
    m_steps->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_steps->addWidget(buildStepMode());            // 0
    m_steps->addWidget(buildStepRouterWarning());    // 1
    m_steps->addWidget(buildStepDetectNetwork());    // 2
    m_steps->addWidget(buildStepAuthoritative());    // 3
    m_steps->addWidget(buildStepGateway());          // 4
    m_steps->addWidget(buildStepReview());           // 5

    // Horizontally center the step widget by flanking with stretches.
    QHBoxLayout *centerRow = new QHBoxLayout();
    centerRow->addStretch(1);
    centerRow->addWidget(m_steps, 4); // give it a strong stretch factor
    centerRow->addStretch(1);
    root->addLayout(centerRow, 1); // expand vertically to fill available height
}

// ── Step 1: choose a mode ────────────────────────────────────────────────────
QWidget *StartupModePage::buildStepMode() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(24);

    QLabel *title = new QLabel("Select Operating Mode", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Choose how this machine participates in the network.\n"
        "You can change this later from the DHCP page.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QHBoxLayout *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(18);

    // Normal card
    QWidget *normalCard = new QWidget();
    normalCard->setObjectName("ModeCard");
    QVBoxLayout *nl = new QVBoxLayout(normalCard);
    nl->setContentsMargins(24, 24, 24, 24);
    nl->setSpacing(12);
    nl->addWidget(makeIconLabel(normalCard, ":/resources/monitor.svg", 40, kNeutral));
    QLabel *nTitle = new QLabel("Normal Mode", normalCard);
    nTitle->setObjectName("ModeCardTitle");
    nTitle->setAlignment(Qt::AlignCenter);
    QLabel *nDesc = new QLabel(
        "Passive monitoring only. Your router keeps handling IP assignment.\n\n"
        "Per-device traffic details are not available in this mode.", normalCard);
    nDesc->setObjectName("ModeCardDesc");
    nDesc->setAlignment(Qt::AlignCenter);
    nDesc->setWordWrap(true);
    m_normalBtn = new QPushButton("Continue in Normal Mode", normalCard);
    m_normalBtn->setObjectName("GhostBtn");
    m_normalBtn->setCursor(Qt::PointingHandCursor);
    nl->addWidget(nTitle);
    nl->addWidget(nDesc);
    nl->addStretch();
    nl->addWidget(m_normalBtn);

    HoverCardWrapper *normalWrapper = new HoverCardWrapper(normalCard, page);
    normalWrapper->setOnClick([this]() { onNormalChosen(); });

    // DHCP card
    QWidget *dhcpCard = new QWidget();
    dhcpCard->setObjectName("ModeCardOrange");
    QVBoxLayout *dl = new QVBoxLayout(dhcpCard);
    dl->setContentsMargins(24, 24, 24, 24);
    dl->setSpacing(12);
    dl->addWidget(makeIconLabel(dhcpCard, ":/resources/subnet.svg", 40, kOrange));
    QLabel *dTitle = new QLabel("DHCP Server Mode", dhcpCard);
    dTitle->setObjectName("ModeCardTitle");
    dTitle->setAlignment(Qt::AlignCenter);
    QLabel *dDesc = new QLabel(
        "This machine becomes the DHCP authority, with full lease control.\n\n"
        "In Intercept mode, all device traffic routes through it and is fully visible.", dhcpCard);
    dDesc->setObjectName("ModeCardDesc");
    dDesc->setAlignment(Qt::AlignCenter);
    dDesc->setWordWrap(true);
    m_dhcpBtn = new QPushButton("Continue to DHCP Setup", dhcpCard);
    m_dhcpBtn->setObjectName("PrimaryBtnOrange");
    m_dhcpBtn->setCursor(Qt::PointingHandCursor);
    dl->addWidget(dTitle);
    dl->addWidget(dDesc);
    dl->addStretch();
    dl->addWidget(m_dhcpBtn);

    HoverCardWrapper *dhcpWrapper = new HoverCardWrapper(dhcpCard, page);
    dhcpWrapper->setOnClick([this]() { onDhcpChosen(); });

    cardsRow->addWidget(dhcpWrapper, 1);
    cardsRow->addWidget(normalWrapper, 1);
    pageLayout->addLayout(cardsRow);

    connect(m_normalBtn, &QPushButton::clicked, this, &StartupModePage::onNormalChosen);
    connect(m_dhcpBtn,   &QPushButton::clicked, this, &StartupModePage::onDhcpChosen);

    return page;
}

// ── Step 2: confirm router's DHCP is disabled ────────────────────────────────
QWidget *StartupModePage::buildStepRouterWarning() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/router.svg", 36, kOrange));

    QLabel *warnTitle = new QLabel("Confirm DHCP Server Setup", page);
    warnTitle->setObjectName("PageTitle");
    warnTitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(warnTitle);

    QLabel *warnSubtitle = new QLabel(
        "Before this machine starts handing out leases, make sure your\n"
        "router's built-in DHCP server is turned off.", page);
    warnSubtitle->setObjectName("PageSubtitle");
    warnSubtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(warnSubtitle);

    m_warnLabel = new QLabel(
        "Important: If your router's DHCP server is still enabled, "
        "devices on the network may receive conflicting IP configurations. "
        "Disable it in your router's admin settings before continuing.", page);
    m_warnLabel->setObjectName("WarnLabel");
    m_warnLabel->setWordWrap(true);
    m_warnLabel->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(m_warnLabel);

    pageLayout->addStretch();

    QHBoxLayout *cbLayout = new QHBoxLayout();
    cbLayout->addStretch();
    AnimatedCheckbox *confirmCb = new AnimatedCheckbox("I have disabled my router's DHCP", page);
    confirmCb->setMinimumWidth(260);
    cbLayout->addWidget(confirmCb);
    cbLayout->addStretch();
    pageLayout->addLayout(cbLayout);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_warnBackBtn = new QPushButton("Back", page);
    m_warnBackBtn->setObjectName("GhostBtn");
    m_warnBackBtn->setCursor(Qt::PointingHandCursor);
    m_warnContinueBtn = new QPushButton("Continue", page);
    m_warnContinueBtn->setObjectName("PrimaryBtnOrange");
    m_warnContinueBtn->setCursor(Qt::PointingHandCursor);
    m_warnContinueBtn->setEnabled(false);
    
    confirmCb->setOnToggled([this](bool checked) {
        m_warnContinueBtn->setEnabled(checked);
    });

    actions->addWidget(m_warnBackBtn);
    actions->addStretch();
    actions->addWidget(m_warnContinueBtn);
    pageLayout->addLayout(actions);

    connect(m_warnBackBtn,     &QPushButton::clicked, this, [this]() { goToStep(0); });
    connect(m_warnContinueBtn, &QPushButton::clicked, this, [this]() { goToStep(2); });

    return page;
}

// ── Step 3: auto-detect this machine's network configuration ────────────────
QWidget *StartupModePage::buildStepDetectNetwork() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/search.svg", 36, kAccent));

    QLabel *title = new QLabel("Detecting Network Configuration", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "We'll scan this machine's active interface to prefill sensible\n"
        "defaults for the DHCP pool.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QWidget *resultBox = new QWidget(page);
    resultBox->setObjectName("ResultBox");
    QGridLayout *grid = new QGridLayout(resultBox);
    grid->setContentsMargins(20, 18, 20, 18);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(10);

    auto addRow = [&](int row, const QString &key, QLabel *&valueLabel) {
        QLabel *keyLabel = new QLabel(key, resultBox);
        keyLabel->setObjectName("ResultKey");
        valueLabel = new QLabel("Detecting…", resultBox);
        valueLabel->setObjectName("ResultValue");
        grid->addWidget(keyLabel, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };
    addRow(0, "Interface", m_detIfaceValue);
    addRow(1, "IP Address", m_detIpValue);
    addRow(2, "Subnet Mask", m_detMaskValue);
    addRow(3, "Gateway", m_detGatewayValue);
    pageLayout->addWidget(resultBox);

    m_detStatusLabel = new QLabel("Detecting…", page);
    m_detStatusLabel->setObjectName("PageSubtitle");
    m_detStatusLabel->setAlignment(Qt::AlignCenter);
    m_detStatusLabel->setWordWrap(true);
    pageLayout->addWidget(m_detStatusLabel);

    m_detProgress = new QProgressBar(page);
    m_detProgress->setRange(0, 0); // indeterminate — animates automatically while visible
    m_detProgress->setTextVisible(false);
    m_detProgress->setFixedHeight(6);
    pageLayout->addWidget(m_detProgress);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_detBackBtn = new QPushButton("Back", page);
    m_detBackBtn->setObjectName("GhostBtn");
    m_detBackBtn->setCursor(Qt::PointingHandCursor);
    m_detRetryBtn = new QPushButton("Re-detect", page);
    m_detRetryBtn->setObjectName("GhostBtn");
    m_detRetryBtn->setCursor(Qt::PointingHandCursor);
    m_detContinueBtn = new QPushButton("Continue", page);
    m_detContinueBtn->setObjectName("PrimaryBtn");
    m_detContinueBtn->setCursor(Qt::PointingHandCursor);
    m_detContinueBtn->setEnabled(false);
    actions->addWidget(m_detBackBtn);
    actions->addWidget(m_detRetryBtn);
    actions->addStretch();
    actions->addWidget(m_detContinueBtn);
    pageLayout->addLayout(actions);

    connect(m_detBackBtn, &QPushButton::clicked, this, [this]() { goToStep(1); });
    connect(m_detContinueBtn, &QPushButton::clicked, this, [this]() { goToStep(3); });
    connect(m_detRetryBtn,    &QPushButton::clicked, this, &StartupModePage::runNetworkDetection);

    return page;
}

// ── Step 4: authoritative mode ───────────────────────────────────────────────
QWidget *StartupModePage::buildStepAuthoritative() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/settings.svg", 36, kAccent));

    QLabel *title = new QLabel("Authoritative Mode", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Decide how this server behaves when a client already holds a lease\n"
        "from a different, unrecognized DHCP server.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(18);

    m_authYesBtn = new OptionCardButton(page);
    setupOptionCard(m_authYesBtn, 
        "Authoritative (Recommended)", 
        "Actively corrects clients holding a lease from another DHCP server, keeping the network consistent.");
    m_authYesBtn->setObjectName("OptionCard");
    m_authYesBtn->setCheckable(true);
    m_authYesBtn->setChecked(true);
    m_authYesBtn->setCursor(Qt::PointingHandCursor);

    m_authNoBtn = new OptionCardButton(page);
    setupOptionCard(m_authNoBtn, 
        "Non-authoritative", 
        "Only answers new requests and never contests leases handed out by another DHCP server.");
    m_authNoBtn->setObjectName("OptionCard");
    m_authNoBtn->setCheckable(true);
    m_authNoBtn->setCursor(Qt::PointingHandCursor);

    QButtonGroup *group = new QButtonGroup(page);
    group->setExclusive(true);
    group->addButton(m_authYesBtn);
    group->addButton(m_authNoBtn);

    HoverCardWrapper *authYesWrapper = new HoverCardWrapper(m_authYesBtn, page);
    authYesWrapper->setOnClick([this]() { m_authYesBtn->setChecked(true); });
    
    HoverCardWrapper *authNoWrapper = new HoverCardWrapper(m_authNoBtn, page);
    authNoWrapper->setOnClick([this]() { m_authNoBtn->setChecked(true); });

    optionsRow->addWidget(authYesWrapper, 1);
    optionsRow->addWidget(authNoWrapper, 1);
    pageLayout->addLayout(optionsRow);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_authBackBtn = new QPushButton("Back", page);
    m_authBackBtn->setObjectName("GhostBtn");
    m_authBackBtn->setCursor(Qt::PointingHandCursor);
    m_authNextBtn = new QPushButton("Continue", page);
    m_authNextBtn->setObjectName("PrimaryBtn");
    m_authNextBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_authBackBtn);
    actions->addStretch();
    actions->addWidget(m_authNextBtn);
    pageLayout->addLayout(actions);

    connect(m_authBackBtn, &QPushButton::clicked, this, [this]() { goToStep(2); });
    connect(m_authNextBtn, &QPushButton::clicked, this, [this]() {
        m_settings.authoritative = m_authYesBtn->isChecked();
        goToStep(4);
    });

    return page;
}

// ── Step 5: gateway / intercept mode ─────────────────────────────────────────
QWidget *StartupModePage::buildStepGateway() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/merger.svg", 36, kAccent));

    QLabel *title = new QLabel("Act as Gateway", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Decide whether client traffic should route through this machine\n"
        "on its way to the real router.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(18);

    m_gwTransparentBtn = new OptionCardButton(page);
    setupOptionCard(m_gwTransparentBtn,
        "Transparent (Recommended)",
        "The real router stays the gateway. Less invasive, with limited per-device traffic visibility.");
    m_gwTransparentBtn->setObjectName("OptionCard");
    m_gwTransparentBtn->setCheckable(true);
    m_gwTransparentBtn->setCursor(Qt::PointingHandCursor);

    m_gwInterceptBtn = new OptionCardButton(page);
    setupOptionCard(m_gwInterceptBtn,
        "Intercept",
        "This machine becomes the gateway via NAT. All client traffic is fully visible.");
    m_gwInterceptBtn->setObjectName("OptionCard");
    m_gwInterceptBtn->setCheckable(true);
    m_gwInterceptBtn->setChecked(true);
    m_gwInterceptBtn->setCursor(Qt::PointingHandCursor);

    QButtonGroup *group = new QButtonGroup(page);
    group->setExclusive(true);
    group->addButton(m_gwTransparentBtn);
    group->addButton(m_gwInterceptBtn);

    HoverCardWrapper *gwTransWrapper = new HoverCardWrapper(m_gwTransparentBtn, page);
    gwTransWrapper->setOnClick([this]() { m_gwTransparentBtn->setChecked(true); });
    
    HoverCardWrapper *gwIntWrapper = new HoverCardWrapper(m_gwInterceptBtn, page);
    gwIntWrapper->setOnClick([this]() { m_gwInterceptBtn->setChecked(true); });

    optionsRow->addWidget(gwIntWrapper, 1);
    optionsRow->addWidget(gwTransWrapper, 1);
    pageLayout->addLayout(optionsRow);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_gwBackBtn = new QPushButton("Back", page);
    m_gwBackBtn->setObjectName("GhostBtn");
    m_gwBackBtn->setCursor(Qt::PointingHandCursor);
    m_gwNextBtn = new QPushButton("Continue", page);
    m_gwNextBtn->setObjectName("PrimaryBtn");
    m_gwNextBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_gwBackBtn);
    actions->addStretch();
    actions->addWidget(m_gwNextBtn);
    pageLayout->addLayout(actions);

    connect(m_gwBackBtn, &QPushButton::clicked, this, [this]() { goToStep(3); });
    connect(m_gwNextBtn, &QPushButton::clicked, this, [this]() {
        m_settings.intercept = m_gwInterceptBtn->isChecked();
        refreshReviewSummary();
        goToStep(5);
    });

    return page;
}

// ── Step 6: review + final settings ──────────────────────────────────────────
QWidget *StartupModePage::buildStepReview() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(18);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/list.svg", 36, kAccent));

    QLabel *title = new QLabel("Review DHCP Settings", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Confirm the lease pool and DNS servers, then start the server.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    m_reviewSummary = new QLabel(page);
    m_reviewSummary->setObjectName("ResultValue");
    m_reviewSummary->setAlignment(Qt::AlignCenter);
    m_reviewSummary->setWordWrap(true);
    pageLayout->addWidget(m_reviewSummary);

    QWidget *fieldsBox = new QWidget(page);
    fieldsBox->setObjectName("ResultBox");
    QGridLayout *grid = new QGridLayout(fieldsBox);
    grid->setContentsMargins(20, 18, 20, 18);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);

    auto addField = [&](int row, int col, const QString &label, QLineEdit *&edit) {
        QLabel *l = new QLabel(label, fieldsBox);
        l->setObjectName("ResultKey");
        edit = new QLineEdit(fieldsBox);
        QVBoxLayout *cell = new QVBoxLayout();
        cell->setSpacing(4);
        cell->addWidget(l);
        cell->addWidget(edit);
        grid->addLayout(cell, row, col);
    };
    addField(0, 0, "Range start", m_rangeStartEdit);
    addField(0, 1, "Range end", m_rangeEndEdit);
    addField(1, 0, "Primary DNS", m_dns1Edit);
    addField(1, 1, "Secondary DNS", m_dns2Edit);
    addField(2, 0, "Lease time (hours)", m_leaseEdit);
    pageLayout->addWidget(fieldsBox);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_reviewBackBtn = new QPushButton("Back", page);
    m_reviewBackBtn->setObjectName("GhostBtn");
    m_reviewBackBtn->setCursor(Qt::PointingHandCursor);
    m_finishBtn = new QPushButton("Complete Setup & Start Server", page);
    m_finishBtn->setObjectName("PrimaryBtn");
    m_finishBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_reviewBackBtn);
    actions->addStretch();
    actions->addWidget(m_finishBtn);
    pageLayout->addLayout(actions);

    connect(m_reviewBackBtn, &QPushButton::clicked, this, [this]() { goToStep(4); });
    connect(m_finishBtn, &QPushButton::clicked, this, [this]() {
        m_settings.rangeStart      = m_rangeStartEdit->text().trimmed();
        m_settings.rangeEnd        = m_rangeEndEdit->text().trimmed();
        m_settings.dns1            = m_dns1Edit->text().trimmed();
        m_settings.dns2            = m_dns2Edit->text().trimmed();
        m_settings.leaseTimeSeconds= m_leaseEdit->text().trimmed().toInt() * 3600;
        if (m_settings.leaseTimeSeconds <= 0) m_settings.leaseTimeSeconds = 86400;
        emit dhcpWizardCompleted(m_settings);
    });

    return page;
}

void StartupModePage::refreshReviewSummary() {
    if (!m_reviewSummary) return;
    m_reviewSummary->setText(QString(
        "%1  ·  %2\nAuthoritative: %3   Gateway: %4")
        .arg(m_settings.interface.isEmpty() ? "—" : m_settings.interface)
        .arg(m_settings.hostIp.isEmpty() ? "—" : m_settings.hostIp)
        .arg(m_settings.authoritative ? "Yes" : "No")
        .arg(m_settings.intercept ? "Intercept" : "Transparent"));

    m_rangeStartEdit->setText(m_settings.rangeStart);
    m_rangeEndEdit->setText(m_settings.rangeEnd);
    m_dns1Edit->setText(m_settings.dns1);
    m_dns2Edit->setText(m_settings.dns2);
    m_leaseEdit->setText(QString::number(m_settings.leaseTimeSeconds / 3600));
}

// ── Network detection ────────────────────────────────────────────────────────
void StartupModePage::runNetworkDetection() {
    if (!m_networkManager) return;

    m_detStatusLabel->setText("Detecting…");
    m_detStatusLabel->setStyleSheet("");
    m_detContinueBtn->setEnabled(false);
    m_detProgress->setVisible(true);
    m_detIfaceValue->setText("Detecting…");
    m_detIpValue->setText("Detecting…");
    m_detMaskValue->setText("Detecting…");
    m_detGatewayValue->setText("Detecting…");

    QWidget *resultBox = findChild<QWidget*>("ResultBox");
    if (resultBox) {
        resultBox->setProperty("status", QVariant());
        resultBox->style()->unpolish(resultBox);
        resultBox->style()->polish(resultBox);
    }

    QString iface = m_networkManager->getActiveInterface();
    if (iface.isEmpty()) {
        m_detStatusLabel->setText("No active network interface detected. Check your connection and retry.");
        m_detProgress->setVisible(false);
        return;
    }

    QHostAddress ip   = m_networkManager->getInterfaceAddress(iface);
    QHostAddress mask = m_networkManager->getInterfaceNetmask(iface);
    if (ip.isNull() || mask.isNull()) {
        m_detStatusLabel->setText("Could not read the IP configuration for " + iface + ".");
        m_detProgress->setVisible(false);
        return;
    }

    QProcess proc;
    proc.start("sh", {"-c", QString("ip route show dev %1 | grep default | awk '{print $3}'").arg(iface)});
    proc.waitForFinished();
    QString gw = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();

    m_settings.interface  = iface;
    m_settings.hostIp     = ip.toString();
    m_settings.subnetMask = mask.toString();
    m_settings.gatewayIp  = gw;

    m_detIfaceValue->setText(iface);
    m_detIpValue->setText(ip.toString());
    m_detMaskValue->setText(mask.toString());
    m_detGatewayValue->setText(gw.isEmpty() ? "Not detected" : gw);

    // Suggested DHCP pool, spanning most of the subnet
    quint32 ipInt   = ip.toIPv4Address();
    quint32 maskInt = mask.toIPv4Address();
    quint32 network = ipInt & maskInt;
    quint32 bcast   = network | (~maskInt);
    quint32 poolStart = network + 100;
    quint32 poolEnd   = bcast - 10;
    if (poolStart <= ipInt) poolStart = ipInt + 1;
    if (poolEnd   <= ipInt) poolEnd   = ipInt + 254;
    m_settings.rangeStart = QHostAddress(poolStart).toString();
    m_settings.rangeEnd   = QHostAddress(poolEnd).toString();
    m_settings.dns1 = "1.1.1.1";
    m_settings.dns2 = "8.8.8.8";
    m_settings.leaseTimeSeconds = 86400;

    if (gw.isEmpty()) {
        m_detStatusLabel->setText("Gateway not detected automatically — you can set it manually in the final step.");
    } else {
        m_detStatusLabel->setText("Network detected successfully.");
        m_detStatusLabel->setStyleSheet("color: #4f7fff; font-weight: 600;");
    }
    m_detProgress->setVisible(false);
    m_detContinueBtn->setEnabled(true);

    if (resultBox) {
        resultBox->setProperty("status", "success");
        resultBox->style()->unpolish(resultBox);
        resultBox->style()->polish(resultBox);
    }
}

void StartupModePage::goToStep(int index) {
    m_steps->setCurrentIndex(index);
    m_stepLabel->setText(index == 0 ? "STEP 1" : QString("STEP %1 OF 6").arg(index + 1));

    QWidget *page = m_steps->currentWidget();
    if (page) {
        auto *effect = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity", page);
        anim->setDuration(260);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, effect, [page]() {
            page->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    if (index == 2) runNetworkDetection();
}

void StartupModePage::onNormalChosen() {
    emit modeSelected(Mode::Normal, false);
}

void StartupModePage::onDhcpChosen() {
    goToStep(1);
}

void StartupModePage::applyTheme() {
    setStyleSheet(
        "StartupModePage { background-color: #0d1117; }"
        "QLabel { color: #e8eaf0; border: none; background: transparent; }"
        "QLabel#StepLabel { font-size: 11px; font-weight: 600; letter-spacing: 0.12em; color: #facc15; }"
        "QLabel#PageTitle { font-size: 22px; font-weight: 700; color: #e8eaf0; }"
        "QLabel#PageSubtitle { font-size: 13px; color: #7c8299; margin-bottom: 4px; }"
        "QLabel#WarnLabel { font-size: 13px; color: #e8c07a; background: rgba(232,192,122,0.08); "
        "   border: 0.5px solid rgba(232,192,122,0.25); border-radius: 8px; padding: 14px 16px; }"
        "QWidget#ModeCard { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.08); border-radius: 12px; }"
        "QWidget#ModeCardOrange { background-color: #181b22; border: 0.5px solid rgba(255,145,66,0.25); border-radius: 12px; }"
        "QWidget#ModeCard[hovered=\"true\"] { background-color: #1e222b; border: 0.5px solid rgba(250,204,21,0.5); }"
        "QWidget#ModeCardOrange[hovered=\"true\"] { background-color: #241c18; border: 0.5px solid rgba(255,145,66,0.6); }"
        "QWidget#ModeCard[hovered=\"true\"] QPushButton#GhostBtn { background-color: #1e2230; color: #e8eaf0; border-color: rgba(255,255,255,0.25); }"
        "QWidget#ModeCardOrange[hovered=\"true\"] QPushButton#PrimaryBtnOrange { background-color: #f07f2e; }"
        "QLabel#ModeCardTitle { font-size: 16px; font-weight: 600; color: #e8eaf0; }"
        "QLabel#ModeCardDesc { font-size: 13px; color: #8a93b8; }"
        "QWidget#ResultBox { background-color: #131722; border: 1px solid rgba(255,255,255,0.07); border-radius: 10px; }"
        "QWidget#ResultBox[status=\"success\"] { background-color: rgba(250,204,21,0.05); border: 1px solid rgba(250,204,21,0.4); }"
        "QLabel#ResultKey { font-size: 11px; font-weight: 600; letter-spacing: 0.05em; color: #5a6175; }"
        "QLabel#ResultValue { font-family: 'SF Mono', Consolas, 'Fira Code', monospace; font-size: 14px; font-weight: 500; color: #e8eaf0; letter-spacing: 0.03em; }"
        "QLineEdit { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.1); border-radius: 6px; "
        "   padding: 8px 10px; color: #e8eaf0; font-size: 13px; }"
        "QLineEdit:focus { border: 0.5px solid #facc15; }"
        "QPushButton { border-radius: 6px; font-size: 13px; font-weight: 500; padding: 10px 18px; }"
        "QPushButton#PrimaryBtn { background-color: #facc15; color: #1a1206; border: none; font-weight: 600; }"
        "QPushButton#PrimaryBtn:hover { background-color: #eab308; }"
        "QPushButton#PrimaryBtn:disabled { background-color: rgba(250,204,21,0.15); color: rgba(250,204,21,0.4); }"
        "QPushButton#PrimaryBtnOrange { background-color: #ff9142; color: #1a1206; border: none; font-weight: 600; }"
        "QPushButton#PrimaryBtnOrange:hover { background-color: #f07f2e; }"
        "QPushButton#PrimaryBtnOrange:disabled { background-color: rgba(255,145,66,0.15); color: rgba(255,145,66,0.4); }"
        "QPushButton#GhostBtn { background-color: transparent; border: 1px solid rgba(250,204,21,0.3); color: #facc15; }"
        "QPushButton#GhostBtn:hover { background-color: rgba(250,204,21,0.1); color: #facc15; border: 1px solid #facc15; }"
        "QPushButton#GhostBtn:disabled { border: 1px solid rgba(250,204,21,0.1); color: rgba(250,204,21,0.3); }"
        "QPushButton#OptionCard { text-align: left; background-color: #181b22; color: #b5bad0; "
        "   border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 0px; font-size: 13px; font-weight: 500; }"
        "QPushButton#OptionCard:hover, QPushButton#OptionCard[hovered=\"true\"] { border: 1px solid rgba(250,204,21,0.4); }"
        "QPushButton#OptionCard:checked { background-color: rgba(250,204,21,0.08); border: 1px solid #facc15; }"
        "QLabel#OptionCardTitle { font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.05em; "
        "   color: #facc15; background: rgba(250,204,21,0.15); border-radius: 6px; padding: 4px 12px; }"
        "QLabel#OptionCardDesc { font-size: 13px; font-weight: 500; color: #dcdfe8; background: transparent; }"
        "QPushButton#OptionCard:checked QLabel#OptionCardTitle { background: #facc15; color: #1a1206; }"
        "QPushButton#OptionCard:checked QLabel#OptionCardDesc { color: #ffffff; }"
    );
}

} // namespace gui
