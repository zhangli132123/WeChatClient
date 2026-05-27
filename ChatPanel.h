#ifndef CHATPANEL_H
#define CHATPANEL_H

#include "userinfo.h"
#include "apimanager.h"
#include <QWidget>
#include <QJsonArray>
#include <QKeyEvent>
#include <QDateTime>

namespace Ui {
class ChatPanel;
}

struct ChatMessage {
    int id;
    int fromUserId;
    int toUserId;
    QString fromUsername;
    QString content;
    QDateTime createdAt;
};

class ChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPanel(QWidget *parent = nullptr);
    void setUser(const UserInfo &userInfo);
    ~ChatPanel();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void on_friendRequestPushButton_clicked();
    void on_addFriendButton_clicked();
    void onFriendsReceived(const QJsonArray& friends);
    void onFriendRemoved();
    void onRemoveFriendFailed(const QString& error);
    void onFriendButtonClicked(int friendId);
    void onDeleteButtonClicked(int friendId);
    void onFriendRequestSent();
    void onFriendRequestFailed(const QString& error);
    
    // 消息相关槽函数
    void onMessageSent(const QJsonObject& message);
    void onMessageSendFailed(const QString& error);
    void onMessagesReceived(const QJsonArray& messages);
    void onMessagesGetFailed(const QString& error);
    
    // 滑动加载
    void onScrollBarValueChanged(int value);
    
    // WebSocket 相关槽函数
    void onNewMessageReceived(const QJsonObject& message);
    void onOfflineMessagesReceived(const QJsonArray& messages);
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketError(const QString& error);

private:
    Ui::ChatPanel *ui;
    UserInfo m_currentUser;
    ApiManager* m_apiManager;
    QMap<int, QString> m_friendsMap;
    
    // 聊天状态
    int m_currentFriendId;
    QString m_currentFriendName;
    int m_messageOffset;
    bool m_isLoadingMessages;
    bool m_isFirstLoad;  // 是否是首次加载（获取更新消息）

    QSet<int> m_loadedMessageIds;  // 已加载的消息ID集合，用于服务器消息去重
    QDateTime m_lastLoadedTime;  // 最后加载消息的时间，用于时间戳分页
    QDateTime m_lastMessageTime;  // 当前聊天的最后消息时间（用于时间标签判断）
    
    void loadFriends();
    void addFriendButton(int friendId, const QString& friendName);
    void sendMessage();
    void loadMessages(const QString& beforeTime = QString(), const QString& afterTime = QString());
    void addMessageToLayout(const ChatMessage& msg);
    void addTimeLabel(const QDateTime& time, bool insertAtBeginning = false);
    void deleteLocalMessages(int friendId);
    void clearChatHistory();
    bool shouldShowTimeLabel(const QDateTime& currentTime);
    QDateTime getLatestMessageTimeFromLayout();  // 从界面获取最新消息时间
    void saveMessageToLocal(const ChatMessage& msg);
    QList<ChatMessage> loadMessagesFromLocal(int friendId);
    void autoLoadHistoryMessages();  // 自动加载历史消息直到出现滚动条
    void saveUnsentMessage(int friendId, const QString& content);
    QString loadUnsentMessage(int friendId);
    void deleteUnsentMessage(int friendId);
};

#endif // CHATPANEL_H