#include "LoginPanel.h"
#include "ui_LoginPanel.h"

LoginPanel::LoginPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginPanel)
{
    ui->setupUi(this);
    userName = "";
    password = "";
    ui->userNameLineEdit->setText(userName);
    ui->passwordLineEdit->setText(password);
    
    // 获取全局唯一的API管理器实例
    m_apiManager = ApiManager::instance();
    
    // 连接API信号
    connect(m_apiManager, &ApiManager::loginSuccess, 
            this, &LoginPanel::onLoginSuccess);
    connect(m_apiManager, &ApiManager::loginFailed, 
            this, &LoginPanel::onLoginFailed);
    connect(m_apiManager, &ApiManager::registerSuccess, 
            this, &LoginPanel::onRegisterSuccess);
    connect(m_apiManager, &ApiManager::registerFailed, 
            this, &LoginPanel::onRegisterFailed);
}

LoginPanel::~LoginPanel()
{
    delete ui;
    // m_apiManager 是全局单例，不需要在这里删除
}


void LoginPanel::on_registPushButton_clicked()
{
    ui->stackedWidget->setCurrentIndex(1);
}


void LoginPanel::on_registCommitPushButton_clicked()
{
    userName = ui->userNameLineEdit_2->text().trimmed();
    password = ui->passwordLineEdit_2->text().trimmed();
    
    if (userName.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "Error", "用户名或密码不能为空");
        return;
    }
    
    // 调用服务端注册API
    m_apiManager->registerUser(userName, password);
}


void LoginPanel::on_loginPushButton_clicked()
{
    userName = ui->userNameLineEdit->text().trimmed();
    password = ui->passwordLineEdit->text().trimmed();

    if (userName.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "Error", "用户名或密码不能为空");
        return;
    }

    // 调用服务端登录API
    m_apiManager->login(userName, password);
}


void LoginPanel::on_backPushButton_clicked()
{
    ui->stackedWidget->setCurrentIndex(0);
    ui->userNameLineEdit->clear();
    ui->passwordLineEdit->clear();
    ui->userNameLineEdit_2->clear();
    ui->passwordLineEdit_2->clear();
}

// API登录成功回调
void LoginPanel::onLoginSuccess(const QJsonObject& userData)
{
    QMessageBox::information(this, "OK", "登录成功");
    
    // 解析用户信息
    m_currentUser.id = userData["id"].toInt();
    m_currentUser.username = userData["username"].toString();
    
    qDebug() << "LOGIN USER ID:" << m_currentUser.id;
    qDebug() << "LOGIN USER NAME:" << m_currentUser.username;
    
    // 发射登录成功信号
    emit loginSuccess(m_currentUser);
}

// API登录失败回调
void LoginPanel::onLoginFailed(const QString& error)
{
    QMessageBox::warning(this, "Error", error);
}

// API注册成功回调
void LoginPanel::onRegisterSuccess()
{
    QMessageBox::information(this, "OK", "注册成功");
    ui->stackedWidget->setCurrentIndex(0);
    ui->userNameLineEdit->setText(userName);
    ui->passwordLineEdit->setText(password);
}

// API注册失败回调
void LoginPanel::onRegisterFailed(const QString& error)
{
    QMessageBox::warning(this, "Error", error);
}

