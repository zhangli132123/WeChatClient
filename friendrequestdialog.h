#ifndef FRIENDREQUESTDIALOG_H
#define FRIENDREQUESTDIALOG_H

#include <QDialog>
#include "apimanager.h"

namespace Ui {
class FriendRequestDialog;
}

class FriendRequestDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FriendRequestDialog(QWidget *parent = nullptr, int userId = 0);
    ~FriendRequestDialog();
    
    void loadRequests();

private slots:
    void onFriendRequestsReceived(const QJsonArray& requests);
    void onFriendRequestHandled(const QString& message);
    void onAgreeButtonClicked(int requestId);
    void onDisagreeButtonClicked(int requestId);

signals:
    void friendListChanged();

private:
    Ui::FriendRequestDialog *ui;
    ApiManager* m_apiManager;
    int m_currentUserId;
};

#endif // FRIENDREQUESTDIALOG_H