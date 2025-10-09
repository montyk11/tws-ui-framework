#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWindow w;
    w.showFullScreen(); // start full screen (or use showMaximized())

    return a.exec();
}
