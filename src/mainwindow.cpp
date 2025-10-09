// mainwindow.cpp (ready-to-paste)
#include "mainwindow.h"
#include "capturethread.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QDebug>
#include <QCoreApplication>
#include <atomic>
#include <QMetaObject>
#include <QMetaType>
#include <algorithm>
#include <QStringList>
#include <QRegularExpression>

// make the GUI global visible to other translation units (not static)
std::atomic<int> palette_mode_global{0};
std::atomic<int> &MainWindow::palette_mode_atomic() { return palette_mode_global; }

static const QStringList kPaletteNames = {
    QString::fromUtf8("IRONBOW"),
    QString::fromUtf8("RAINBOW"),
    QString::fromUtf8("ARCTIC"),
    QString::fromUtf8("LAVA"),
    QString::fromUtf8("BLACKHOT"),
    QString::fromUtf8("WHITEHOT"),
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_label(new OverlayLabel(this)),
      m_capture(new CaptureThread(palette_mode_global, this)),
      m_overlayTimer(new QTimer(this)),
      m_menuVisible(false)
{
    setCentralWidget(m_label);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_label->setStyleSheet("background: black;");

    // menu initialization
    m_menuItems = QStringList{"Image", "Reticle", "Features", "Battery", "Settings", "Exit", "Power Off"};

    // submenu - clean strings
    m_subMenus["Image"]     = {"Brightness", "Contrast", "Filter", "Palette", "Back"};
    m_subMenus["Brightness"] = {"-  50  +", "Back"};
    m_subMenus["Contrast"] = {"-  50  +", "Back"};
    m_subMenus["Filter"] = {"Sharp", "Smooth", "Normal", "Back"};
    m_subMenus["Palette"] = {"IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT", "Back"};

    // other submenus
    m_subMenus["Reticle"] = {"Profiles", "Type", "Zeroing", "Red Dot", "On/Off", "Back"};
    m_subMenus["Profiles"] = {"<  2  >", "Back"};
    m_subMenus["Type"] = {"Crosshair", "7_62x51", "5_45x39", "7_63x45", "7_62x39", "Back"};
    m_subMenus["Zeroing"] = {"X", "Y", "Auto", "Back"};
    m_subMenus["X"] = {"-  0  +", "Back"};
    m_subMenus["Y"] = {"-  0  +", "Back"};
    m_subMenus["Red Dot"] = {"On", "Off", "Back"};
    m_subMenus["Features"] = {"Standby", "Auto BPR", "Snapshots", "Recording", "PIP", "Screenshare", "LRF", "Ballistics", "IMU/GPS", "Back"};
    m_subMenus["Standby"] = {"On", "Off", "Back"};
    m_subMenus["Auto BPR"] = {"On", "Off", "Back"};
    m_subMenus["Snapshots"] = {"View", "Delete All", "Back"};
    m_subMenus["View"] = {"<", "4/20", ">", "Back"};
    m_subMenus["Recording"] = {"On", "Off", "View", "Delete All", "Back"};
    m_subMenus["Recording View"] = {"<", "1/10", ">", "Back"};
    m_subMenus["PIP"] = {"On", "Off", "Back"};
    m_subMenus["Screenshare"] = {"On", "Off", "Back"};
    m_subMenus["LRF"] = {"On", "Off", "X", "Y", "Back"};
    m_subMenus["Ballistics"] = {"Bullet", "Drag Function", "Ballistic Coeff.", "Muzzle Velocity", "Zero Range", "Sight Height", "Back"};
    m_subMenus["IMU/GPS"] = {"Set IMU", "Reset IMU", "IMU Zeroing", "GPS", "Back"};
    m_subMenus["GPS"] = {"On", "Off", "Back"};
    m_subMenus["Battery"]   = {"Percentage %", "Line ||| ", "Life hh:mm", "Back"};
    m_subMenus["Settings"]  = {"1", "2", "3", "4", "Back"};

    // Create overlay menu widget (child of m_label so it floats above video)
    m_menuWidget = new QWidget(m_label);
    m_menuWidget->setGeometry(320, 5, 160, 230);
    m_menuWidget->setStyleSheet("background: rgba(0,0,0,160); border-radius: 4px;");

    // Vertical layout for menu
    QVBoxLayout *menuLayout = new QVBoxLayout(m_menuWidget);
    menuLayout->setContentsMargins(5, 5, 5, 5);

    for (const QString &item : m_menuItems) {
        QLabel *lbl = new QLabel(item, m_menuWidget);
        lbl->setStyleSheet("color: white; font: 14px 'Sans';");
        menuLayout->addWidget(lbl);
        m_menuLabels.append(lbl);
    }
    m_menuWidget->hide();

    // submenu widget
    m_subMenuWidget = new QWidget(m_label);
    m_subMenuWidget->setGeometry(480, 5, 160, 230); // right of main menu
    m_subMenuWidget->setStyleSheet("background: rgba(0,0,0,160); border-radius: 4px;");
    QVBoxLayout *subLayout = new QVBoxLayout(m_subMenuWidget);
    subLayout->setContentsMargins(5,5,5,5);
    m_subMenuWidget->hide();

    // Use queued connection for cross-thread signal delivery
    connect(m_capture, &CaptureThread::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_capture, &CaptureThread::paletteIndexChanged, this, &MainWindow::onPaletteChanged, Qt::QueuedConnection);

    // connect gpio events forwarded by CaptureThread
    connect(m_capture, &CaptureThread::gpioPressed, this, &MainWindow::onGpioPressed, Qt::QueuedConnection);

    qDebug() << "[MainWindow] m_capture pointer:" << m_capture;

    m_capture->start();

    // quick sanity: set palette to 0 initially (keeps UI consistent)
    palette_mode_global.store(0);
    m_currentPalette = (int)palette_mode_global.load();
    m_paletteIndex = m_currentPalette;

    // optional: timer to refresh overlay if you want blinking etc.
    m_overlayTimer->setInterval(1000/30);
    connect(m_overlayTimer, &QTimer::timeout, [this](){ /* no-op: frames drive repaint */ });
    m_overlayTimer->start();
}

MainWindow::~MainWindow()
{
    if (m_capture) {
        m_capture->stopAndWait();
    }
}

void MainWindow::onImuDataReady(float pitch, float roll, float yaw) {
    Q_UNUSED(pitch); Q_UNUSED(roll); Q_UNUSED(yaw);
    m_label->update();
}

void MainWindow::updateMenuHighlight()
{
    for (int i = 0; i < m_menuLabels.size(); ++i) {
        if (i == m_menuIndex) {
            m_menuLabels[i]->setStyleSheet(
                "background-color: rgba(235, 109, 25, 181);"
                "color: white;"
                "font: bold 14px 'Sans';"
                "padding: 4px;"
                "border-radius: 4px;");
        } else {
            m_menuLabels[i]->setStyleSheet(
                "background-color: transparent;"
                "color: white;"
                "font: 14px 'Sans';"
                "padding: 4px;");
        }
    }
}

void MainWindow::updateSubMenuHighlight()
{
    for (int i = 0; i < m_subMenuLabels.size(); ++i) {
        if (i == m_subMenuIndex) {
            m_subMenuLabels[i]->setStyleSheet(
                "background-color: rgba(235, 109, 25, 181);"
                "color: white; font: bold 14px 'Sans'; padding: 4px; border-radius: 4px;");
        } else {
            m_subMenuLabels[i]->setStyleSheet(
                "background-color: transparent;"
                "color: white; font: 14px 'Sans'; padding: 4px;");
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_capture) {
        m_capture->stopAndWait();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onFrameReady(const QImage &img)
{
    if (img.isNull() || img.width() == 0 || img.height() == 0) {
        qWarning() << "onFrameReady: got null/empty image";
        return;
    }

    static std::atomic<bool> busy{false};
    if (busy.exchange(true)) {
        // skip this frame
        return;
    }

    QImage display = img.copy();

    QPainter p;
    if (!p.begin(&display)) {
        qWarning() << "onFrameReady: QPainter::begin() failed";
        busy.store(false);
        return;
    }

    // Draw palette name (bottom right)
    p.setPen(Qt::white);
    p.setFont(QFont("Monospace", 12, QFont::Bold));
    int currentIdx = (int)palette_mode_global.load();
    QString name = CaptureThread::paletteName(currentIdx);
    p.drawText(550, 470, QString("Palette: %1 (%2)").arg(name).arg(currentIdx));

    p.end();

    m_label->setPixmap(QPixmap::fromImage(display).scaled(m_label->size(), Qt::KeepAspectRatio));

    busy.store(false);
}

void MainWindow::onPaletteChanged(int idx)
{
    m_currentPalette = idx;
    m_paletteIndex = m_currentPalette;
    qDebug() << "[MainWindow] onPaletteChanged idx =" << idx << "name:" << CaptureThread::paletteName(idx);
}

// sanitize item text coming from various code paths (strip whitespace, remove surrounding quotes)
static QString sanitizeMenuItem(const QString &raw)
{
    QString s = raw.trimmed();

    // Remove surrounding double quotes if present: "RAINBOW" -> RAINBOW
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.mid(1, s.size() - 2).trimmed();
    }

    // Also remove surrounding single quotes if present
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        s = s.mid(1, s.size() - 2).trimmed();
    }

    // collapse multiple spaces
    s = s.simplified();

    return s;
}

static int findPaletteIndexByName(const QString &name)
{
    QString s = sanitizeMenuItem(name);
    for (int i = 0; i < kPaletteNames.size(); ++i) {
        if (s.compare(kPaletteNames[i], Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!event) return;
    int k = event->key();
    if (k >= Qt::Key_0 && k <= Qt::Key_5) {
        int idx = k - Qt::Key_0;
        qDebug() << "[MainWindow] key pressed, palette idx:" << idx;
        palette_mode_global.store(idx);
        bool invoked = QMetaObject::invokeMethod(m_capture, "setPaletteIndex", Qt::QueuedConnection, Q_ARG(int, idx));
        qDebug() << "[MainWindow] invokeMethod(setPaletteIndex) returned" << invoked << "for idx" << idx;
        if (!invoked) qWarning() << "[MainWindow] invokeMethod failed — slot not registered";
    } else if (k == Qt::Key_Q || k == Qt::Key_Escape) {
        close();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::onGpioPressed(unsigned int offset, int value) {
    qDebug() << "[GPIO] offset:" << offset << "value:" << value;
    
    if (value != 0) return; // Only process button press

    const unsigned int PF0 = 0; // Up
    const unsigned int PF1 = 1; // Down
    const unsigned int PF6 = 6; // Enter

    if (!m_menuVisible) {
        if (offset == PF6) {
            qDebug() << "[GPIO] Opening menu";
            m_menuVisible = true;
            m_menuWidget->show();
            m_currentMenuWidget = m_menuWidget;
            m_menuIndex = 0;
            updateMenuHighlight();
        }
        return;
    }

    if (m_currentMenuWidget == m_menuWidget) {
        if (offset == PF0) {
            m_menuIndex = (m_menuIndex - 1 + m_menuItems.size()) % m_menuItems.size();
        } else if (offset == PF1) {
            m_menuIndex = (m_menuIndex + 1) % m_menuItems.size();
        } else if (offset == PF6) {
            QString selectedItem = m_menuLabels.at(m_menuIndex)->text();
            enterMenuItem(selectedItem);
        }
        updateMenuHighlight();
    } else if (m_currentMenuWidget == m_subMenuWidget) {
        if (offset == PF0) {
            m_subMenuIndex = (m_subMenuIndex - 1 + m_subMenuLabels.size()) % m_subMenuLabels.size();
        } else if (offset == PF1) {
            m_subMenuIndex = (m_subMenuIndex + 1) % m_subMenuLabels.size();
        } else if (offset == PF6) {
            QString selectedItem = m_subMenuLabels.at(m_subMenuIndex)->text();
            enterSubMenuItem(selectedItem);
        }
        updateSubMenuHighlight();
    }
}

void MainWindow::buildSubMenu(const QString &menuName)
{
    QLayout *oldLayout = m_subMenuWidget->layout();
    if (oldLayout) {
        QLayoutItem *child;
        while ((child = oldLayout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
    }
    m_subMenuLabels.clear();

    QVBoxLayout *subLayout = static_cast<QVBoxLayout*>(m_subMenuWidget->layout());
    if (!m_subMenus.contains(menuName)) return;

    for (const QString &subItem : m_subMenus[menuName]) {
        QLabel *lbl = new QLabel(subItem, m_subMenuWidget);
        lbl->setStyleSheet("color: white; font: 14px 'Sans';");
        subLayout->addWidget(lbl);
        m_subMenuLabels.append(lbl);
    }

    m_subMenuIndex = 0;
    updateSubMenuHighlight();

    m_subMenuWidget->show();
    m_inSubmenu = true;
    m_currentMenuWidget = m_subMenuWidget;
}

void MainWindow::enterMenuItem(const QString &item)
{
    QString san = sanitizeMenuItem(item);
    if (m_subMenus.contains(san)) {
        m_subMenuWidget->hide();
        m_menuHistory.push(san);
        buildSubMenu(san);
    } else if (san == "Exit") {
        m_menuWidget->hide();
        m_subMenuWidget->hide();
        m_menuVisible = false;
        m_menuHistory.clear();
    } else if (san == "Power Off") {
        QCoreApplication::quit();
    }
}

void MainWindow::enterSubMenuItem(const QString &item)
{
    QString san = sanitizeMenuItem(item);

    if (san == "Back") {
        m_subMenuWidget->hide();
        if(!m_menuHistory.isEmpty()){
            m_menuHistory.pop();
            if (m_menuHistory.isEmpty()) {
                m_menuWidget->show();
                m_currentMenuWidget = m_menuWidget;
                m_inSubmenu = false;
            } else {
                buildSubMenu(m_menuHistory.top());
            }
        } else {
            m_menuVisible = false;
            m_menuWidget->hide();
        }
        return;
    }

    if (m_subMenus.contains(san)) {
        m_subMenuWidget->hide();
        m_menuHistory.push(san);
        buildSubMenu(san);
        return;
    }

    int idx = findPaletteIndexByName(san);
    if (idx >= 0) {
        qDebug() << "[enterSubMenuItem] PALETTE selected:" << san << " idx=" << idx;
        palette_mode_global.store(idx);
        bool invoked = QMetaObject::invokeMethod(m_capture, "setPaletteIndex", Qt::QueuedConnection, Q_ARG(int, idx));
        qDebug() << "[MainWindow] invokeMethod(setPaletteIndex) returned" << invoked << "for idx" << idx;
        m_currentPalette = idx;
        m_paletteIndex = idx;
        m_subMenuWidget->hide();
        m_inSubmenu = false;
        return;
    }

    handleAction(san);
}

void MainWindow::handleAction(const QString &item) {
    QString san = sanitizeMenuItem(item);
    int pidx = findPaletteIndexByName(san);
    if (pidx >= 0) {
        qDebug() << "[MainWindow::handleAction] detected palette:" << san << "idx:" << pidx;
        palette_mode_global.store(pidx);
        bool invoked = QMetaObject::invokeMethod(m_capture, "setPaletteIndex",
                                                 Qt::QueuedConnection,
                                                 Q_ARG(int, pidx));
        qDebug() << "[MainWindow] invokeMethod(setPaletteIndex) returned" << invoked << "for idx" << pidx;
        return;
    }

    if (san.compare("On", Qt::CaseInsensitive) == 0) {
        qDebug() << "[MainWindow::handleAction] ON action";
        return;
    }
    if (san.compare("Off", Qt::CaseInsensitive) == 0) {
        qDebug() << "[MainWindow::handleAction] OFF action";
        return;
    }

    qDebug() << "[MainWindow::handleAction] no-op for:" << san;
}

void MainWindow::applySelectedPalette()
{
    if (m_subMenus.contains("Palette") && m_paletteIndex >= 0 && m_paletteIndex < m_subMenus["Palette"].size()-1) {
        int idx = m_paletteIndex;
        qDebug() << "[applySelectedPalette] idx:" << idx;
        palette_mode_global.store(idx);
        bool invoked = QMetaObject::invokeMethod(m_capture, "setPaletteIndex", Qt::QueuedConnection, Q_ARG(int, idx));
        qDebug() << "[applySelectedPalette] invokeMethod(setPaletteIndex) returned" << invoked << "for idx" << idx;
        if (!invoked) qWarning() << "[applySelectedPalette] invokeMethod failed";
        m_currentPalette = idx;
    }
}
