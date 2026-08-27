#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMessageBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSqlQuery>
#include <QListWidgetItem>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#define PORT 8848

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;

private slots:
    void on_setListen_clicked();
    void on_sendmessage_clicked();

private:
    // UI
    Ui::Widget *ui;
    QTcpServer *m_s;
    QTcpSocket *m_currentClient;
    QList<QTcpSocket*> m_clients;
    QMap<QString, QTcpSocket*> m_userMap;
    QMap<QTcpSocket*, QByteArray> m_recvBuf;
    QMap<QString, QStringList> user_unrecieved_chatHistory;
    QSqlDatabase m_db;

    //yyyy/MM/dd hh:mm:ss
    // === 功能函数 ===                                 // 启动监听
    void onNewConnection();                                // 新连接处理
    void onReadyRead(QTcpSocket *client);                  // 收到数据处理
    void onDisconnected(QTcpSocket *client);               // 断开处理
    // ==================== 业务处理 ====================
    bool handleLogin(QTcpSocket *client, const QString &account,const QString &password);   // 处理登录
    void handleSend(QTcpSocket *client, const QString &receiver,
                    const QString &content,const QString timestamp);                        // 处理发送

    // ==================== 发送功能 ====================
    void sendToClient(QTcpSocket *client, const QString &msg);      // 发送消息给指定客户端
    void broadcast(const QString &msg);                             // 广播给所有客户端
    void sendError(QTcpSocket *client, const QString &err);         // 发送错误信息
 // ==================== 日志 & 界面 ====================
    void appendLog(const QString &log);
    void updateClientList();                                         // 更新界面列表
    QString getClientDisplayName(QTcpSocket *client);                // 获取客户端显示名
    QString getClientInfo(QTcpSocket *client);                       // 获取IP:端口

// ==================== 离线消息转发 ====================
    void init_db();
    void add_offline_messages(const QString &sender,const QString &content,const QString &receiver);
    bool offline_messages_query(QTcpSocket *client,const QString &name);
};

#endif // WIDGET_H