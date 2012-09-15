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
    ui->area->setWidget(m_widget);
    resize(500, 600);

    connect(this, SIGNAL(data(QByteArray)), m_widget, SLOT(processData(QByteArray)), Qt::QueuedConnection);
    connect(ui->scaleRadio, SIGNAL(toggled(bool)),     m_widget, SLOT(setResize(bool)));
    connect(ui->wBox,       SIGNAL(valueChanged(int)), m_widget, SLOT(setResX(int)));
    connect(ui->hBox,       SIGNAL(valueChanged(int)), m_widget, SLOT(setResY(int)));
    connect(ui->rotBox,     SIGNAL(valueChanged(int)), m_widget, SLOT(setRotation(int)));
    connect(ui->rotLeftBtn, SIGNAL(clicked()),         m_widget, SLOT(rotateLeft()));
    connect(ui->rotRightBtn,SIGNAL(clicked()),         m_widget, SLOT(rotateRight()));

    connect(m_widget, SIGNAL(rotChanged(int)), ui->rotBox, SLOT(setValue(int)));
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
