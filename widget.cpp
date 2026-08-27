#include "widget.h"
#include "ui_widget.h"

// ==================== 构造函数 ====================
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_s(new QTcpServer(this))
    , m_currentClient(nullptr)
{
    ui->setupUi(this);
    setWindowTitle("服务器");
    ui->port->setText(QString::number(PORT));
    init_db();
    ui->connection_listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // 新连接信号
    connect(m_s, &QTcpServer::newConnection, this, &Widget::onNewConnection);
}

Widget::~Widget()
{
    for (auto client : m_clients) {
        client->disconnectFromHost();
        client->deleteLater();
    }
    m_clients.clear();
    m_userMap.clear();
    delete ui;
}



void Widget::init_db(){
    m_db= QSqlDatabase::addDatabase("QMYSQL");
    m_db.setHostName("127.0.0.1");
    m_db.setPort(3306);
    m_db.setUserName("root");
    m_db.setPassword("root");
    m_db.setDatabaseName("offline_messages_db");

    if (!m_db.open()) {
        QMessageBox::critical(this, "数据库错误",
                              "无法连接MySQL数据库：" + m_db.lastError().text());
        return;
    }

    qDebug() << "✅ 数据库连接成功";
}


// ==================== 新连接处理 ====================
void Widget::onNewConnection()
{
    QTcpSocket *client = m_s->nextPendingConnection();
    m_clients.append(client);
    appendLog(QString("🔗 新连接: %1 (等待登录...)").arg(getClientInfo(client)));

    // 连接信号
    connect(client, &QTcpSocket::readyRead, this, [this, client]() {
        onReadyRead(client);
    });
    connect(client, &QTcpSocket::disconnected, this, [this, client]() {
        onDisconnected(client);
    });
    connect(client, &QTcpSocket::errorOccurred, this, [this, client](QAbstractSocket::SocketError){
        appendLog(QString("⚠️客户端异常断开：%1").arg(client->errorString()));
        client->disconnectFromHost();
    });
}

