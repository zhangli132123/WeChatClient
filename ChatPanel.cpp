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
    
    ui->historyVerticalLayout->setAlignment(Qt::AlignTop);
    // ✅ 添加修复代码：
    ui->historyVerticalLayout->setContentsMargins(0, 0, 0, 5);
    ui->historyScrollAreaWidgetContents->setSizePolicy(
        QSizePolicy::Preferred, 
        QSizePolicy::Ignored
    );
    ui->historyScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    
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

    // 重置最后加载时间（切换好友时重置）
    m_lastLoadedTime = QDateTime();
    
    // 重置最后消息时间（切换好友时重置，用于时间标签判断）
    m_lastMessageTime = QDateTime();
    
    // 设置为首次加载状态
    m_isFirstLoad = true;

    // 更新聊天标题
    ui->label->setText(QString("与 %1 聊天").arg(m_currentFriendName));

    // 清空聊天记录
    clearChatHistory();

    // 加载缓存的未发送消息到输入框
    QString unsentText = loadUnsentMessage(friendId);
    ui->inputTextEdit->setText(unsentText);

    // 先加载本地缓存的消息（快速显示）
    QList<ChatMessage> localMessages = loadMessagesFromLocal(friendId); // 这里面加载的信息，时间是不带T的

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
            
            // 更新最后消息时间（用于时间标签判断）
            m_lastMessageTime = msg.createdAt;
        }

        // 设置最后加载时间为最早的本地消息时间，用于后续历史消息分页
        m_lastLoadedTime = localMessages.first().createdAt;
    }

    // 加载服务器消息：
    // - 如果本地有消息，获取比本地最新消息更新的内容（使用 after_time）
    // - 如果本地没有消息，获取最新消息（不传递任何时间参数）
    QString afterTime;
    QString beforeTime;
    if (!localMessages.isEmpty()) {
        afterTime = localMessages.last().createdAt.toString("yyyy-MM-ddTHH:mm:ss");
    }
    
    loadMessages(beforeTime, afterTime);

    // 延迟滚动到底部（等待布局更新完成）
    QTimer::singleShot(30, this, [this]() {
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
    msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-ddTHH:mm:ss");
    
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
    
    // 更新最后消息时间（用于时间标签判断）
    m_lastMessageTime = msg.createdAt;
    
    // 自动滚动到底部
    QTimer::singleShot(30, this, [=]() {
        QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void ChatPanel::onMessageSendFailed(const QString& error)
{
    QMessageBox::warning(this, "错误", QString("发送消息失败: %1").arg(error));
    ui->inputTextEdit->clear();
}

void ChatPanel::loadMessages(const QString& beforeTime, const QString& afterTime)
{
    if (m_currentFriendId == -1 || m_isLoadingMessages) return;
    
    m_isLoadingMessages = true;
    m_apiManager->getMessages(m_currentUser.id, m_currentFriendId, 20, beforeTime, afterTime);
}

void ChatPanel::onMessagesReceived(const QJsonArray& messages)
{
    m_isLoadingMessages = false;
    
    if (messages.isEmpty()) {
        if (!m_lastLoadedTime.isValid() && m_loadedMessageIds.isEmpty()) {
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
    
    // 解析消息
    QList<ChatMessage> msgList;
    bool isHistoryLoad = !m_isFirstLoad; // 是否是加载历史消息
    
    for (const QJsonValue& val : messages) {
        QJsonObject obj = val.toObject();
        ChatMessage msg;
        msg.id = obj["id"].toInt();
        msg.fromUserId = obj["from_user_id"].toInt();
        msg.toUserId = obj["to_user_id"].toInt();
        msg.fromUsername = obj["from_username"].toString();
        msg.content = obj["content"].toString();
        QString timeStr = obj["created_at"].toString();
        msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-ddTHH:mm:ss");
        msgList.append(msg);
        
        // 保存到本地
        saveMessageToLocal(msg);
    }
    
    // 判断是否需要反转：
    // - 首次加载（获取更新消息）：服务器返回升序，不需要反转
    // - 滚动加载历史消息：服务器返回降序，需要反转成升序
    if (isHistoryLoad) {
        std::reverse(msgList.begin(), msgList.end());
    }
    
    // 记录插入前的布局数量，用于历史消息插入
    int layoutCount = ui->historyVerticalLayout->count();
    
    // 遍历消息列表
    for (int i = 0; i < msgList.size(); ++i) {
        const ChatMessage& msg = msgList[i];
        
        // 跳过本地已加载的消息（去重）
        if (m_loadedMessageIds.contains(msg.id)) {
            continue;
        }
        // 记录新加载的消息ID
        m_loadedMessageIds.insert(msg.id);

        // 检查是否需要显示时间标签（从界面布局获取最新消息时间）
        if (shouldShowTimeLabel(msg.createdAt)) {
            addTimeLabel(msg.createdAt, isHistoryLoad);
        }
        
        MessageItem* item = new MessageItem(
            msg.fromUserId == m_currentUser.id ? "我" : msg.fromUsername,
            msg.createdAt,
            msg.content,
            ui->historyScrollAreaWidgetContents
        );
        
        // 设置消息时间属性，用于后续时间标签判断
        item->setProperty("messageTime", msg.createdAt);
        
        QHBoxLayout* rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(item);

        // 根据加载类型选择插入位置：
        // - 首次加载（获取更新消息）：添加到末尾
        // - 滚动加载历史消息：插入到前面
        if (isHistoryLoad) {
            ui->historyVerticalLayout->insertLayout(0, rowLayout);
        } else {
            ui->historyVerticalLayout->addLayout(rowLayout);
        }
        
        // 更新最后消息时间（用于时间标签判断）
        m_lastMessageTime = msg.createdAt;
    }
    
    // 更新最后加载时间（用于下次分页）
    if (!msgList.isEmpty()) {
        if (isHistoryLoad) {
            // 历史消息加载：更新为当前批次最早的消息时间
            m_lastLoadedTime = msgList.first().createdAt;
        } else {
            // 首次加载：如果本地有消息，保持最早时间；否则更新为当前最早时间
            if (!m_lastLoadedTime.isValid()) {
                m_lastLoadedTime = msgList.first().createdAt;
            }
        }
    }

    // 首次加载完成后，设置为非首次加载状态
    m_isFirstLoad = false;

    // 延迟处理 - 等待布局更新完成
    if (!isHistoryLoad) {
        // 首次加载：滚动到底部并检查是否需要自动加载历史消息
        QTimer::singleShot(30, this, [this]() {
            QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
            bar->setValue(bar->maximum());
            
            // 如果没有滚动条（内容太少），自动加载更多历史消息
            autoLoadHistoryMessages();
        });
    } else {
        // 历史消息加载：检查是否需要继续加载更多
        QTimer::singleShot(0, this, [this, hasMore = !msgList.isEmpty()]() {
            if (hasMore) {
                autoLoadHistoryMessages();
            }
        });
    }
}

void ChatPanel::autoLoadHistoryMessages()
{
    QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
    
    // 如果滚动条不可见（maximum == 0 表示内容高度 <= 可视区域高度）
    // 且还有更多消息可加载（m_lastLoadedTime 有效）
    // 且当前没有正在加载
    if (bar->maximum() == 0 && m_lastLoadedTime.isValid() && !m_isLoadingMessages && m_currentFriendId != -1) {
        QString beforeTime = m_lastLoadedTime.toString("yyyy-MM-ddTHH:mm:ss");
        loadMessages(beforeTime);
    }
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
        QString beforeTime;
        if (m_lastLoadedTime.isValid()) {
            beforeTime = m_lastLoadedTime.toString("yyyy-MM-ddTHH:mm:ss");
        }
        loadMessages(beforeTime);
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
    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    
    // if (msg.fromUserId == m_currentUser.id) {
    //     rowLayout->addStretch();
    //     rowLayout->addWidget(item);
    // } else {
    //     rowLayout->addWidget(item);
    //     rowLayout->addStretch();
    // }

    rowLayout->addWidget(item);
    
    ui->historyVerticalLayout->addLayout(rowLayout);
}

void ChatPanel::addTimeLabel(const QDateTime& time, bool insertAtBeginning)
{
    // 指定父对象，确保正确的对象树关系
    QHBoxLayout* timeLayout = new QHBoxLayout();
    QLabel* timeLabel = new QLabel(time.toString("yyyy-MM-dd HH:mm"),
                                  ui->historyScrollAreaWidgetContents);
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setStyleSheet("color: #999; font-size: 12px; padding: 5px 0;");
    timeLayout->addWidget(timeLabel);
    timeLayout->setAlignment(Qt::AlignCenter);
    
    // 根据参数选择插入位置
    if (insertAtBeginning) {
        ui->historyVerticalLayout->insertLayout(0, timeLayout);
    } else {
        ui->historyVerticalLayout->addLayout(timeLayout);
    }
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
    if (!m_lastMessageTime.isValid()) {
        return true;
    }
    
    // 超过10分钟显示时间标签
    int minutesDiff = m_lastMessageTime.secsTo(currentTime) / 60;
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
    qDebug() << timeStr;
    msg.createdAt = QDateTime::fromString(timeStr, "yyyy-MM-ddTHH:mm:ss");
    
    saveMessageToLocal(msg);
    

    // 如果是当前聊天好友的消息，显示到界面
    if (isCurrentFriend && m_currentFriendId != -1) {
        // 显示时间标签（如果间隔超过10分钟）
        if (shouldShowTimeLabel(msg.createdAt)) {
            addTimeLabel(msg.createdAt);
        }
        
        // 添加消息到界面
        addMessageToLayout(msg);
        
        // 更新最后消息时间（用于时间标签判断）
        m_lastMessageTime = msg.createdAt;
        
        // 自动滚动到底部
        // QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
        // bar->setValue(bar->maximum());
        QTimer::singleShot(30, this, [=]() {
            QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
            bar->setValue(bar->maximum());
        });
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
            
            // 更新最后消息时间（用于时间标签判断）
            m_lastMessageTime = msg.createdAt;
        }
    }
    
    // 自动滚动到底部
    QTimer::singleShot(30, this, [=]() {
        QScrollBar* bar = ui->historyScrollArea->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
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
