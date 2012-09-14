#include <QElapsedTimer>
#include "mainwindow.h"
#include "viewwidget.h"
#include "ui_mainwindow.h"
#include <QTcpSocket>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),m_socket(this)
{
    ui->setupUi(this);

    connect(&m_socket, SIGNAL(readyRead()), SLOT(readyRead()));
    m_socket.bind(33334);

    m_widget = new ViewWidget(this);
    ui->mainLayout->insertWidget(1, m_widget, 1);
    resize(500, 600);

    connect(this, SIGNAL(data(QByteArray)), m_widget, SLOT(processData(QByteArray)), Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::readyRead()
{
    QByteArray datagram;
    while(m_socket.hasPendingDatagrams())
    {
        datagram.resize(m_socket.pendingDatagramSize());
        m_socket.readDatagram(datagram.data(), datagram.size());

        m_widget->processData(datagram);
    }
}

void MainWindow::stateChanged(QAbstractSocket::SocketState state)
{
    ui->label->setText(QString("Server state: %1, addr: %2").arg(state).arg(m_socket.localAddress().toString()));
}
