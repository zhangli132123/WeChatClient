#include "friendrequestitem.h"
#include "ui_friendrequestitem.h"

FriendRequestItem::FriendRequestItem(int userId, QString name, QWidget *parent)
    : QWidget(parent), m_userId(userId)
    , ui(new Ui::FriendRequestItem)
{
    ui->setupUi(this);
    
    // 使用 UI 文件中的 label 设置用户名
    ui->label->setText(name);
    
    // 连接 UI 文件中的按钮
    connect(ui->agreePushButton, &QPushButton::clicked, this, [=]() {
        emit acceptClicked(m_userId);
    });

    connect(ui->disagreePushButton, &QPushButton::clicked, this, [=]() {
        emit rejectClicked(m_userId);
    });
}

void FriendRequestItem::resizeEvent(QResizeEvent *event)
{
    // 让 horizontalLayoutWidget 填满整个 FriendButton
    ui->horizontalLayout->setGeometry(this->rect());
    QWidget::resizeEvent(event);
}

FriendRequestItem::~FriendRequestItem()
{
    delete ui;
}