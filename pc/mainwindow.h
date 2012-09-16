#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket>
#include <QTcpServer>

namespace Ui {
    class MainWindow;
}

class ViewWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
Q_SIGNALS:
    void data(QByteArray data);

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    
private slots:
    void readyReadUdp();
    void readyReadTcp();
    void addConn();
    void listen(int port);
    void tcpDisconnected();

private:
    Ui::MainWindow *ui;
    QUdpSocket m_socket;
    QTcpServer m_server;
    ViewWidget *m_widget;
};

#endif // MAINWINDOW_H
