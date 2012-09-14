#include <QPainter>
#include <QElapsedTimer>
#include <QTimer>
#include <QDateTime>
#include "viewwidget.h"

ViewWidget::ViewWidget(QWidget *parent) :
    QWidget(parent)
{
    setFixedSize(320, 480);
    m_fps = 0;
    m_frames = 0;

    QTimer *t = new QTimer(this);
    t->start(1000);
    connect(t, SIGNAL(timeout()), SLOT(fpsTime()));
}

void ViewWidget::processData(QByteArray data)
{
    for(int i = 0; i < data.size();)
    {
        if(m_readPkt.empty)
        {
            int idx = data.indexOf("FRAM\n", i);
            if(idx != -1 && idx+6 < data.size())
            {
                m_readPkt.empty = false;
                data.remove(0, idx+6);
                i = 0;
                continue;
            }
            return;
        }

        switch(m_readPkt.pos)
        {
            case 0:
                m_readPkt.len = quint8(data[i++]) << 24;
                ++m_readPkt.pos;
                continue;
            case 1:
                m_readPkt.len |= quint8(data[i++]) << 16;
                ++m_readPkt.pos;
                continue;
            case 2:
                m_readPkt.len |= quint8(data[i++]) << 8;
                ++m_readPkt.pos;
                continue;
            case 3:
            {
                m_readPkt.len |= quint8(data[i++]);
                m_readPkt.len += 4;
                m_readPkt.initLen(m_readPkt.len);
                m_readPkt.pos = 4;
                continue;
            }
            default:
            {
                *m_readPkt.dataItr = (quint8)data[i++];
                ++m_readPkt.dataItr;
                break;
            }
        }

        if(m_readPkt.dataItr >= m_readPkt.data+m_readPkt.len)
        {
            QByteArray frame = qUncompress(m_readPkt.data, m_readPkt.len);
            if(!frame.isEmpty())
                m_draw = frame;

            m_readPkt.initLen(0);

            ++m_frames;
            update();
            break;
        }
    }
}

void ViewWidget::fpsTime()
{
    static qint64 last = QDateTime::currentMSecsSinceEpoch();
    qint64 now = QDateTime::currentMSecsSinceEpoch();;
    m_fps = float(m_frames)/(float(now-last)/1000);
    m_frames = 0;
    last = now;
    update();
}

void ViewWidget::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);
    if(!m_draw.isEmpty())
        p.drawImage(0, 0, QImage((uchar*)m_draw.data(), 320, 480, QImage::Format_RGB16));
    p.setPen(Qt::white);
    p.drawText(0, 0, 50, 50, 0, QString::number(m_fps, 'f'));
}
