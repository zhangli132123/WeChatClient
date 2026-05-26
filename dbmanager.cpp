#include "dbmanager.h"
#include <QSqlQuery>
#include <QDebug>
#include <QSqlError>
#include <QFileInfo>

bool DBManager::initDB()
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "wechat_user");
    db.setDatabaseName("wechat_user.db");

    if (!db.open())
    {
        qDebug() << "DB open failed";
        return false;
    }

    QSqlQuery query(db);

    // 创建用户表
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE,
            password TEXT
        )
    )";

    if (!query.exec(sql))
    {
        qDebug() << "Create users table failed:" << query.lastError().text();
        return false;
    }

    // 创建本地消息缓存表（SQLite语法）
    sql = R"(
        CREATE TABLE IF NOT EXISTS local_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            from_user_id INTEGER NOT NULL,
            to_user_id INTEGER NOT NULL,
            from_username TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT NOT NULL
        )
    )";

    if (!query.exec(sql))
    {
        qDebug() << "Create local_messages table failed:" << query.lastError().text();
        return false;
    }

    // 创建索引（SQLite需要单独创建）
    sql = "CREATE INDEX IF NOT EXISTS idx_local_messages_from_to ON local_messages(from_user_id, to_user_id)";
    if (!query.exec(sql))
    {
        qDebug() << "Create idx_local_messages_from_to index failed:" << query.lastError().text();
        return false;
    }

    sql = "CREATE INDEX IF NOT EXISTS idx_local_messages_created_at ON local_messages(created_at)";
    if (!query.exec(sql))
    {
        qDebug() << "Create idx_local_messages_created_at index failed:" << query.lastError().text();
        return false;
    }

    // 创建未发送消息缓存表（SQLite语法）
    sql = R"(
        CREATE TABLE IF NOT EXISTS unsent_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            friend_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE(user_id, friend_id)
        )
    )";

    if (!query.exec(sql))
    {
        qDebug() << "Create unsent_messages table failed:" << query.lastError().text();
        return false;
    }

    // 创建索引
    sql = "CREATE INDEX IF NOT EXISTS idx_unsent_messages_user_friend ON unsent_messages(user_id, friend_id)";
    if (!query.exec(sql))
    {
        qDebug() << "Create idx_unsent_messages_user_friend index failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QSqlDatabase DBManager::getDB()
{
    return QSqlDatabase::database("wechat_user");
}