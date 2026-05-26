#ifndef FRIENDREQUESTITEM_H
#define FRIENDREQUESTITEM_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace Ui {
class FriendRequestItem;
}

class FriendRequestItem : public QWidget
{
    Q_OBJECT

public:
    explicit FriendRequestItem(int userId, QString name, QWidget *parent = nullptr);
    ~FriendRequestItem();

protected:
    void resizeEvent(QResizeEvent *event);

signals:
    void acceptClicked(int userId);
    void rejectClicked(int userId);

private:
    Ui::FriendRequestItem *ui;
    int m_userId;
};

#endif // FRIENDREQUESTITEM_H


