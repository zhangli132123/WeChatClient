#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "LoginPanel.h"
#include "ChatPanel.h"
#include "userinfo.h"
#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    LoginPanel *loginPanel;
    ChatPanel *chatPanel;
    void adjustWindowToWidget(QWidget *w);
};

#endif // MAINWINDOW_H
