#ifndef USER_H
#define USER_H
#include <QString>
class m_friend{
    QString name;
    QString account;
    m_friend(QString name,QString account):name(name),account(account){}
};

class user{
    QString name;
    QString account;
    QString password;
    QString ip;
    QString port;
    bool online;
    QList<m_friend> *fd;
};

class message{
    QString sender;
    QString receiver;
    QString msg;
    message(QString sender,QString receiver,QString message):sender(sender),receiver(receiver),msg(message){}
    QString mesg() {    // 格式化显示
        return sender + ": " + msg;
    }
};

bool sendMessage();
#endif // USER_H
