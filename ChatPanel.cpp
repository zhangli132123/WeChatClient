#include "ChatPanel.h"
#include "ui_ChatPanel.h"
#include "friendrequestdialog.h"
#include "friendbutton.h"
#include "messageitem.h"
#include "dbmanager.h"
#include <QMessageBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatPanel)
    , m_currentFriendId(-1)
    , m_messageOffset(0)
    , m_isLoadingMessages(false)
{
    ui->setupUi(this);
    // 获取全局唯一的API管理器实例
    m_apiManager = ApiManager::instance();

    ui->inputTextEdit->setPlaceholderText("按Enter发送，按Alt+Enter换行");
    
    if (!ui->friendsScrollAreaWidgetContents->layout()) {
        QVBoxLayout* layout = new QVBoxLayout(ui->friendsScrollAreaWidgetContents);
        layout->setSpacing(2);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setAlignment(Qt::AlignTop);
    }
    
    ui->friendsScrollAreaWidgetContents->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->historyVerticalLayout->setAlignment(Qt::AlignTop);
    
    // 添加/删除好友相关信号
    connect(m_apiManager, &ApiManager::friendsReceived, this, &ChatPanel::onFriendsReceived);
    connect(m_apiManager, &ApiManager::friendRemoved, this, &ChatPanel::onFriendRemoved);
    connect(m_apiManager, &ApiManager::removeFriendFailed, this, &ChatPanel::onRemoveFriendFailed);
    connect(m_apiManager, &ApiManager::friendRequestSent, this, &ChatPanel::onFriendRequestSent);
    connect(m_apiManager, &ApiManager::friendRequestFailed, this, &ChatPanel::onFriendRequestFailed);
    
    // 发送/接受消息相关信号
    connect(m_apiManager, &ApiManager::messageSent, this, &ChatPanel::onMessageSent);
    connect(m_apiManager, &ApiManager::messageSendFailed, this, &ChatPanel::onMessageSendFailed);
    connect(m_apiManager, &ApiManager::messagesReceived, this, &ChatPanel::onMessagesReceived);     // 使用http拉取的消息
    connect(m_apiManager, &ApiManager::messagesGetFailed, this, &ChatPanel::onMessagesGetFailed);
    
    // WebSocket 相关信号
    connect(m_apiManager, &ApiManager::newMessageReceived, this, &ChatPanel::onNewMessageReceived);     // 在WebSocket连接的时候，在线收到的消息
    connect(m_apiManager, &ApiManager::offlineMessagesReceived, this, &ChatPanel::onOfflineMessagesReceived);   // 上线后收到的离线时的消息
    connect(m_apiManager, &ApiManager::webSocketConnected, this, &ChatPanel::onWebSocketConnected);
    connect(m_apiManager, &ApiManager::webSocketDisconnected, this, &ChatPanel::onWebSocketDisconnected);
    connect(m_apiManager, &ApiManager::webSocketError, this, &ChatPanel::onWebSocketError);
    
    // 滑动加载
    connect(ui->historyScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, 
            this, &ChatPanel::onScrollBarValueChanged);
    
    // 初始化本地数据库
    DBManager::initDB();
}

void ChatPanel::setUser(const UserInfo &userInfo)
{
    m_currentUser = userInfo;
    loadFriends();
}

ChatPanel::~ChatPanel()
{
    delete ui;
}

