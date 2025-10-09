#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QStringList>
#include <QStack>
#include <QMap>
#include <QList>
#include <atomic>
#include "OverlayLabel.h"

class QPainter;
class QImage;
class QCloseEvent;
class QKeyEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // expose reference to global atomic (defined in mainwindow.cpp)
    static std::atomic<int> &palette_mode_atomic();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFrameReady(const QImage &img);
    void onPaletteChanged(int idx);
    void onGpioPressed(unsigned int offset, int value);
    void onImuDataReady(float pitch, float roll, float yaw);

private:
    // menu/navigation helpers
    void handleAction(const QString &item);
    void enterMenuItem(const QString &item);
    void enterSubMenuItem(const QString &item);
    void applySelectedPalette();
    void buildSubMenu(const QString &menuName);
    void updateMenuHighlight();
    void updateSubMenuHighlight();

    // zoom helpers
    QImage applyZoomToImage(const QImage &src, int zoomLevel);
    QString sanitizeMenuItem(const QString &raw);
    int findPaletteIndexByName(const QString &name);

    // UI widgets
    OverlayLabel *m_label;
    QLabel *m_zoomLabel;           // top-left zoom label
    class CaptureThread *m_capture;
    QTimer *m_overlayTimer;

    // menu state
    QStack<QString> m_menuHistory;
    QWidget *m_currentMenuWidget = nullptr;
    QWidget *m_menuWidget = nullptr;
    QWidget *m_subMenuWidget = nullptr;
    bool m_menuVisible = false;
    QStringList m_menuItems;
    QMap<QString, QStringList> m_subMenus;
    QList<QLabel*> m_menuLabels;
    QList<QLabel*> m_subMenuLabels;
    int m_menuIndex = 0;
    bool m_inSubmenu = false;
    int m_subMenuIndex = 0;

    // palette state
    QStringList m_paletteItems;
    int m_paletteIndex = 0;
    int m_currentPalette = 0;

    // IMU
    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    float m_yaw = 0.0f;

    // zoom state
    const QList<int> m_zoomLevels = {1, 2, 4}; // cycle order
    int m_zoomIndex = 0;                       // index into m_zoomLevels
    int m_currentZoom = 1;                     // current zoom magnitude
};