// ==================== 收到数据分类处理 ====================
void Widget::onReadyRead(QTcpSocket *client)
{
    //追加新读到的数据到缓冲区
    m_recvBuf[client].append(client->readAll());

    //循环切出一条条完整消息
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

// ==================== 断开处理 ====================
void Widget::onDisconnected(QTcpSocket *client)
{
    m_recvBuf.remove(client);
    QString account = m_userMap.key(client, "");
    if (!account.isEmpty()) {
        m_userMap.remove(account);
        appendLog(QString("❌ 用户 [%1] 已下线").arg(account));
    }

    m_clients.removeOne(client);
    updateClientList();

    if (m_currentClient == client) {
        m_currentClient = m_clients.isEmpty() ? nullptr : m_clients.last();
    }

    client->deleteLater();
}

// ==================== 处理登录 ====================
bool Widget::handleLogin(QTcpSocket *client, const QString &account,const QString &password)
{
    // 检查账号是否为空
    if (account.isEmpty()) {
        sendError(client, "账号不能为空");
        return false;
    }

    // 检查账号是否已在线
    if (m_userMap.contains(account)) {
        sendError(client, "账号已在线");
        return false;
    }

    //----------------

    //进行数据库查询

    //----------------

    // 记录登录信息
    m_userMap[account] = client;
    m_currentClient = client;

    // 发送成功回执
    sendToClient(client, "LOGIN_OK");
    offline_messages_query(client,account);

    // 更新界面
    updateClientList();
    appendLog(QString("✅ 用户 [%1] 登录成功").arg(account));

    return true;
}

// ==================== 处理发送 ====================
void Widget::handleSend(QTcpSocket *client, const QString &receiver, const QString &content,const QString timestamp)
{
    // 检查发送者是否已登录
    QString sender = m_userMap.key(client, "");
    if (sender.isEmpty()) {
        sendError(client, "请先登录");
        return;
    }

    // 检查接收者是否在线
    if (!m_userMap.contains(receiver)) {

        appendLog(QString("💾 [%1] → [%2]: 用户不在线").arg(sender).arg(receiver));
        add_offline_messages(sender,content,receiver);
        return;
    }

    // 转发消息

    QTcpSocket *target = m_userMap[receiver];
    QString forwardMsg = QString("MSG\t%1\t%2\t%3").arg(sender).arg(content).arg(timestamp);
    sendToClient(target, forwardMsg);

    // 发送成功回执
    sendToClient(client, "SEND_OK");

    // 日志
    appendLog(QString("📨 [%1] → [%2]: %3").arg(sender).arg(receiver).arg(content));
}


void Widget::add_offline_messages(const QString &sender,const QString &content,const QString &receiver){
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

bool Widget::offline_messages_query(QTcpSocket *client,const QString &account){
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


void Widget::sendToClient(QTcpSocket *client, const QString &msg)
{
    if (client && client->state() == QAbstractSocket::ConnectedState) {
        client->write((msg + "\n").toUtf8());
    }
}
// ==================== 广播 ====================
void Widget::broadcast(const QString &msg)
{
    for (auto client : m_clients) {
        sendToClient(client, msg);
    }
}

// ==================== 发送错误 ====================
void Widget::sendError(QTcpSocket *client, const QString &err)
{
    sendToClient(client, "ERROR\t" + err);
}

// ==================== 添加日志 ====================
void Widget::appendLog(const QString &log)
{
    ui->history->appendPlainText(log);
}

// ==================== 设置监听按钮 ====================
void Widget::on_setListen_clicked()
{
    unsigned short port = ui->port->text().toUShort();
    if (!m_s->listen(QHostAddress::Any, port)) {
        QMessageBox::critical(this, "错误", "监听失败");
        return;
    }
    ui->setListen->setDisabled(true);
    appendLog(QString("🟢 服务器已启动，监听端口: %1").arg(port));
}

// ==================== 发送消息按钮 ====================
void Widget::on_sendmessage_clicked()
{
    QString msg = ui->message->toPlainText();
    if (msg.isEmpty()) {
        QMessageBox::information(this, "提示", "请输入发送信息");
        return;
    }

    if (m_clients.isEmpty()) {
        QMessageBox::warning(this, "提示", "没有客户端连接");
        return;
    }

    QList<QListWidgetItem*> selectedItems = ui->connection_listWidget->selectedItems();

    if (!selectedItems.isEmpty())
    {
        // 有选中 → 单独发送
        QStringList targets;
        for(auto item:selectedItems){
            QTcpSocket *target = item->data(Qt::UserRole).value<QTcpSocket*>();
            if(!target) continue;
            if (target->state() == QAbstractSocket::ConnectedState) {
                sendToClient(target, msg);
                appendLog(QString("📩 单独发送给 %1: %2")
                              .arg(getClientDisplayName(target)).arg(msg));
            } else {
                QMessageBox::warning(this, "提示", "该客户端已断开连接");
            }
        }
    }
    else {
        // 无选中 → 广播
        broadcast(msg);
        appendLog(QString("📢 广播给所有客户端(%1个): %2")
                      .arg(m_clients.size()).arg(msg));
    }

    ui->message->clear();
    }

// ==================== 更新客户端列表 ====================
void Widget::updateClientList()
{
    ui->connection_listWidget->clear();

    for (int i = 0; i < m_clients.size(); ++i) {
        QTcpSocket *client = m_clients[i];
        QString displayName = getClientDisplayName(client);

        QString status;
        switch (client->state()) {
        case QAbstractSocket::ConnectedState: status = "🟢"; break;
        case QAbstractSocket::ConnectingState: status = "🟡"; break;
        default:                               status = "🔴"; break;
        }

        QListWidgetItem* item = new QListWidgetItem(
            QString("%1 [%2] %3").arg(status).arg(i).arg(displayName));
        //绑定socket指针
        item->setData(Qt::UserRole, QVariant::fromValue(client));
        ui->connection_listWidget->addItem(item);
    }
}

// ==================== 获取客户端显示名 ====================
QString Widget::getClientDisplayName(QTcpSocket *client)
{
    QString account = m_userMap.key(client, "未登录");
    QString info = getClientInfo(client);
    return QString("%1 (%2)").arg(account).arg(info);
}

// ==================== 获取客户端IP:端口 ====================
QString Widget::getClientInfo(QTcpSocket *client)
{
    return QString("%1:%2")
    .arg(client->peerAddress().toString())
        .arg(client->peerPort());
}