#include "apimanager.h"
#include <QNetworkProxy>

// 静态实例指针
ApiManager* ApiManager::m_instance = nullptr;

// 获取全局唯一实例
ApiManager* ApiManager::instance()
{
    if (!m_instance) {
        // 创建唯一实例，parent 为 nullptr 表示顶层对象
        m_instance = new ApiManager();
    }
    return m_instance;
}

// 私有构造函数
ApiManager::ApiManager(QObject *parent) : QObject(parent)
{
    // 禁用代理，避免系统代理干扰网络连接
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    
    m_networkManager = new QNetworkAccessManager(this);
    m_serverUrl = "http://8.137.183.24:8080";
    m_webSocketUrl = "ws://8.137.183.24:8080/ws";
    
    // 初始化 WebSocket
    m_webSocket = nullptr;
    m_currentUserId = -1;
    m_isWebSocketConnected = false;
    m_reconnectAttempts = 0;
    
    // 初始化定时器
    m_heartbeatTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    m_retryTimer = new QTimer(this);
    m_msgIdCounter = 0;
    
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ApiManager::onHeartbeatTimeout);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ApiManager::onReconnectTimeout);
    connect(m_retryTimer, &QTimer::timeout, this, &ApiManager::onRetryTimeout);
}

ApiManager::~ApiManager()
{
    disconnectWebSocket();
    // m_webSocket 有 parent，会被 Qt 自动删除，不需要手动 delete
}

void ApiManager::setServerUrl(const QString& url)
{
    m_serverUrl = url;

    QString wsUrl = url;
    wsUrl.replace("http://", "ws://");
    wsUrl.replace("https://", "wss://");

    m_webSocketUrl = wsUrl;
}



/****************************************************** 登录注册 ******************************************************/
void ApiManager::login(const QString& username, const QString& password)
{
    QUrl url(m_serverUrl + "/api/login");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject data;
    data["username"] = username;
    data["password"] = password;

    QJsonDocument doc(data);
    QByteArray jsonData = doc.toJson();

    QNetworkReply* reply = m_networkManager->post(request, jsonData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        this->onLoginReply(reply);
    });
}

void ApiManager::registerUser(const QString& username, const QString& password)
{
    QUrl url(m_serverUrl + "/api/register");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject data;
    data["username"] = username;
    data["password"] = password;

    QJsonDocument doc(data);
    QByteArray jsonData = doc.toJson();

    QNetworkReply* reply = m_networkManager->post(request, jsonData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onRegisterReply(reply);
    });
}

void ApiManager::onLoginReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit loginFailed(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        emit loginFailed("Invalid response format");
        reply->deleteLater();
        return;
    }

    QJsonObject obj = doc.object();
    
    // 后端会在返回的Json文件中返回success字段
    if (obj.contains("success") && obj["success"].toBool()) {
        QJsonObject userData = obj["data"].toObject();
        emit loginSuccess(userData);
        
        // 登录成功后连接 WebSocket
        m_currentUserId = userData["id"].toInt();
        connectWebSocket(m_currentUserId);
    } else {
        QString error = obj.contains("message") ? obj["message"].toString() : "Login failed";
        emit loginFailed(error);
    }

    reply->deleteLater();
}

void ApiManager::onRegisterReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit registerFailed(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        emit registerFailed("Invalid response format");
        reply->deleteLater();
        return;
    }

    QJsonObject obj = doc.object();
    
    if (obj.contains("success") && obj["success"].toBool()) {
        emit registerSuccess();
    } else {
        QString error = obj.contains("message") ? obj["message"].toString() : "Register failed";
        emit registerFailed(error);
    }

    reply->deleteLater();
}
/****************************************************** 登录注册 ******************************************************/





/****************************************************** 好友 ******************************************************/
void ApiManager::sendFriendRequest(int userId, const QString& friendUsername)
{
    QUrl url(m_serverUrl + "/api/add_friend");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject data;
    data["user_id"] = userId;
    data["friend_username"] = friendUsername;
    
    QJsonDocument doc(data);
    QNetworkReply* reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onSendFriendRequestReply(reply);
    });
}

void ApiManager::onSendFriendRequestReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit friendRequestFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        emit friendRequestFailed("Invalid response format");
        reply->deleteLater();
        return;
    }
    
    QJsonObject obj = doc.object();
    if (obj.contains("success") && obj["success"].toBool()) {
        emit friendRequestSent();
    } else {
        QString error = obj.contains("message") ? obj["message"].toString() : "Send friend request failed";
        emit friendRequestFailed(error);
    }
    
    reply->deleteLater();
}

