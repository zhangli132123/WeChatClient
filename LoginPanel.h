#ifndef LOGINPANEL_H
#define LOGINPANEL_H

#include <QWidget>
#include "apimanager.h"
#include "userinfo.h"
#include <QMessageBox>
#include <QJsonObject>


QT_BEGIN_NAMESPACE
namespace Ui {
class LoginPanel;
}
QT_END_NAMESPACE

class LoginPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPanel(QWidget *parent = nullptr);
    ~LoginPanel() override;

private slots:
    void on_registPushButton_clicked();
    void on_registCommitPushButton_clicked();
    void on_loginPushButton_clicked();
    
    // API回调
    void onLoginSuccess(const QJsonObject& userData);
    void onLoginFailed(const QString& error);
    void onRegisterSuccess();
    void onRegisterFailed(const QString& error);
    void on_backPushButton_clicked();

private:
    Ui::LoginPanel *ui;
    QString userName;
    QString password;
    ApiManager* m_apiManager;
    UserInfo m_currentUser;

signals:
    void loginSuccess(UserInfo user);
};
#endif // LOGINPANEL_H
