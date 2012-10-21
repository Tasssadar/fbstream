#include <QElapsedTimer>
#include <QDialog>
#include <QHBoxLayout>
#include <QDesktopWidget>
#include <QApplication>
#include "mainwindow.h"
#include "viewwidget.h"
#include "ui_mainwindow.h"
#include <QTcpSocket>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),m_socket(this), m_server(this)
{
    ui->setupUi(this);

    connect(&m_socket, SIGNAL(readyRead()), SLOT(readyReadUdp()));
    connect(&m_server, SIGNAL(newConnection()), SLOT(addConn()));
    listen(33334);

    m_widget = new ViewWidget(this);
    m_fullWindow = NULL;

    ui->area->setWidget(m_widget);
    resize(500, 600);

    connect(this, SIGNAL(data(QByteArray)), m_widget, SLOT(processData(QByteArray)), Qt::QueuedConnection);
    connect(ui->scaleRadio, SIGNAL(toggled(bool)),     m_widget, SLOT(setResize(bool)));
    connect(ui->wBox,       SIGNAL(valueChanged(int)), m_widget, SLOT(setResX(int)));
    connect(ui->hBox,       SIGNAL(valueChanged(int)), m_widget, SLOT(setResY(int)));
    connect(ui->rotBox,     SIGNAL(valueChanged(int)), m_widget, SLOT(setRotation(int)));
    connect(ui->rotLeftBtn, SIGNAL(clicked()),         m_widget, SLOT(rotateLeft()));
    connect(ui->rotRightBtn,SIGNAL(clicked()),         m_widget, SLOT(rotateRight()));
    connect(ui->fullScreenBtn, SIGNAL(clicked()),      SLOT(enterFullscreen()));
    connect(m_widget,       SIGNAL(leaveFullscreen()), SLOT(leaveFullscreen()));
    connect(ui->portBox,    SIGNAL(valueChanged(int)), SLOT(listen(int)));

    connect(m_widget, SIGNAL(rotChanged(int)), ui->rotBox, SLOT(setValue(int)));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::readyReadUdp()
{
    QByteArray datagram;
    while(m_socket.hasPendingDatagrams())
    {
        datagram.resize(m_socket.pendingDatagramSize());
        m_socket.readDatagram(datagram.data(), datagram.size());

        m_widget->processData(datagram);
    }
}

void MainWindow::readyReadTcp()
{
    QTcpSocket *s = (QTcpSocket*)sender();
    m_widget->processData(s->readAll());
}

void MainWindow::tcpDisconnected()
{
    sender()->deleteLater();
}

void MainWindow::addConn()
{
    while(QTcpSocket *s = m_server.nextPendingConnection())
    {
        connect(s, SIGNAL(readyRead()), SLOT(readyReadTcp()));
        connect(s, SIGNAL(disconnected()), SLOT(tcpDisconnected()));
    }
}

void MainWindow::listen(int port)
{
    m_socket.close();
    if(!m_socket.bind(port))
        qWarning("UDP: Failed to bind on port %d ", port);

    m_server.close();
    if(!m_server.listen(QHostAddress::Any, port))
         qWarning("TCP: Failed to listen on port %d ", port);
}

void MainWindow::enterFullscreen()
{
    ui->scaleRadio->setChecked(true);

    m_fullWindow = new QDialog(this);

    ui->area->takeWidget();

    QPalette p = m_widget->palette();
    p.setColor(QPalette::Window, Qt::black);
    m_widget->setPalette(p);

    QHBoxLayout *l = new QHBoxLayout(m_fullWindow);
    l->addWidget(m_widget);
    l->setSpacing(0);
    l->setMargin(0);
    m_fullWindow->resize(QApplication::desktop()->screenGeometry().size());
    m_fullWindow->showFullScreen();
}

void MainWindow::leaveFullscreen()
{
    if(!m_fullWindow)
        return enterFullscreen();

    m_widget->setPalette(QApplication::palette());

    m_fullWindow->showNormal();
    ui->area->setWidget(m_widget);
    delete m_fullWindow;
    m_fullWindow = NULL;
}