void ApiManager::handleFriendRequest(int requestId, const QString& action)
{
    QUrl url(m_serverUrl + "/api/handle_friend_request");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject data;
    data["request_id"] = requestId;
    data["action"] = action;
    
    QJsonDocument doc(data);
    QNetworkReply* reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHandleFriendRequestReply(reply);
    });
}

void ApiManager::onHandleFriendRequestReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit friendRequestFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        emit friendRequestFailed("Invalid response format");
        reply->deleteLater();
        return;
    }
    
    QJsonObject obj = doc.object();
    if (obj.contains("success") && obj["success"].toBool()) {
        emit friendRequestHandled(obj["message"].toString());
    } else {
        QString error = obj.contains("message") ? obj["message"].toString() : "Handle friend request failed";
        emit friendRequestFailed(error);
    }
    
    reply->deleteLater();
}

void ApiManager::getFriends(int userId)
{
    QUrl url(m_serverUrl + "/api/friends?user_id=" + QString::number(userId));
    QNetworkRequest request(url);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onGetFriendsReply(reply);
    });
}

void ApiManager::onGetFriendsReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        reply->deleteLater();
        return;
    }
    
    QJsonObject obj = doc.object();
    if (obj.contains("success") && obj["success"].toBool()) {
        emit friendsReceived(obj["data"].toArray());
    }
    
    reply->deleteLater();
}

void ApiManager::getFriendRequests(int userId)
{
    QUrl url(m_serverUrl + "/api/friend_requests?user_id=" + QString::number(userId));
    QNetworkRequest request(url);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onGetFriendRequestsReply(reply);
    });
}

void ApiManager::onGetFriendRequestsReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        reply->deleteLater();
        return;
    }
    
    QJsonObject obj = doc.object();
    if (obj.contains("success") && obj["success"].toBool()) {
        emit friendRequestsReceived(obj["data"].toArray());
    }
    
    reply->deleteLater();
}

void ApiManager::removeFriend(int userId, int friendId)
{
    QUrl url(m_serverUrl + "/api/remove_friend");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject data;
    data["user_id"] = userId;
    data["friend_id"] = friendId;
    
    QJsonDocument doc(data);
    QNetworkReply* reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onRemoveFriendReply(reply);
    });
}

void ApiManager::onRemoveFriendReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit removeFriendFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        emit removeFriendFailed("Invalid response format");
        reply->deleteLater();
        return;
    }
    
    QJsonObject obj = doc.object();
    if (obj.contains("success") && obj["success"].toBool()) {
        emit friendRemoved();
    } else {
        QString error = obj.contains("message") ? obj["message"].toString() : "Remove friend failed";
        emit removeFriendFailed(error);
    }
    
    reply->deleteLater();
}
/****************************************************** 好友 ******************************************************/




/****************************************************** 消息发送 ******************************************************/
void ApiManager::sendMessage(int fromUserId, int toUserId, const QString& content)
{
    // 生成客户端消息 ID（用于 ACK 确认和去重）
    QString clientMsgId = QString("%1_%2_%3")
        .arg(fromUserId)
        .arg(m_msgIdCounter++)  // TCP连接是from和to来确定的，每个客户端自己做好唯一ID就可以了
        .arg(QDateTime::currentMSecsSinceEpoch());
    
    // 构造待确认消息
    PendingMessage pending;
    pending.clientMsgId = clientMsgId;
    pending.fromUserId = fromUserId;
    pending.toUserId = toUserId;
    pending.content = content;
    pending.retryCount = 0;
    pending.sendTime = QDateTime::currentDateTime();
    
    // 加入待确认队列
    m_pendingMessages.insert(clientMsgId, pending);
    
    // 发送消息（通过 WebSocket）
    sendPendingMessage(pending);
    
    // 启动重试定时器（如果未启动）
    if (!m_retryTimer->isActive()) {
        m_retryTimer->start(ACK_TIMEOUT);
    }
}

// 实际用来发送消息的函数
void ApiManager::sendPendingMessage(const PendingMessage& msg)
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        return;  // 连接断开时，等待重连后由 onRetryTimeout 重试
    }
    
    QJsonObject data;
    data["from_user_id"] = msg.fromUserId;
    data["to_user_id"] = msg.toUserId;
    data["content"] = msg.content;
    data["client_msg_id"] = msg.clientMsgId;
    
    QJsonDocument doc(data);
    // 把消息包装金message
    QString message = QString("{\"type\":\"send_message\",\"data\":%1}")
        .arg(doc.toJson(QJsonDocument::Compact));
    // 用webSocket发送消息
    m_webSocket->sendTextMessage(message);
    
    qDebug() << "发送消息, client_msg_id:" << msg.clientMsgId 
             << "重试次数:" << msg.retryCount;
}

