#include "friendrequestdialog.h"
#include "ui_friendrequestdialog.h"
#include "friendrequestitem.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

FriendRequestDialog::FriendRequestDialog(QWidget *parent, int userId)
    : QDialog(parent)
    , ui(new Ui::FriendRequestDialog)
    , m_currentUserId(userId)
{
    ui->setupUi(this);
    // 获取全局唯一的API管理器实例
    m_apiManager = ApiManager::instance();
    
    connect(m_apiManager, &ApiManager::friendRequestsReceived, this, &FriendRequestDialog::onFriendRequestsReceived);
    connect(m_apiManager, &ApiManager::friendRequestHandled, this, &FriendRequestDialog::onFriendRequestHandled);
    
    loadRequests();
}

FriendRequestDialog::~FriendRequestDialog()
{
    delete ui;
}

void FriendRequestDialog::loadRequests()
{
    // 清空现有请求列表
    QLayoutItem *item;
    while ((item = ui->verticalLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    m_apiManager->getFriendRequests(m_currentUserId);
}

void FriendRequestDialog::onFriendRequestsReceived(const QJsonArray& requests)
{
    if (requests.isEmpty()) {
        QLabel* label = new QLabel("暂无好友请求");
        ui->verticalLayout->addWidget(label);
        return;
    }
    
    for (const QJsonValue& val : requests) {
        QJsonObject req = val.toObject();
        int requestId = req["id"].toInt();
        QString username = req["username"].toString();

        FriendRequestItem* item = new FriendRequestItem(requestId, username, this);
        
        ui->verticalLayout->addWidget(item);
        
        // 连接信号
        connect(item, &FriendRequestItem::acceptClicked, this, &FriendRequestDialog::onAgreeButtonClicked);
        connect(item, &FriendRequestItem::rejectClicked, this, &FriendRequestDialog::onDisagreeButtonClicked);
    }
    
    // 添加 spacer 把内容推到顶部
    ui->verticalLayout->addStretch();
}

void FriendRequestDialog::onAgreeButtonClicked(int requestId)
{
    m_apiManager->handleFriendRequest(requestId, "accept");
}

void FriendRequestDialog::onDisagreeButtonClicked(int requestId)
{
    m_apiManager->handleFriendRequest(requestId, "reject");
}

void FriendRequestDialog::onFriendRequestHandled(const QString& message)
{
    QMessageBox::information(this, "提示", message);
    loadRequests(); // 重新加载请求列表
    emit friendListChanged();
}