void ChatPanel::loadFriends()
{
    // 清除contents里面的组件
    QLayout* layout = ui->friendsScrollAreaWidgetContents->layout();
    if (layout) {
        QLayoutItem *item;
        while ((item = layout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    }
    
    m_friendsMap.clear();
    m_apiManager->getFriends(m_currentUser.id);
}

// 添加一个好友按钮
void ChatPanel::addFriendButton(int friendId, const QString& friendName)
{
    m_friendsMap.insert(friendId, friendName);
    
    FriendButton* item = new FriendButton(friendId, friendName, this);
    
    ui->friendVerticalLayout->setAlignment(Qt::AlignTop);
    QLayout* layout = ui->friendVerticalLayout->layout();
    if (layout) {
        layout->addWidget(item);
    }
    
    connect(item, &FriendButton::friendClicked, this, &ChatPanel::onFriendButtonClicked);
    connect(item, &FriendButton::deleteClicked, this, &ChatPanel::onDeleteButtonClicked);
}

void ChatPanel::onFriendsReceived(const QJsonArray& friends)
{
    for (const QJsonValue& val : friends) {
        QJsonObject friendObj = val.toObject();
        int friendId = friendObj["id"].toInt();
        QString friendName = friendObj["username"].toString();
        addFriendButton(friendId, friendName);
    }
}

void ChatPanel::onFriendButtonClicked(int friendId)
{
    // 如果已经在与该好友聊天，直接返回，不重新加载
    if (m_currentFriendId == friendId) {
        return;
    }

    // 先保存当前输入框的内容（如果有当前好友）
    if (m_currentFriendId != -1) {
        QString currentText = ui->inputTextEdit->toPlainText().trimmed();
        if (!currentText.isEmpty()) {
            saveUnsentMessage(m_currentFriendId, currentText);
        }
        else {
            saveUnsentMessage(m_currentFriendId, "");
        }
    }

    m_currentFriendId = friendId;
    m_currentFriendName = m_friendsMap.value(friendId);
    m_messageOffset = 0;
    m_isLoadingMessages = false;

    // 清空已加载消息ID集合（切换好友时重置）
    m_loadedMessageIds.clear();

    // 更新聊天标题
    ui->label->setText(QString("与 %1 聊天").arg(m_currentFriendName));

    // 清空聊天记录
    clearChatHistory();

    // 加载缓存的未发送消息到输入框
    QString unsentText = loadUnsentMessage(friendId);
    ui->inputTextEdit->setText(unsentText);

    // 先加载本地缓存的消息（快速显示）
    QList<ChatMessage> localMessages = loadMessagesFromLocal(friendId);

    if (!localMessages.isEmpty()) {
        // 按时间排序（升序）
        std::sort(localMessages.begin(), localMessages.end(),
            [](const ChatMessage& a, const ChatMessage& b) {
                return a.createdAt < b.createdAt;
            });

        for (const ChatMessage& msg : localMessages) {
            // 记录已加载的消息ID，用于后续服务器消息去重
            m_loadedMessageIds.insert(msg.id);

            if (shouldShowTimeLabel(msg.createdAt)) {
                addTimeLabel(msg.createdAt);
            }
            addMessageToLayout(msg);
            m_lastMessageTimeMap[m_currentFriendId] = msg.createdAt;
        }

        // 更新offset，避免滚动加载时重复加载本地消息
        m_messageOffset = localMessages.size();
    }

    // 加载服务器消息
    loadMessages(m_messageOffset);

    // 延迟滚动到底部（等待布局更新完成）
    QTimer::singleShot(0, this, [this]() {
        QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void ChatPanel::deleteLocalMessages(int friendId)
{
    QSqlDatabase db = DBManager::getDB();
    if (!db.isOpen()) {
        db.open();
    }

    QSqlQuery query(db);

    // 删除本地消息缓存
    query.prepare(R"(
        DELETE FROM local_messages
        WHERE (from_user_id = ? AND to_user_id = ?)
           OR (from_user_id = ? AND to_user_id = ?)
    )");
    query.addBindValue(m_currentUser.id);
    query.addBindValue(friendId);
    query.addBindValue(friendId);
    query.addBindValue(m_currentUser.id);
    query.exec();

    // 删除未发送消息缓存
    query.prepare(R"(
        DELETE FROM unsent_messages
        WHERE user_id = ? AND friend_id = ?
    )");
    query.addBindValue(m_currentUser.id);
    query.addBindValue(friendId);
    query.exec();
}

void ChatPanel::onDeleteButtonClicked(int friendId)
{
    QString friendName = m_friendsMap.value(friendId);

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认删除",
                                 QString("确定要删除好友 %1 吗？").arg(friendName),
                                 QMessageBox::Yes|QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_apiManager->removeFriend(m_currentUser.id, friendId);

        // 删除本地数据库中该好友的所有记录
        deleteLocalMessages(friendId);

        if (m_currentFriendId == friendId) {
            clearChatHistory();
            ui->label->setText("选择好友开始聊天");
            m_currentFriendId = -1;
        }
    }
}

void ChatPanel::onFriendRemoved()
{
    // 重新加载一次就可以了
    loadFriends();
    QMessageBox::information(this, "成功", "好友已删除");
}

void ChatPanel::onRemoveFriendFailed(const QString& error)
{
    QMessageBox::warning(this, "错误", QString("删除好友失败: %1").arg(error));
}

void ChatPanel::on_friendRequestPushButton_clicked()
{
    FriendRequestDialog* dialog = new FriendRequestDialog(this, m_currentUser.id);
    connect(dialog, &FriendRequestDialog::friendListChanged, this, &ChatPanel::loadFriends);
    dialog->exec();
}

void ChatPanel::on_addFriendButton_clicked()
{
    QString friendUsername = ui->addFriendLineEdit->text().trimmed();
    if (friendUsername.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入要添加的用户名");
        return;
    }
    
    m_apiManager->sendFriendRequest(m_currentUser.id, friendUsername);
    ui->addFriendLineEdit->clear();
}

void ChatPanel::onFriendRequestSent()
{
    QMessageBox::information(this, "成功", "好友请求已发送");
}

void ChatPanel::onFriendRequestFailed(const QString& error)
{
    QMessageBox::warning(this, "错误", QString("发送请求失败: %1").arg(error));
}

void ChatPanel::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) &&
        !(event->modifiers() & Qt::AltModifier)) {
        event->accept();
        sendMessage();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ChatPanel::sendMessage()
{
    if (m_currentFriendId == -1) {
        QMessageBox::warning(this, "提示", "请先选择一个好友");
        return;
    }
    
    QString text = ui->inputTextEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;
    
    // 发送消息到服务器
    m_apiManager->sendMessage(m_currentUser.id, m_currentFriendId, text);
    
    ui->inputTextEdit->clear();
}

// 从Json文件中获得信息，填充到ChatMessage中，后面需要使用信息都是从ChatMessage中调用
void ChatPanel::onMessageSent(const QJsonObject& message)
{
    ChatMessage msg;
    msg.id = message["id"].toInt();
    msg.fromUserId = message["from_user_id"].toInt();
    msg.toUserId = message["to_user_id"].toInt();
    msg.content = message["content"].toString();
    msg.fromUsername = m_currentUser.username;
    
    QString timeStr = message["created_at"].toString();
    msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
    
    // 显示时间标签（如果间隔超过10分钟）
    if (shouldShowTimeLabel(msg.createdAt)) {
        addTimeLabel(msg.createdAt);
    }
    
    // 添加消息到界面
    addMessageToLayout(msg);
    
    // 保存到本地数据库的local_messages表中
    saveMessageToLocal(msg);
    
    // 删除未发送消息缓存（消息已发送成功）（这个缓存是在切换好友的时候，保证输入框中的内容还在）
    deleteUnsentMessage(m_currentFriendId);
    
    m_lastMessageTimeMap[m_currentFriendId] = msg.createdAt;
    
    // 自动滚动到底部
    QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void ChatPanel::onMessageSendFailed(const QString& error)
{
    QMessageBox::warning(this, "错误", QString("发送消息失败: %1").arg(error));
    ui->inputTextEdit->clear();
}

void ChatPanel::loadMessages(int offset)
{
    if (m_currentFriendId == -1 || m_isLoadingMessages) return;
    
    m_isLoadingMessages = true;
    m_apiManager->getMessages(m_currentUser.id, m_currentFriendId, 20, offset);
}

void ChatPanel::onMessagesReceived(const QJsonArray& messages)
{
    m_isLoadingMessages = false;
    
    if (messages.isEmpty()) {
        if (m_messageOffset == 0) {
            ui->label->setText(QString("与 %1 聊天 (暂无消息)").arg(m_currentFriendName));
        }
        return;
    }
    
    // 验证消息是否属于当前好友（检查第一条消息即可）
    if (!messages.isEmpty()) {
        QJsonObject firstMsg = messages.first().toObject();
        int msgFromId = firstMsg["from_user_id"].toInt();
        int msgToId = firstMsg["to_user_id"].toInt();
        if (msgFromId != m_currentFriendId && msgToId != m_currentFriendId) {
            // 消息不属于当前好友，忽略
            return;
        }
    }
    
    // 服务器返回的是按时间倒序，需要反转
    QList<ChatMessage> msgList;
    for (const QJsonValue& val : messages) {
        QJsonObject obj = val.toObject();
        ChatMessage msg;
        msg.id = obj["id"].toInt();
        msg.fromUserId = obj["from_user_id"].toInt();
        msg.toUserId = obj["to_user_id"].toInt();
        msg.fromUsername = obj["from_username"].toString();
        msg.content = obj["content"].toString();
        QString timeStr = obj["created_at"].toString();
        msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
        msgList.append(msg);
        
        // 保存到本地
        saveMessageToLocal(msg);
    }
    
    // 服务器返回的是降序（最新在前），需要反转成升序（最旧在前，最新在后）
    std::reverse(msgList.begin(), msgList.end());
    
    // 使用临时变量跟踪遍历过程中的最后消息时间（用于时间标签判断）
    QDateTime tempLastTime = m_lastMessageTimeMap.value(m_currentFriendId);
    
    // 根据加载类型选择插入方式
    for (const ChatMessage& msg : msgList) {
        // 跳过本地已加载的消息（去重）
        if (m_loadedMessageIds.contains(msg.id)) {
            continue;
        }
        // 记录新加载的消息ID
        m_loadedMessageIds.insert(msg.id);

        // 检查是否需要显示时间标签（使用临时变量）
        bool shouldShow = false;
        if (!tempLastTime.isValid()) {
            shouldShow = true;
        } else {
            int minutesDiff = tempLastTime.secsTo(msg.createdAt) / 60;
            shouldShow = minutesDiff >= 10;
        }
        
        if (shouldShow) {
            QHBoxLayout* timeLayout = new QHBoxLayout();
            QLabel* timeLabel = new QLabel(msg.createdAt.toString("yyyy-MM-dd HH:mm"), 
                                          ui->historyScrollAreaWidgetContents);
            timeLabel->setAlignment(Qt::AlignCenter);
            timeLabel->setStyleSheet("color: #999; font-size: 12px; padding: 5px 0;");
            timeLayout->addWidget(timeLabel);
            timeLayout->setAlignment(Qt::AlignCenter);
            ui->historyVerticalLayout->addLayout(timeLayout);
        }
        
        MessageItem* item = new MessageItem(
            msg.fromUserId == m_currentUser.id ? "我" : msg.fromUsername,
            msg.createdAt,
            msg.content,
            ui->historyScrollAreaWidgetContents
        );
        
        QHBoxLayout* rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        
        if (msg.fromUserId == m_currentUser.id) {
            rowLayout->addStretch();
            rowLayout->addWidget(item);
        } else {
            rowLayout->addWidget(item);
            rowLayout->addStretch();
        }
        
        // 总是添加到末尾（保持升序）
        ui->historyVerticalLayout->addLayout(rowLayout);
        
        // 更新临时变量和Map
        tempLastTime = msg.createdAt;
        m_lastMessageTimeMap[m_currentFriendId] = msg.createdAt;
    }
    
    // 更新offset
    m_messageOffset += messages.size();

    // 延迟滚动到底部（等待布局更新完成）
    QTimer::singleShot(0, this, [this]() {
        QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void ChatPanel::onMessagesGetFailed(const QString& error)
{
    m_isLoadingMessages = false;
    qDebug() << "Get messages failed:" << error;
}

void ChatPanel::onScrollBarValueChanged(int value)
{
    // 当滚动到顶部时加载更多历史消息
    if (value == 0 && !m_isLoadingMessages && m_currentFriendId != -1) {
        loadMessages(m_messageOffset);
    }
}

// 根据msg中的信息，制作相应的消息UI
void ChatPanel::addMessageToLayout(const ChatMessage& msg)
{
    MessageItem* item = new MessageItem(
        msg.fromUserId == m_currentUser.id ? "我" : msg.fromUsername,
        msg.createdAt,
        msg.content,
        ui->historyScrollAreaWidgetContents
    );
    
    // 指定父对象，确保正确的对象树关系
    QHBoxLayout* rowLayout = new QHBoxLayout(ui->historyScrollAreaWidgetContents);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    
    if (msg.fromUserId == m_currentUser.id) {
        rowLayout->addStretch();
        rowLayout->addWidget(item);
    } else {
        rowLayout->addWidget(item);
        rowLayout->addStretch();
    }
    
    ui->historyVerticalLayout->addLayout(rowLayout);
}

void ChatPanel::addTimeLabel(const QDateTime& time)
{
    // 指定父对象，确保正确的对象树关系
    QHBoxLayout* timeLayout = new QHBoxLayout(ui->historyScrollAreaWidgetContents);
    QLabel* timeLabel = new QLabel(time.toString("yyyy-MM-dd HH:mm"), 
                                  ui->historyScrollAreaWidgetContents);
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setStyleSheet("color: #999; font-size: 12px; padding: 5px 0;");
    timeLayout->addWidget(timeLabel);
    timeLayout->setAlignment(Qt::AlignCenter);
    ui->historyVerticalLayout->addLayout(timeLayout);
}


// 递归清除Layout中的内容
void clearLayout(QLayout *layout)
{
    if (!layout)
        return;

    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            // 不需要手动 disconnect，Qt 销毁对象时会自动清理所有信号连接
            widget->hide();
            widget->deleteLater();
        }
        else if (QLayout *childLayout = item->layout())
        {
            clearLayout(childLayout);
        }

        delete item;
    }
}

// 在切换好友和删除好友的时候调用
void ChatPanel::clearChatHistory()
{
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(ui->historyVerticalLayout);
    if (!layout) return;
    
    clearLayout(layout);
    m_messageOffset = 0;
}

bool ChatPanel::shouldShowTimeLabel(const QDateTime& currentTime)
{
    QDateTime lastTime = m_lastMessageTimeMap.value(m_currentFriendId);
    if (!lastTime.isValid()) {
        return true;
    }
    
    // 超过10分钟显示时间标签
    int minutesDiff = lastTime.secsTo(currentTime) / 60;
    return minutesDiff >= 10;
}

void ChatPanel::saveMessageToLocal(const ChatMessage& msg)
{
    QSqlDatabase db = DBManager::getDB();
    if (!db.isOpen()) {
        db.open();
    }
    
    QSqlQuery query(db);
    query.prepare(R"(
        INSERT OR REPLACE INTO local_messages (
            id, from_user_id, to_user_id, from_username, content, created_at
        ) VALUES (?, ?, ?, ?, ?, ?)
    )");
    
    query.addBindValue(msg.id);
    query.addBindValue(msg.fromUserId);
    query.addBindValue(msg.toUserId);
    query.addBindValue(msg.fromUsername);
    query.addBindValue(msg.content);
    query.addBindValue(msg.createdAt.toString("yyyy-MM-dd HH:mm:ss"));
    
    query.exec();
}

QList<ChatMessage> ChatPanel::loadMessagesFromLocal(int friendId)
{
    QList<ChatMessage> messages;
    
    QSqlDatabase db = DBManager::getDB();
    if (!db.isOpen()) {
        db.open();
    }
    
    // 获取一天前的时间
    QDateTime oneDayAgo = QDateTime::currentDateTime().addDays(-1);
    
    QSqlQuery query(db);
    query.prepare(R"(
        SELECT id, from_user_id, to_user_id, from_username, content, created_at
        FROM local_messages
        WHERE ((from_user_id = ? AND to_user_id = ?) 
            OR (from_user_id = ? AND to_user_id = ?))
          AND created_at >= ?
        ORDER BY created_at DESC
        LIMIT 50
    )");
    
    query.addBindValue(m_currentUser.id);
    query.addBindValue(friendId);
    query.addBindValue(friendId);
    query.addBindValue(m_currentUser.id);
    query.addBindValue(oneDayAgo.toString("yyyy-MM-dd HH:mm:ss"));
    
    if (query.exec()) {
        while (query.next()) {
            ChatMessage msg;
            msg.id = query.value(0).toInt();
            msg.fromUserId = query.value(1).toInt();
            msg.toUserId = query.value(2).toInt();
            msg.fromUsername = query.value(3).toString();
            msg.content = query.value(4).toString();
            QString timeStr = query.value(5).toString();
            msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
            messages.append(msg);
        }
    }
    
    return messages;
}


// 把发送框中的消息写入数据库中
void ChatPanel::saveUnsentMessage(int friendId, const QString& content)
{
    QSqlDatabase db = DBManager::getDB();
    if (!db.isOpen()) {
        db.open();
    }
    
    QSqlQuery query(db);
    query.prepare(R"(
        INSERT OR REPLACE INTO unsent_messages (user_id, friend_id, content, updated_at)
        VALUES (?, ?, ?, ?)
    )");
    
    query.addBindValue(m_currentUser.id);
    query.addBindValue(friendId);
    query.addBindValue(content);
    query.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    
    query.exec();
}

QString ChatPanel::loadUnsentMessage(int friendId)
{
    QSqlDatabase db = DBManager::getDB();
    if (!db.isOpen()) {
        db.open();
    }
    
    QSqlQuery query(db);
    query.prepare(R"(
        SELECT content FROM unsent_messages
        WHERE user_id = ? AND friend_id = ?
    )");
    
    query.addBindValue(m_currentUser.id);
    query.addBindValue(friendId);
    
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    
    return QString();
}

void ChatPanel::deleteUnsentMessage(int friendId)
{
    QSqlDatabase db = DBManager::getDB();
    if (!db.isOpen()) {
        db.open();
    }
    
    QSqlQuery query(db);
    query.prepare(R"(
        DELETE FROM unsent_messages
        WHERE user_id = ? AND friend_id = ?
    )");
    
    query.addBindValue(m_currentUser.id);
    query.addBindValue(friendId);
    
    query.exec();
}

// WebSocket 接收新消息
void ChatPanel::onNewMessageReceived(const QJsonObject& message)
{
    int fromUserId = message["from_user_id"].toInt();
    int toUserId = message["to_user_id"].toInt();
    
    // 判断是否是当前好友的消息
    bool isCurrentFriend = (fromUserId == m_currentFriendId || toUserId == m_currentFriendId);
    
    // 保存到本地数据库
    ChatMessage msg;
    msg.id = message["id"].toInt();
    msg.fromUserId = fromUserId;
    msg.toUserId = toUserId;
    msg.content = message["content"].toString();
    msg.fromUsername = message["from_username"].toString();
    QString timeStr = message["created_at"].toString();
    msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
    
    saveMessageToLocal(msg);
    
    // 如果是当前聊天好友的消息，显示到界面
    if (isCurrentFriend && m_currentFriendId != -1) {
        // 显示时间标签（如果间隔超过10分钟）
        if (shouldShowTimeLabel(msg.createdAt)) {
            addTimeLabel(msg.createdAt);
        }
        
        // 添加消息到界面
        addMessageToLayout(msg);
        
        m_lastMessageTimeMap[m_currentFriendId] = msg.createdAt;
        
        // 自动滚动到底部
        QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

// WebSocket 接收离线消息
void ChatPanel::onOfflineMessagesReceived(const QJsonArray& messages)
{
    qDebug() << "收到离线消息: " << messages.size() << " 条";
    
    if (messages.isEmpty() || m_currentFriendId == -1) {
        return;
    }
    
    for (const QJsonValue& val : messages) {
        QJsonObject obj = val.toObject();
        int fromUserId = obj["from_user_id"].toInt();
        int toUserId = obj["to_user_id"].toInt();
        
        // 只处理当前好友的离线消息
        if (fromUserId == m_currentFriendId || toUserId == m_currentFriendId) {
            ChatMessage msg;
            msg.id = obj["id"].toInt();
            msg.fromUserId = fromUserId;
            msg.toUserId = toUserId;
            msg.content = obj["content"].toString();
            msg.fromUsername = obj["from_username"].toString();
            QString timeStr = obj["created_at"].toString();
            msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
            
            // 显示时间标签（如果间隔超过10分钟）
            if (shouldShowTimeLabel(msg.createdAt)) {
                addTimeLabel(msg.createdAt);
            }
            
            // 添加消息到界面
            addMessageToLayout(msg);
            
            m_lastMessageTimeMap[m_currentFriendId] = msg.createdAt;
        }
    }
    
    // 自动滚动到底部
    QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void ChatPanel::onWebSocketConnected()
{
    qDebug() << "WebSocket 连接成功";
}

void ChatPanel::onWebSocketDisconnected()
{
    qDebug() << "WebSocket 连接断开";
}

void ChatPanel::onWebSocketError(const QString& error)
{
    qDebug() << "WebSocket 错误: " << error;
}