void ApiManager::getMessages(int userId, int friendId, int limit, int offset)
{
    QUrl url(m_serverUrl + QString("/api/get_messages?user_id=%1&friend_id=%2&limit=%3&offset=%4")
             .arg(userId).arg(friendId).arg(limit).arg(offset));
    QNetworkRequest request(url);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onGetMessagesReply(reply);
    });
}

void ApiManager::onGetMessagesReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit messagesGetFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    if (!doc.isObject()) {
        emit messagesGetFailed("Invalid response format");
        reply->deleteLater();
        return;
    }
    
    QJsonObject obj = doc.object();
    if (obj.contains("success") && obj["success"].toBool()) {
        emit messagesReceived(obj["data"].toArray());
    } else {
        QString error = obj.contains("message") ? obj["message"].toString() : "Get messages failed";
        emit messagesGetFailed(error);
    }
    
    reply->deleteLater();
}
/****************************************************** 消息发送 ******************************************************/





// === WebSocket 相关实现 ===

void ApiManager::connectWebSocket(int userId)
{
    m_currentUserId = userId;
    
    // 如果存在旧连接，先关闭并删除
    if (m_webSocket) {
        // 断开所有信号连接
        m_webSocket->disconnect();
        // 关闭连接
        m_webSocket->close();
        // 删除旧对象
        delete m_webSocket;
        m_webSocket = nullptr;
    }
    
    // 创建新的 WebSocket，设置 parent 以便 Qt 自动管理生命周期
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    // 连接信号
    connect(m_webSocket, &QWebSocket::connected, this, &ApiManager::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &ApiManager::onWebSocketDisconnected);
    connect(m_webSocket, &QWebSocket::errorOccurred, this, &ApiManager::onWebSocketError);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &ApiManager::onWebSocketTextMessageReceived);
    
    m_reconnectAttempts = 0;
    m_webSocket->open(QUrl(m_webSocketUrl));
    qDebug() << "正在连接 WebSocket: " << m_webSocketUrl;
}

void ApiManager::disconnectWebSocket()
{
    if (m_webSocket) {
        m_webSocket->close();
    }
    
    m_heartbeatTimer->stop();
    m_reconnectTimer->stop();
    m_isWebSocketConnected = false;
}

bool ApiManager::isWebSocketConnected() const
{
    return m_isWebSocketConnected;
}

void ApiManager::onWebSocketConnected()
{
    qDebug() << "WebSocket 连接成功";
    m_isWebSocketConnected = true;
    m_reconnectAttempts = 0;
    m_reconnectTimer->stop();
    
    emit webSocketConnected();
    
    // 发送登录消息到服务器
    QJsonObject loginData;
    loginData["user_id"] = m_currentUserId;
    QJsonDocument doc(loginData);
    QString message = QString("{\"type\":\"login\",\"data\":%1}").arg(doc.toJson(QJsonDocument::Compact));
    m_webSocket->sendTextMessage(message);
    
    // 启动心跳定时器
    m_lastHeartbeatTime = QDateTime::currentDateTime();
    m_heartbeatTimer->start(HEARTBEAT_INTERVAL);
    sendHeartbeat();
}

void ApiManager::onWebSocketDisconnected()
{
    qDebug() << "WebSocket 连接断开";
    m_isWebSocketConnected = false;
    m_heartbeatTimer->stop();
    
    emit webSocketDisconnected();
    
    // 尝试自动重连
    scheduleReconnect();
}

void ApiManager::onWebSocketError(QAbstractSocket::SocketError error)
{
    qDebug() << "WebSocket 错误: " << m_webSocket->errorString();
    qDebug() << "WebSocket 错误代码: " << error;
    qDebug() << "WebSocket 状态: " << m_webSocket->state();
    emit webSocketError(m_webSocket->errorString());
    
    // 触发重连
    if (m_isWebSocketConnected) {
        m_isWebSocketConnected = false;
        m_heartbeatTimer->stop();
        scheduleReconnect();
    }
}

