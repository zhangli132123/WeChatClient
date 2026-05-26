#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loginPanel = new LoginPanel;
    chatPanel  = new ChatPanel;

    setCentralWidget(loginPanel);
    adjustWindowToWidget(loginPanel);

    connect(loginPanel, &LoginPanel::loginSuccess,
            this, [=](UserInfo user){
                chatPanel->setUser(user);
                setCentralWidget(chatPanel);
                adjustWindowToWidget(chatPanel);
            });
}

void MainWindow::adjustWindowToWidget(QWidget *w)
{
    w->ensurePolished();   // 关键：确保布局计算完成
    w->adjustSize();

    // 获取widget的实际大小（adjustSize后的大小）
    QSize widgetSize = w->size();

    if (widgetSize.width() <= 0 || widgetSize.height() <= 0)
        return;

    // 对于QMainWindow，需要考虑窗口框架（标题栏、边框等）的大小
    // 计算窗口所需的总大小（包括框架）
    QSize windowSize = widgetSize + this->frameGeometry().size() - this->geometry().size();
    
    this->resize(windowSize);

    this->setMinimumSize(windowSize);
    this->setMaximumSize(windowSize);
}

MainWindow::~MainWindow()
{
}
