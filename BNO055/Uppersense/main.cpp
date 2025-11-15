#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWindow w;
    w.showMaximized();   // ya lo hace el constructor, pero por si acaso

    return a.exec();
}
