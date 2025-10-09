#include "mainwindow.h"
#include "capturethread.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include <algorithm>

// global palette atomic (single definition used by capturethread as extern)
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
      m_zoomLabel(new QLabel(m_label)),
      m_capture(new CaptureThread(palette_mode_global, this)),
      m_overlayTimer(new QTimer(this))
{
    // central widget and basic style
    setCentralWidget(m_label);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_label->setStyleSheet("background: black;");

    // build menus (same items you had)
    m_menuItems = QStringList{"Image", "Reticle", "Features", "Battery", "Settings", "Exit", "Power Off"};
    m_subMenus["Image"]     = {"Brightness", "Contrast", "Filter", "Palette", "Back"};
    m_subMenus["Brightness"] = {"-  50  +", "Back"};
    m_subMenus["Contrast"] = {"-  50  +", "Back"};
    m_subMenus["Filter"] = {"Sharp", "Smooth", "Normal", "Back"};
    m_subMenus["Palette"] = {"IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT", "Back"};
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

    // zoom label (top-left)
    m_zoomLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_zoomLabel->setStyleSheet("color: white; background: rgba(0,0,0,128); padding: 4px; border-radius: 4px; font: bold 14px 'Sans';");
    m_zoomLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_zoomLabel->setGeometry(8, 8, 140, 28);
    m_zoomLabel->show();
    m_zoomLabel->setText(QString("Zoom: %1×").arg(m_currentZoom));
    m_zoomLabel->raise();

    // Connect capture thread signals
    connect(m_capture, &CaptureThread::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_capture, &CaptureThread::paletteIndexChanged, this, &MainWindow::onPaletteChanged, Qt::QueuedConnection);
    connect(m_capture, &CaptureThread::gpioPressed, this, &MainWindow::onGpioPressed, Qt::QueuedConnection);

    qDebug() << "[MainWindow] m_capture pointer:" << m_capture;
    m_capture->start();

    // sync palette
    palette_mode_global.store(0);
    m_currentPalette = (int)palette_mode_global.load();
    m_paletteIndex = m_currentPalette;

    // overlay timer (if you need periodic overlay updates)
    m_overlayTimer->setInterval(1000/30);
    connect(m_overlayTimer, &QTimer::timeout, [this](){});
    m_overlayTimer->start();
}

MainWindow::~MainWindow()
{
    if (m_capture) {
        m_capture->stopAndWait();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_capture) m_capture->stopAndWait();
    QMainWindow::closeEvent(event);
}

/*
 * applyZoomToImage:
 *  - center-crops the source image by the integer zoom factor (1,2,4...)
 *  - scales the cropped region to the display label size, preserving aspect ratio
 */
QImage MainWindow::applyZoomToImage(const QImage &src, int zoomLevel)
{
    if (src.isNull() || zoomLevel <= 1) return src;

    int W = src.width();
    int H = src.height();

    // compute crop size for integer zoom (e.g. 2x => half width/height)
    int cropW = std::max(1, W / zoomLevel);
    int cropH = std::max(1, H / zoomLevel);

    int cropX = (W - cropW) / 2;
    int cropY = (H - cropH) / 2;

    QImage cropped = src.copy(cropX, cropY, cropW, cropH);

    QSize target = m_label->size();
    if (target.width() <= 0 || target.height() <= 0) return cropped;

    return cropped.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation);
}

void MainWindow::onFrameReady(const QImage &img)
{
    if (img.isNull() || img.width() == 0 || img.height() == 0) {
        qWarning() << "onFrameReady: got null/empty image";
        return;
    }

    static std::atomic<bool> busy{false};
    if (busy.exchange(true)) { // if already busy, skip frame
        return;
    }

    // Apply zoom on the GUI side:
    QImage processed;
    if (m_currentZoom <= 1) {
        // simply scale whole frame to label
        processed = img.scaled(m_label->size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    } else {
        // center-crop to zoom and then scale to label
        processed = applyZoomToImage(img, m_currentZoom);
    }

    // draw overlays directly onto processed image
    QPainter p;
    if (!p.begin(&processed)) {
        qWarning() << "onFrameReady: QPainter::begin() failed";
        busy.store(false);
        return;
    }

    // palette name bottom-right (example placement)
    p.setPen(Qt::white);
    p.setFont(QFont("Monospace", 12, QFont::Bold));
    int currentIdx = (int)palette_mode_global.load();
    QString name = CaptureThread::paletteName(currentIdx);
    p.drawText(processed.width() - 220, processed.height() - 10, QString("Palette: %1 (%2)").arg(name).arg(currentIdx));

    p.end();

    // set pixmap onto overlay label
    m_label->setPixmap(QPixmap::fromImage(processed));

    // update zoom label text and raise it above image
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("Zoom: %1×").arg(m_currentZoom));
        m_zoomLabel->raise();
    }

    busy.store(false);
}