// 服务器受到消息后释放信号所执行的槽函数
void ApiManager::onWebSocketTextMessageReceived(const QString& message)
{
    qDebug() << "收到 WebSocket 消息: " << message;
    
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    if (type == "new_message") {
        emit newMessageReceived(obj["data"].toObject());
    } else if (type == "offline_messages") {
        emit offlineMessagesReceived(obj["messages"].toArray());
    } else if (type == "message_sent") {
        // 收到服务端 ACK，从待确认队列中移除
        QString clientMsgId = obj["client_msg_id"].toString();
        if (!clientMsgId.isEmpty() && m_pendingMessages.contains(clientMsgId)) {
            m_pendingMessages.remove(clientMsgId);
            qDebug() << "消息 ACK 确认, client_msg_id:" << clientMsgId;
            // 如果队列为空，停止重试定时器
            if (m_pendingMessages.isEmpty()) {
                m_retryTimer->stop();
            }
        }
        emit messageSent(obj["data"].toObject());
    } else if (type == "message_send_failed") {
        // 服务端拒绝消息
        QString clientMsgId = obj["client_msg_id"].toString();
        if (!clientMsgId.isEmpty() && m_pendingMessages.contains(clientMsgId)) {
            m_pendingMessages.remove(clientMsgId);
            if (m_pendingMessages.isEmpty()) {
                m_retryTimer->stop();
            }
        }
        emit messageSendFailed(obj["message"].toString());
    } else if (type == "heartbeat_response") {
        m_lastHeartbeatTime = QDateTime::currentDateTime();
        qDebug() << "收到心跳响应";
    }
}

void ApiManager::sendHeartbeat()
{
    if (m_isWebSocketConnected && m_webSocket) {
        QJsonObject data;
        data["type"] = "heartbeat";
        QJsonDocument doc(data);
        m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        qDebug() << "发送心跳";
    }
}

void ApiManager::onRetryTimeout()
{
    // 检查待确认队列中是否有超时消息需要重试
    QList<QString> toRemove;  // 需要移除的消息（重试次数用尽）
    QDateTime now = QDateTime::currentDateTime();
    
    QMapIterator<QString, PendingMessage> it(m_pendingMessages);
    while (it.hasNext()) {
        it.next();
        const PendingMessage& msg = it.value();
        
        qint64 elapsed = msg.sendTime.msecsTo(now);
        if (elapsed < ACK_TIMEOUT) {
            continue;  // 还没到超时时间
        }
        
        if (msg.retryCount < MAX_RETRY_COUNT) {
            // 还有重试次数，重发消息
            PendingMessage retryMsg = msg;
            retryMsg.retryCount++;
            retryMsg.sendTime = now;
            m_pendingMessages[msg.clientMsgId] = retryMsg;
            
            sendPendingMessage(retryMsg);
            qDebug() << "重试消息, client_msg_id:" << msg.clientMsgId
                     << "第" << retryMsg.retryCount << "次重试";
        } else {
            // 重试次数用尽，标记为失败
            toRemove.append(msg.clientMsgId);
            qDebug() << "消息发送失败（重试次数用尽）, client_msg_id:" << msg.clientMsgId;
            emit messageSendFailed(QString("消息发送失败（已重试 %1 次）: %2")
                .arg(MAX_RETRY_COUNT)
                .arg(msg.content));
        }
    }
    
    // 移除失败的消息
    for (const QString& id : toRemove) {
        m_pendingMessages.remove(id);
    }
    
    // 如果队列为空，停止定时器
    if (m_pendingMessages.isEmpty()) {
        m_retryTimer->stop();
    }
}

// 心跳计时器到时了触发的槽函数
void ApiManager::onHeartbeatTimeout()
{
    qint64 secondsSinceLastHeartbeat = m_lastHeartbeatTime.secsTo(QDateTime::currentDateTime());
    
    if (secondsSinceLastHeartbeat >= HEARTBEAT_TIMEOUT / 1000) {
        qDebug() << "心跳超时，断开连接并尝试重连";
        emit heartbeatTimeout();
        
        if (m_webSocket) {
            m_webSocket->close();
        }
        
        scheduleReconnect();
    } else {
        // 发送心跳
        sendHeartbeat();
    }
}

void ApiManager::scheduleReconnect()
{
    if (m_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        qDebug() << "已达到最大重连次数，停止尝试";
        return;
    }
    
    m_reconnectAttempts++;
    qDebug() << QString("计划第 %1 次重连").arg(m_reconnectAttempts);
    
    if (!m_reconnectTimer->isActive()) {
        m_reconnectTimer->start(RECONNECT_INTERVAL);
    }
}

void ApiManager::onReconnectTimeout()
{
    m_reconnectTimer->stop();
    
    if (m_currentUserId > 0) {
        qDebug() << QString("尝试第 %1 次重连").arg(m_reconnectAttempts);
        connectWebSocket(m_currentUserId);
    }
}