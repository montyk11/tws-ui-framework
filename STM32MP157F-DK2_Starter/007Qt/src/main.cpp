#include <QApplication>
#include <QSplashScreen>
#include <QThread> // for sleep
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Show splash screen
    QSplashScreen splash(QPixmap(":/images/startup.png"));
    splash.show();
    a.processEvents(); // ensure it appears immediately

    // Wait 2 seconds
    QThread::sleep(2);

    MainWindow w;
    w.showFullScreen(); // start full screen (or use showMaximized())

    return a.exec();
}
