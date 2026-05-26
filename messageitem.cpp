#include "messageitem.h"
#include "ui_messageitem.h"
#include <QDateTime>


MessageItem::MessageItem(const QString &name, QDateTime time, const QString &text, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MessageItem)
{
    ui->setupUi(this);

    // 设置 MessageItem 的大小策略：根据内容自适应，不超过最大宽度
    setMaximumWidth(300);  // 限制最大宽度
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);  // 根据内容自适应

    ui->messageLabel->setWordWrap(true);
    ui->messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    ui->messageLabel->setMaximumWidth(280);  // 比父容器小一点，留边距
    ui->messageLabel->setMinimumWidth(50);
    
    ui->messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    
    ui->nameLabel->setText(name + ':');
    ui->messageLabel->setText(text);
}


void MessageItem::setText(const QString &text)
{
    ui->messageLabel->setText(text);
}

MessageItem::~MessageItem()
{
    delete ui;
}
