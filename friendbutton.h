#ifndef FRIENDBUTTON_H
#define FRIENDBUTTON_H

#include <QWidget>

namespace Ui {
class FriendButton;
}

class FriendButton : public QWidget
{
    Q_OBJECT

public:
    explicit FriendButton(int friendId, const QString& friendName, QWidget *parent = nullptr);
    ~FriendButton();

signals:
    void friendClicked(int friendId);
    void deleteClicked(int friendId);

private:
    Ui::FriendButton *ui;

    int m_friendId;
};

#endif // FRIENDBUTTON_H
