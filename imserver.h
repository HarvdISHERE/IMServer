#ifndef IMSERVER_H
#define IMSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QByteArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>

#define PORT 8848

class ImServer : public QObject
{
    Q_OBJECT
public:
    explicit ImServer(QObject *parent = nullptr);
    ~ImServer() override;

    bool startServer();

private:
    QTcpServer* m_s = nullptr;
    QList<QTcpSocket*> m_clients;
    QMap<QString, QTcpSocket*> m_userMap;   // account -> socket
    QMap<QTcpSocket*, QByteArray> m_recvBuf;
    QSqlDatabase m_db;

    void onNewConnection();
    void onReadyRead(QTcpSocket* client);
    void onDisconnected(QTcpSocket* client);

    bool handleLogin(QTcpSocket *client, const QString &account,const QString &password);
    void handleSend(QTcpSocket *client, const QString &receiver, const QString &content,const QString timestamp);

    bool init_db();
    void add_offline_messages(const QString &sender,const QString &content,const QString &receiver);
    bool offline_messages_query(QTcpSocket *client,const QString &account);

    void sendToClient(QTcpSocket *client, const QString &msg);
    void broadcast(const QString &msg);
    void sendError(QTcpSocket *client, const QString &err);
    QString getClientInfo(QTcpSocket *client);
};

#endif // IMSERVER_H
