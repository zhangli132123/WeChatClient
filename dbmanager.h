#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QSqlDatabase>

class DBManager
{
public:
    static bool initDB();
    static QSqlDatabase getDB();
};

#endif // DBMANAGER_H
