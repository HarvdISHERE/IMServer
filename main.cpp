#include <QCoreApplication>
#include "imserver.h"
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    ImServer srv;
    if(!srv.startServer())
    {
        qCritical()<<"服务启动失败，退出程序";
        return -1;
    }

    return a.exec();
}