void MainWindow::onPaletteChanged(int idx)
{
    m_currentPalette = idx;
    m_paletteIndex = m_currentPalette;
    qDebug() << "[MainWindow] onPaletteChanged idx =" << idx << "name:" << CaptureThread::paletteName(idx);
}

QString MainWindow::sanitizeMenuItem(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.mid(1, s.size() - 2).trimmed();
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.mid(1, s.size() - 2).trimmed();
    s = s.simplified();
    return s;
}

int MainWindow::findPaletteIndexByName(const QString &name)
{
    QString s = sanitizeMenuItem(name);
    for (int i = 0; i < kPaletteNames.size(); ++i) {
        if (s.compare(kPaletteNames[i], Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!event) return;
    int k = event->key();

    // quick numeric palette selection
    if (k >= Qt::Key_0 && k <= Qt::Key_5) {
        int idx = k - Qt::Key_0;
        qDebug() << "[MainWindow] key pressed, palette idx:" << idx;
        palette_mode_global.store(idx);
        bool invoked = QMetaObject::invokeMethod(m_capture, "setPaletteIndex", Qt::QueuedConnection, Q_ARG(int, idx));
        qDebug() << "[MainWindow] invokeMethod(setPaletteIndex) returned" << invoked << "for idx" << idx;
    } else if (k == Qt::Key_Q || k == Qt::Key_Escape) {
        close();
    } else if (k == Qt::Key_Z) {
        // test keyboard zoom cycle: 1x->2x->4x->1x
        m_zoomIndex = (m_zoomIndex + 1) % m_zoomLevels.size();
        m_currentZoom = m_zoomLevels.at(m_zoomIndex);
        if (m_zoomLabel) m_zoomLabel->setText(QString("Zoom: %1×").arg(m_currentZoom));
        qDebug() << "[MainWindow] keyboard zoom cycle to" << m_currentZoom << "x";
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

/*
 * onGpioPressed:
 * - When menu is NOT visible, PF0 cycles zoom levels (1x->2x->4x->1x).
 * - When menu IS visible, PF0/PF1/PF6 behave as menu navigation as before.
 */
void MainWindow::onGpioPressed(unsigned int offset, int value)
{
    qDebug() << "[GPIO] offset:" << offset << "value:" << value;
    if (value != 0) return; // only on press

    const unsigned int PF0 = 0; // Up (or zoom when menu hidden)
    const unsigned int PF1 = 1; // Down
    const unsigned int PF6 = 6; // Enter

    // If menu not visible, PF0 cycles zoom
    if (!m_menuVisible) {
        if (offset == PF0) {
            m_zoomIndex = (m_zoomIndex + 1) % m_zoomLevels.size();
            m_currentZoom = m_zoomLevels.at(m_zoomIndex);
            if (m_zoomLabel) {
                m_zoomLabel->setText(QString("Zoom: %1×").arg(m_currentZoom));
                m_zoomLabel->raise();
            }
            qDebug() << "[GPIO] PF0 zoom cycled to" << m_currentZoom << "x";
            return;
        }
        if (offset == PF6) {
            // open menu
            qDebug() << "[GPIO] Opening menu";
            m_menuVisible = true;
            m_menuWidget->show();
            m_currentMenuWidget = m_menuWidget;
            m_menuIndex = 0;
            updateMenuHighlight();
            return;
        }
        // other buttons no-op when menu hidden
        return;
    }

    // Menu visible: use navigation semantics as before
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
            if (child->widget()) delete child->widget();
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

void MainWindow::onImuDataReady(float pitch, float roll, float yaw)
{
    Q_UNUSED(pitch); Q_UNUSED(roll); Q_UNUSED(yaw);
    m_label->update();
}
