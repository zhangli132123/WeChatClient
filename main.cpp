#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWindow w;
    w.show();
    return QCoreApplication::exec();
}
// #include <QApplication>
// #include <QWebSocket>
// #include <QDebug>

// int main(int argc, char *argv[])
// {
//     QApplication a(argc, argv);

//     QWebSocket *ws = new QWebSocket();

//     QObject::connect(ws, &QWebSocket::connected, []() {
//         qDebug() << "CONNECTED";
//     });

//     QObject::connect(ws, &QWebSocket::disconnected, []() {
//         qDebug() << "DISCONNECTED";
//     });

//     QObject::connect(ws, &QWebSocket::errorOccurred,
//                      [ws](QAbstractSocket::SocketError error) {
//                          qDebug() << "ERROR:" << error;
//                          qDebug() << ws->errorString();
//                      });

//     qDebug() << "OPENING...";
//     ws->open(QUrl("ws://8.137.183.24:8080/ws"));

//     return a.exec();
// }