#include "imserver.h"
#include <QDebug>
#include <QHostAddress>
#include <QSqlError>

ImServer::ImServer(QObject *parent)
    : QObject(parent)
{
    m_s = new QTcpServer(this);
    init_db();
    connect(m_s,&QTcpServer::newConnection,this,&ImServer::onNewConnection);
}

ImServer::~ImServer()
{
    for(auto client : m_clients)
    {
        client->disconnectFromHost();
        client->deleteLater();
    }
    m_clients.clear();
    m_userMap.clear();
}

bool ImServer::startServer()
{
    if(!m_s->listen(QHostAddress::Any,PORT))
    {
        qCritical()<<"监听失败:"<<m_s->errorString();
        return false;
    }
    qInfo()<<"✅ IM服务启动成功，监听端口:"<<PORT;
    return true;
}

bool ImServer::init_db()
{
    m_db= QSqlDatabase::addDatabase("QMYSQL");
    m_db.setHostName("127.0.0.1");
    m_db.setPort(3306);
    m_db.setUserName("root");
    m_db.setPassword("root");
    m_db.setDatabaseName("offline_messages_db");

    if (!m_db.open()) {
        qCritical()<<"❌数据库连接失败:"<< m_db.lastError().text();
        return false;
    }
    qInfo()<<"✅数据库连接成功";
    return true;
}

void ImServer::onNewConnection()
{
    QTcpSocket *client = m_s->nextPendingConnection();
    m_clients.append(client);
    qInfo()<<"🔗新连接："<<getClientInfo(client);

    connect(client,&QTcpSocket::readyRead,this,[this,client](){
        onReadyRead(client);
    });
    connect(client,&QTcpSocket::disconnected,this,[this,client](){
        onDisconnected(client);
    });
    connect(client,&QTcpSocket::errorOccurred,this,[this,client](auto){
        qWarning()<<"⚠️客户端异常断开 "<<getClientInfo(client)<<" :"<<client->errorString();
        client->disconnectFromHost();
    });
}

void ImServer::onReadyRead(QTcpSocket *client)
{
    m_recvBuf[client].append(client->readAll());
    while(m_recvBuf[client].contains('\n'))
    {
        int pos = m_recvBuf[client].indexOf('\n');
        QByteArray onePacket = m_recvBuf[client].left(pos);
        m_recvBuf[client].remove(0, pos+1);
        QString text = QString::fromUtf8(onePacket).trimmed();
        if(text.isEmpty()) continue;

        const QString timestamp=QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
        QStringList parts = text.split('\t');
        if (parts.isEmpty()) continue;

        if (parts[0] == "LOGIN:" && parts.size() >= 3) {
            handleLogin(client, parts[1],parts[2]);
        }
        else if (parts[0] == "SEND:" && parts.size() >= 3) {
            handleSend(client, parts[1], parts[2],timestamp);
        }
    }
}

void ImServer::onDisconnected(QTcpSocket *client)
{
    m_recvBuf.remove(client);
    QString account = m_userMap.key(client, "");
    if (!account.isEmpty()) {
        m_userMap.remove(account);
        qInfo()<<"❌用户下线："<<account;
    }
    m_clients.removeOne(client);
    client->deleteLater();
}

bool ImServer::handleLogin(QTcpSocket *client, const QString &account, const QString &password)
{
    Q_UNUSED(password);
    if (account.isEmpty()) {
        sendError(client, "账号不能为空");
        return false;
    }
    if (m_userMap.contains(account)) {
        sendError(client, "账号已在线");
        return false;
    }

    m_userMap[account] = client;
    sendToClient(client, "LOGIN_OK");
    offline_messages_query(client,account);

    qInfo()<<"✅用户登录成功："<<account;
    return true;
}

void ImServer::handleSend(QTcpSocket *client, const QString &receiver, const QString &content, const QString timestamp)
{
    QString sender = m_userMap.key(client, "");
    if (sender.isEmpty()) {
        sendError(client, "请先登录");
        return;
    }

    if (!m_userMap.contains(receiver)) {
        qInfo()<<"💾消息存入离线消息 "<<sender<<" → "<<receiver;
        add_offline_messages(sender,content,receiver);
        return;
    }

    QTcpSocket *target = m_userMap[receiver];
    QString forwardMsg = QString("MSG\t%1\t%2\t%3").arg(sender).arg(content).arg(timestamp);
    sendToClient(target, forwardMsg);
    sendToClient(client, "SEND_OK");
    qInfo()<<"📨转发消息 "<<sender<<" → "<<receiver<<" : "<<content;
}

void ImServer::add_offline_messages(const QString &sender, const QString &content, const QString &receiver)
{
    QDateTime dt=QDateTime::currentDateTime();
    QString time=dt.toString("yyyy/MM/dd hh:mm:ss");
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO offline_messages(sender,receiver,content,timestamp)"
                  "VALUES(:sender,:receiver,:content,:timestamp)");
    query.bindValue(":sender",sender);
    query.bindValue(":receiver",receiver);
    query.bindValue(":content",content);
    query.bindValue(":timestamp",time);
    query.exec();
}

bool ImServer::offline_messages_query(QTcpSocket *client, const QString &account)
{
    QSqlQuery query(m_db);
    QSqlQuery update(m_db);
    query.prepare("SELECT sender,content,timestamp,id FROM offline_messages WHERE is_sent=0 AND receiver = :account ORDER BY id ASC");
    query.bindValue(":account",account);
    if(query.exec()){
        while(query.next()){
            QString sender=query.value(0).toString();
            QString content=query.value(1).toString();
            QDateTime time=query.value(2).toDateTime();
            int msgId=query.value(3).toInt();
            QString timestamp=time.toString("yyyy/MM/dd hh:mm:ss");
            QString msg="OFFLINE_MSG\t"+sender+'\t'+content+'\t'+timestamp;
            sendToClient(client,msg);

            update.prepare("UPDATE offline_messages SET is_sent=1 WHERE is_sent=0 AND receiver = :account AND id= :msgId");
            update.bindValue(":msgId",msgId);
            update.bindValue(":account",account);
            update.exec();
        }
        sendToClient(client, "OFFLINE_MSG_END");
        return true;
    }
    return false;
}

void ImServer::sendToClient(QTcpSocket *client, const QString &msg)
{
    if (client && client->state() == QAbstractSocket::ConnectedState) {
        client->write((msg + "\n").toUtf8());
    }
}

void ImServer::broadcast(const QString &msg)
{
    for (auto client : m_clients) {
        sendToClient(client, msg);
    }
}

void ImServer::sendError(QTcpSocket *client, const QString &err)
{
    sendToClient(client, "ERROR\t" + err);
}

QString ImServer::getClientInfo(QTcpSocket *client)
{
    return QString("%1:%2")
    .arg(client->peerAddress().toString())
        .arg(client->peerPort());
}
