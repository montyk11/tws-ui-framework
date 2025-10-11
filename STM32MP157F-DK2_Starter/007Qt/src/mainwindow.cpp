#include "mainwindow.h"
#include "capturethread.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QDebug>
#include <QCoreApplication>
#include <atomic>
#include <linux/input.h>
#include <fcntl.h>
#include <QProcess>
#include <unistd.h>

static std::atomic<int> palette_mode_global{0};
std::atomic<int> &MainWindow::palette_mode_atomic() { return palette_mode_global; }

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    //   m_label(new QLabel(this)),
      m_label(new OverlayLabel(this)),
      m_capture(new CaptureThread(palette_mode_global, this)),
      m_overlayTimer(new QTimer(this)),
      m_menuVisible(false)


{
    setCentralWidget(m_label);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_label->setStyleSheet("background: black;");
    
    batteryIcon = new QLabel(m_label);
    QPixmap pix("res/battery.png");
    qDebug() << "Battery icon loaded?" << !pix.isNull();
    batteryIcon->setPixmap(QPixmap(":/res/battery.png").scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    batteryIcon->setAttribute(Qt::WA_TranslucentBackground);
    batteryIcon->setStyleSheet("background: transparent;");
    batteryIcon->setFixedSize(32, 32);
    batteryIcon->move(m_label->width() - 40, 10); // top-right corner
    batteryIcon->show();

    setupPowerButton(); // <--- add this

    // menu initialization
    m_menuItems = QStringList{"Image", "Reticle", "Features", "Battery", "Settings", "Exit", "Power Off"};
    // m_paletteItems = QStringList{"IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT"};

    // submenu
    // m_paletteItems = QStringList{"IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT"};
    m_subMenus["Image"]     = {"Brightness", "Contrast", "Filter", "Palette", "Back"};// m_subMenus["Palette"]   = m_paletteItems + QStringList{"Back"};  // nested submenu for palettes
    m_subMenus["Brightness"] = {"-  50  +", "Back"};
    m_subMenus["Contrast"] = {"-  50  +", "Back"};
    m_subMenus["Filter"] = {"Sharp", "Smooth", "Normal", "Back"};
    m_subMenus["Palette"] = {"IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT", "Back"};
    
    m_subMenus["Reticle"] = {"Profiles", "Type", "Zeroing", "Red Dot", "On/Off", "Back"};
    m_subMenus["Profiles"] = {"<  2  >", "Back"}; // Use string indicators for value changes
    m_subMenus["Type"] = {"Crosshair", "7_62x51", "5_45x39", "7_63x45", "7_62x39", "Back"};
    m_subMenus["Zeroing"] = {"X", "Y", "Auto", "Back"};
    m_subMenus["X"] = {"-  0  +", "Back"};
    m_subMenus["Y"] = {"-  0  +", "Back"};
    m_subMenus["Red Dot"] = {"On", "Off","Auto", "Back"};
    
    // Add more entries for the "Features" menu
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
    // m_menuWidget->setGeometry(320, 5, 160, 230);
    m_menuWidget->resize(160, 230);
    m_menuWidget->move((m_label->width() - 160) / 2, 5);

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

    // // submenu widget
    // m_subMenuWidget = new QWidget(m_label);
    // m_subMenuWidget->setGeometry(480, 5, 160, 230); // right of main menu
    // m_subMenuWidget->setStyleSheet("background: rgba(0,0,0,160); border-radius: 4px;");
    // QVBoxLayout *subLayout = new QVBoxLayout(m_subMenuWidget);
    // subLayout->setContentsMargins(5,5,5,5);
    // m_subMenuWidget->hide();
    // Initially create submenu with any geometry, width & height same as before
    m_subMenuWidget = new QWidget(m_label);
    m_subMenuWidget->setFixedSize(160, 230);
    m_subMenuWidget->setStyleSheet("background: rgba(0,0,0,160); border-radius: 4px;");
    QVBoxLayout *subLayout = new QVBoxLayout(m_subMenuWidget);
    subLayout->setContentsMargins(5,5,5,5);
    m_subMenuWidget->hide();

    // Position submenu dynamically relative to main menu
    auto updateSubMenuPos = [this]() {
    if (m_menuWidget && m_subMenuWidget) {
            QPoint menuPos = m_menuWidget->pos();
            m_subMenuWidget->move(menuPos.x() + m_menuWidget->width(), menuPos.y());
        }
    };

    
    // Use queued connection for cross-thread signal delivery
    connect(m_capture, &CaptureThread::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_capture, &CaptureThread::paletteIndexChanged, this, &MainWindow::onPaletteChanged, Qt::QueuedConnection);

    // connect gpio events forwarded by CaptureThread
    connect(m_capture, &CaptureThread::gpioPressed, this, &MainWindow::onGpioPressed, Qt::QueuedConnection);
    // Connect the signal from the CaptureThread to the slot in MainWindow
    connect(m_capture, &CaptureThread::imuDataReady, this, &MainWindow::onImuDataReady);


    m_capture->start();

    // sync with thread's palette at start
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

void MainWindow::updateSubMenuPos()
{
    if (m_menuWidget && m_subMenuWidget) {
        QPoint menuPos = m_menuWidget->pos();
        m_subMenuWidget->move(menuPos.x() + m_menuWidget->width(), menuPos.y());
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (batteryIcon && m_label){
        // batteryIcon->move(width() - batteryIcon->width() - 10, 10);
        batteryIcon->move(m_label->width() - batteryIcon->width() - 10, 10);
    }
    if (m_menuWidget && m_label) {
        // Keep same y (e.g., 5), same width, but center horizontally
        int newX = (m_label->width() - m_menuWidget->width()) / 2;
        int newY = 5; // keep same top margin

        m_menuWidget->move(newX, newY);
    }
    
    updateSubMenuPos();

}


void MainWindow::onImuDataReady(float pitch, float roll, float yaw) {
    m_pitch = 10.0f;//pitch;
    m_roll = 45.0f;//roll;
    m_yaw = -30.0f;//yaw;
    // Request a repaint to draw the new lines
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

    // Drop frame if previous frame is still being processed by GUI
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
    QString name = CaptureThread::paletteName((int)palette_mode_global.load());
    p.drawText(750, 395, name); 

    // Draw menu overlay only if visible
    // if (m_menuVisible) {
    //     drawMenuOverlay(p, display);
    // }

    // Draw menu overlay (if visible) using the *same* painter (no nested painters)
    // drawMenuOverlay(p, display);

    p.end();

    m_label->setPixmap(QPixmap::fromImage(display).scaled(m_label->size(), Qt::KeepAspectRatio));

    busy.store(false);
}

void MainWindow::onPaletteChanged(int idx)
{
    m_currentPalette = idx;
    // When capture thread changes palette (e.g. keyboard), reflect it in UI
    m_paletteIndex = m_currentPalette;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!event) return;
    int k = event->key();
    // number keys 0..5 -> direct palette select
    if (k >= Qt::Key_0 && k <= Qt::Key_5) {
        int idx = k - Qt::Key_0;
        palette_mode_global.store(idx);
        emit m_capture->paletteIndexChanged(idx);
    } else if (k == Qt::Key_Q || k == Qt::Key_Escape) {
        close();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

// Updated drawMenuOverlay: accepts an active QPainter& (no nested painters)
// void MainWindow::drawMenuOverlay(QPainter &p, QImage &image)
// {
//     Q_UNUSED(image);
//     p.setRenderHint(QPainter::Antialiasing, true);
//     // panel background
//     int panelW = 200;
//     int cellH = 28;
//     int x = 210;
//     int y = 10;
//     int rows = m_inSubmenu ? m_paletteItems.size() : m_menuItems.size();
//     // Draw a translucent dark background
//     p.setPen(Qt::NoPen);
//     p.setBrush(QColor(0,0,0,160));
//     p.drawRoundedRect(x-8, y-8, panelW, (rows+1)*cellH, 6, 6);
//     // Draw each row
//     for (int i=0; i<rows; ++i) {
//         int ry = y + i * cellH;
//         QString text;
//         bool highlighted = false;
//         if (m_inSubmenu) {
//             text = m_paletteItems.at(i);
//             highlighted = (i == m_paletteIndex);
//         } else {
//             text = m_menuItems.at(i);
//             highlighted = (i == m_menuIndex);
//         }
//         if (highlighted) {
//             // highlight background
//             p.setBrush(QColor(255,255,255,30));
//             p.setPen(Qt::NoPen);
//             p.drawRect(x-6, ry-2, panelW-12, cellH-2);
//         }
//         // draw text
//         p.setPen(Qt::white);
//         p.setFont(QFont("Sans", 12));
//         p.drawText(x, ry + 20, text);
//     }
//     // draw small instructions
//     p.setFont(QFont("Sans", 10));
//     p.setPen(QColor(200,200,200));
//     p.drawText(x, y + (rows+1)*cellH - 6, "PF0=Up  PF1=Down  PF6=Enter");
// }

// void MainWindow::onGpioPressed(unsigned int offset, int value)
// {
//     qDebug() << "GPIO event offset=" << offset << " value=" << value;
//     // we only react on button press (value == 0 is pressed in your platform)
//     if (value != 0) return;
//     // NOTE: adjust offsets if your gpio mapping differs.
//     // PF0 -> offset 0, PF1 -> offset 1, PF6 -> offset 6 (per your offsets[] = {0,1,4,6})
//     const unsigned int PF0 = 0;// up
//     const unsigned int PF1 = 1;//down
//     const unsigned int PF6 = 6; // enter
//     if (offset == PF6) {
//         if (!m_menuVisible) {
//             m_menuWidget->show();
//             m_menuVisible = true;
//             updateMenuHighlight();
//             return;
//         }
//     }
//     if (!m_inSubmenu) {
//         if (offset == PF0) { // up
//             int n = m_menuItems.size();
//             m_menuIndex = (m_menuIndex - 1 + n) % n;
//             updateMenuHighlight();
//         } else if (offset == PF1) { // down
//             int n = m_menuItems.size();
//             m_menuIndex = (m_menuIndex + 1) % n;
//             updateMenuHighlight();
//         } else if (offset == PF6) { // enter
//             enterMenuItem();
//         }
//     } else {
//         // submenu (color palettes)
//          if (offset == PF0) { // up
//         int n = m_subMenuLabels.size();
//         m_subMenuIndex = (m_subMenuIndex - 1 + n) % n;
//         updateSubMenuHighlight();
//     } else if (offset == PF1) { // down
//         int n = m_subMenuLabels.size();
//         m_subMenuIndex = (m_subMenuIndex + 1) % n;
//         updateSubMenuHighlight();
//     } else if (offset == PF6) { // enter
//         QString choice = m_subMenuLabels[m_subMenuIndex]->text();
//         if (choice == "Back") {
//             m_subMenuWidget->hide();
//             m_inSubmenu = false;
//         } else {
//             qDebug() << "Selected submenu:" << choice;
//         }
//     }
// }
// }

void MainWindow::onGpioPressed(unsigned int offset, int value) {
    if (value != 0) return; // Only process button press

    const unsigned int PF0 = 0; // Up
    const unsigned int PF1 = 1; // Down
    const unsigned int PF6 = 6; // Enter
    // const unsigned int WAKEUP_PIN = PA0;

    // if (offset == WAKEUP_PIN && value == 0) {
    //     onWakeupButtonPressed();
    // }

    if (!m_menuVisible) {
        if (offset == PF6) {
            m_menuVisible = true;
            m_menuWidget->show();
            m_currentMenuWidget = m_menuWidget;
            m_menuIndex = 0;
            updateMenuHighlight();
        }
        return; // Menu is not active, so other buttons do nothing
    }

    if (m_currentMenuWidget == m_menuWidget) { // Main menu is active
        if (offset == PF0) { // Up
            m_menuIndex = (m_menuIndex - 1 + m_menuItems.size()) % m_menuItems.size();
        } else if (offset == PF1) { // Down
            m_menuIndex = (m_menuIndex + 1) % m_menuItems.size();
        } else if (offset == PF6) { // Enter
            enterMenuItem(m_menuLabels.at(m_menuIndex)->text());
        }
        updateMenuHighlight();
    } else if (m_currentMenuWidget == m_subMenuWidget) { // Submenu is active
        if (offset == PF0) { // Up
            m_subMenuIndex = (m_subMenuIndex - 1 + m_subMenuLabels.size()) % m_subMenuLabels.size();
        } else if (offset == PF1) { // Down
            m_subMenuIndex = (m_subMenuIndex + 1) % m_subMenuLabels.size();
        } else if (offset == PF6) { // Enter
            enterSubMenuItem(m_subMenuLabels.at(m_subMenuIndex)->text());
        }
        updateSubMenuHighlight();
    }
}

void MainWindow::buildSubMenu(const QString &menuName)
{
    // if (!m_subMenus.contains(menuName)) return;

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
    m_currentMenuWidget = m_subMenuWidget; // Add this line
}

void MainWindow::enterMenuItem(const QString &item)
{
    // QString item = m_menuItems.at(m_menuIndex);

    if (m_subMenus.contains(item)) {
        // open submenu for this item
        m_subMenuWidget->hide();
        m_menuHistory.push(item);
        buildSubMenu(item);
    }
    else if (item == "Exit") {
        m_menuWidget->hide();
        m_menuVisible = false;
        m_menuHistory.clear();
    }
    else if (item == "Power Off") {
        QCoreApplication::quit();
    }
    // else {
    //     qDebug() << "Selected menu item:" << item;
    // }
}

void MainWindow::handleAction(const QString &item) {
    // Implement logic for each action item here
    qDebug() << "Handling action for:" << item;

    if (item == "On") {
        // Logic to turn a feature on
    } else if (item == "Off") {
        // Logic to turn a feature off
    }
    // Add more if/else statements for other actions
}

void MainWindow::enterSubMenuItem(const QString &item)
{
    // QString choice = m_subMenuLabels[m_subMenuIndex]->text();

    if (/*choice*/item == "Back") {
        m_subMenuWidget->hide();
        if(!m_menuHistory.isEmpty()){
            m_menuHistory.pop();
            if (m_menuHistory.isEmpty()) {
                m_menuWidget->show();
                m_currentMenuWidget = m_menuWidget;
            } else {
                buildSubMenu(m_menuHistory.top());
            }
        } else {
            // Should not happen if logic is correct, but good to handle
            m_menuVisible = false;
        }
        return;
        
        // m_inSubmenu = false;
        // return;
    }

    // if this submenu has further nested submenu
    if (m_subMenus.contains(item/*choice*/)) {
        m_subMenuWidget->hide();
        m_menuHistory.push(item);
        buildSubMenu(item/*choice*/);
        //return;
    }
     else {
        handleAction(item);
    }

    // if it's a palette
    // if (m_paletteItems.contains(choice)) {
    //     int idx = m_paletteItems.indexOf(choice);
    //     if (idx >= 0) {
    //         palette_mode_global.store(idx);
    //         emit m_capture->paletteIndexChanged(idx);
    //         m_currentPalette = idx;
    //         qDebug() << "Palette selected:" << choice;
    //     }
    //     m_subMenuWidget->hide();
    //     m_inSubmenu = false;
    //     return;
    // }
    // handle other features (Brightness, Contrast, etc.)
    // qDebug() << "Selected submenu:" << choice;
}

void MainWindow::applySelectedPalette()
{
    if (m_paletteIndex >= 0 && m_paletteIndex < m_paletteItems.size()) {
        palette_mode_global.store(m_paletteIndex);
        emit m_capture->paletteIndexChanged(m_paletteIndex);
        m_currentPalette = m_paletteIndex;
        qDebug() << "Palette selected:" << m_paletteItems.at(m_paletteIndex);
    }
}

void MainWindow::onPowerButtonPressed()
{
    // qDebug() << "System shutting down...";
    QProcess::execute("poweroff"); // or use "shutdown -h now"
}

void MainWindow::setupPowerButton()
{
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        qWarning("Failed to open /dev/input/event0");
        return;
    }

    QSocketNotifier *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, fd]() {
        struct input_event ev;
        while (read(fd, &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) {
                qDebug() << "Power button pressed!";
                onPowerButtonPressed();
            }
        }
    });
}