#ifndef MESSAGEITEM_H
#define MESSAGEITEM_H

#include <QWidget>

namespace Ui {
class MessageItem;
}

class MessageItem : public QWidget
{
    Q_OBJECT

public:
    explicit MessageItem(const QString &name, QDateTime time, const QString &text, QWidget *parent = nullptr);
    ~MessageItem();

public:
    void setText(const QString &text);

private:
    Ui::MessageItem *ui;
};

#endif // MESSAGEITEM_H
