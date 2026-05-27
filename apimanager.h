#ifndef APIMANAGER_H
#define APIMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QWebSocket>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QUuid>

// 待确认消息结构
struct PendingMessage {
    QString clientMsgId;      // 客户端消息 ID（UUID）
    int fromUserId;
    int toUserId;
    QString content;
    int retryCount;           // 已重试次数
    QDateTime sendTime;       // 首次发送时间
};

class ApiManager : public QObject
{
    Q_OBJECT

public:
    // 单例模式 - 获取全局唯一实例
    static ApiManager* instance();
    
    // 禁止拷贝和赋值
    ApiManager(const ApiManager&) = delete;
    ApiManager& operator=(const ApiManager&) = delete;
    
    // 析构函数
    ~ApiManager();
    
    // 登录API
    void login(const QString& username, const QString& password);
    
    // 注册API
    void registerUser(const QString& username, const QString& password);
    
    // 设置服务端地址
    void setServerUrl(const QString& url);
    
    // 好友相关API
    void sendFriendRequest(int userId, const QString& friendUsername);
    void handleFriendRequest(int requestId, const QString& action);
    void getFriends(int userId);
    void getFriendRequests(int userId);
    void removeFriend(int userId, int friendId);
    
    // 消息相关API
    void sendMessage(int fromUserId, int toUserId, const QString& content);
    void getMessages(int userId, int friendId, int limit = 20, int offset = 0);
    void getMessages(int userId, int friendId, int limit, const QString& beforeTime);
    void getMessages(int userId, int friendId, int limit, const QString& beforeTime, const QString& afterTime);
    
    // WebSocket 相关
    void connectWebSocket(int userId);
    void disconnectWebSocket();
    bool isWebSocketConnected() const;

signals:
    // 这些信号发送出来，主要用来给UI来相应的
    void loginSuccess(const QJsonObject& userData);
    void loginFailed(const QString& error);
    void registerSuccess();
    void registerFailed(const QString& error);
    
    // 好友相关信号
    void friendRequestSent();
    void friendRequestFailed(const QString& error);
    void friendRequestHandled(const QString& message);
    void friendsReceived(const QJsonArray& friends);
    void friendRequestsReceived(const QJsonArray& requests);
    void friendRemoved();
    void removeFriendFailed(const QString& error);
    
    // 消息相关信号
    void messageSent(const QJsonObject& message);
    void messageSendFailed(const QString& error);
    void messagesReceived(const QJsonArray& messages);
    void messagesGetFailed(const QString& error);
    
    // WebSocket 相关信号
    void webSocketConnected();
    void webSocketDisconnected();
    void webSocketError(const QString& error);
    void newMessageReceived(const QJsonObject& message);
    void offlineMessagesReceived(const QJsonArray& messages);
    void heartbeatTimeout();

private slots:
    void onLoginReply(QNetworkReply* reply);
    void onRegisterReply(QNetworkReply* reply);
    void onSendFriendRequestReply(QNetworkReply* reply);
    void onHandleFriendRequestReply(QNetworkReply* reply);
    void onGetFriendsReply(QNetworkReply* reply);
    void onGetFriendRequestsReply(QNetworkReply* reply);
    void onRemoveFriendReply(QNetworkReply* reply);
    void onGetMessagesReply(QNetworkReply* reply);
    
    // WebSocket 相关槽函数
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketError(QAbstractSocket::SocketError error);
    void onWebSocketTextMessageReceived(const QString& message);
    void onHeartbeatTimeout();
    void onReconnectTimeout();
    
    // ACK 重试槽函数
    void onRetryTimeout();

private:
    // 单例模式静态成员
    static ApiManager* m_instance;
    
    // 私有构造函数，禁止外部创建
    explicit ApiManager(QObject *parent = nullptr);
    
    QNetworkAccessManager* m_networkManager;
    QString m_serverUrl;
    QString m_webSocketUrl;
    
    // WebSocket 相关成员
    QWebSocket* m_webSocket;
    int m_currentUserId;
    bool m_isWebSocketConnected;
    
    // 心跳相关
    QTimer* m_heartbeatTimer;
    QTimer* m_reconnectTimer;
    QDateTime m_lastHeartbeatTime;
    int m_reconnectAttempts;
    const int HEARTBEAT_INTERVAL = 10000;  // 10秒
    const int HEARTBEAT_TIMEOUT = 30000;   // 30秒超时
    const int RECONNECT_INTERVAL = 5000;   // 5秒重连间隔
    const int MAX_RECONNECT_ATTEMPTS = 10; // 最大重连次数
    
    // ACK 重试相关
    QMap<QString, PendingMessage> m_pendingMessages;  // 待确认消息队列
    QTimer* m_retryTimer;                             // 重试定时器
    int m_msgIdCounter;                               // 消息 ID 计数器
    const int ACK_TIMEOUT = 5000;                     // ACK 超时时间（5秒）
    const int MAX_RETRY_COUNT = 3;                    // 最大重试次数
    
    void sendHeartbeat();
    void scheduleReconnect();
    void sendPendingMessage(const PendingMessage& msg);  // 发送/重发待确认消息
};

#endif // APIMANAGER_H