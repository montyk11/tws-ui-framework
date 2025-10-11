#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QStringList>
#include <QStack>
#include "OverlayLabel.h"
#include <QSocketNotifier>


class QPainter;
class QImage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    static std::atomic<int> &palette_mode_atomic();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFrameReady(const QImage &img);
    void onPaletteChanged(int idx);
    void onGpioPressed(unsigned int offset, int value);
    void onImuDataReady(float pitch, float roll, float yaw);
    void onPowerButtonPressed();

private:
    QStack<QString> m_menuHistory;
    QWidget *m_currentMenuWidget; // Points to the currently visible menu widget
    // overlay/menu helpers
    // void drawMenuOverlay(QPainter &p, QImage &image); // <-- painter-forwarding signature
    void handleAction(const QString &item);
    void enterMenuItem(const QString &item);
    void enterSubMenuItem(const QString &item);
    void applySelectedPalette();
    void buildSubMenu(const QString &menuName);
    void updateMenuHighlight();
    void updateSubMenuHighlight();
    void setupPowerButton();
    void resizeEvent(QResizeEvent *event);
    void updateSubMenuPos();
    // void onWakeupButtonPressed();
    // QLabel *m_label;
    QLabel *batteryIcon;

    OverlayLabel *m_label;
    class CaptureThread *m_capture;
    QTimer *m_overlayTimer;

    // menu state
    bool m_menuVisible;
    QStringList m_menuItems;
    QStringList m_paletteItems;
    int m_menuIndex = 0;
    int m_paletteIndex = 0;
    int m_currentPalette = 0;
    bool m_inSubmenu = false;
    int m_subMenuIndex = 0;

    QWidget *m_menuWidget;       // new menu overlay
    QWidget *m_subMenuWidget;
    QMap<QString, QStringList> m_subMenus;
    QList<QLabel*> m_menuLabels; // for highlighting
    QList<QLabel*> m_subMenuLabels;

    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    float m_yaw = 0.0f;



};
