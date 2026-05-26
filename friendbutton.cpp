#include "friendbutton.h"
#include "ui_friendbutton.h"

FriendButton::FriendButton(int friendId, const QString& friendName, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FriendButton)
    , m_friendId(friendId)
{
    ui->setupUi(this);

    ui->namePushButton->setText(friendName);
    
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    this->setMinimumHeight(35);

    connect(ui->namePushButton, &QPushButton::clicked, this, [this](){
        emit friendClicked(m_friendId);
    });

    connect(ui->deletePushButton, &QPushButton::clicked, this, [this](){
        emit deleteClicked(m_friendId);
    });
}

FriendButton::~FriendButton()
{
    delete ui;
}
