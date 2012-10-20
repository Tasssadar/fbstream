#include <QPainter>
#include <QElapsedTimer>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <math.h>
#include "viewwidget.h"

#define RAD(x) (x*(M_PI/180))
#define LEN_TRESHOLD (800*1280*4)

ViewWidget::ViewWidget(QWidget *parent) :
    QWidget(parent)
{
    m_fps = 0;
    m_frames = 0;
    m_resize = false;
    m_avgFps = 0;
    m_framesAvg = 0;
    m_resX = 800;
    m_resY = 1280;
    m_rot = 0;

    memset(&m_ren.rot, 0, sizeof(m_ren));

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QTimer *t = new QTimer(this);
    t->start(500);
    connect(t, SIGNAL(timeout()), SLOT(fpsTime()));

    t = new QTimer(this);
    t->start(5000);
    connect(t, SIGNAL(timeout()), SLOT(fpsAvg()));

    // to init counter
    fpsTime();
    fpsAvg();

    setMinimumSizeRot();
}

void ViewWidget::processData(QByteArray data)
{
    for(int i = 0; i < data.size();)
    {
        // wait for header
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

        // read len
        for(;m_readPkt.pos < 4; ++m_readPkt.pos)
            m_readPkt.len |= quint8(data[i++]) << (m_readPkt.pos)*8;


        switch(m_readPkt.pos)
        {
            // set len
            case 4:
            {
                if(m_readPkt.len >= LEN_TRESHOLD)
                {
                    m_readPkt.initLen(0);
                    continue;
                }

                m_readPkt.initLen(m_readPkt.len);
                m_readPkt.pos = 5;

                break;
            }
            // receive data
            case 5:
            {
                uchar *end = m_readPkt.data+m_readPkt.len;
                for(; i < data.size() && m_readPkt.dataItr != end; ++i)
                {
                    *m_readPkt.dataItr++ = (quint8)data[i];
                }

                // frame complete
                if(m_readPkt.dataItr == end)
                {
                    QByteArray frame((char*)m_readPkt.data, m_readPkt.len);
                    if(!frame.isEmpty())
                    {
                        m_draw = frame;
                        ++m_frames;
                        ++m_framesAvg;
                        update();
                    }

                    m_readPkt.initLen(0);
                }
                break;
            }
        }
    }
}

void ViewWidget::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);

    if(!m_draw.isEmpty())
    {
        p.save();
        QImage i = QImage::fromData(m_draw, "jpg");

        if(m_resize)
        {
            i = i.scaled(m_ren.scaleW, m_ren.scaleH, Qt::KeepAspectRatio,Qt::SmoothTransformation);
            p.translate(m_ren.centerX, m_ren.centerY);
        }
        p.rotate(m_ren.rot);
        p.translate(m_ren.transX, m_ren.transY);
        p.drawImage(0, 0, i);
        p.restore();
    }
    else
    {
        p.drawText(rect(), Qt::AlignCenter, "Waiting for the first frame...");
        return;
    }

    p.setBrush(QBrush(Qt::SolidPattern));
    p.drawRect(0, 0, 35, 30);
    p.setPen(Qt::white);
    p.drawText(0, 0, 35, 15, Qt::AlignHCenter, QString::number(m_fps, 'f', 1));
    p.drawText(0, 15, 35, 15, Qt::AlignHCenter, QString::number(m_avgFps, 'f', 1));
}

void ViewWidget::setResize(bool resize)
{
    m_resize = resize;
    if(!m_resize)
        setMinimumSizeRot();
    else
    {
        setMinimumSize(0, 0);
        setMaximumSize(16777215, 16777215);
    }
    updateRenderData();
}

void ViewWidget::setResX(int res)
{
    m_resX = res;
    if(!m_resize)
        setMinimumSizeRot();
    updateRenderData();
}

void ViewWidget::setResY(int res)
{
    m_resY = res;
    if(!m_resize)
        setMinimumSizeRot();
    updateRenderData();
}

void ViewWidget::setRotation(int rot)
{
    m_rot = rot;
    if(m_rot <= -360)
        m_rot = 360 + m_rot;
    else if(m_rot >= 360)
        m_rot -= 360;

    if(!m_resize)
        setMinimumSizeRot();
    updateRenderData();

    emit rotChanged(m_rot);
}

void ViewWidget::setMinimumSizeRot()
{
    float ang = RAD(m_rot);
    int sw = abs(cos(ang)*m_resX + sin(ang)*m_resY);
    int sh = abs(sin(ang)*m_resX + cos(ang)*m_resY);

    setFixedSize(sw, sh);
}

void ViewWidget::updateRenderData()
{
    if(m_draw.isEmpty())
        return;

    QImage i = QImage((uchar*)m_draw.data(), m_resX, m_resY, QImage::Format_RGB16);

    int rot = m_rot;
    while(rot < 0) rot = 360 + rot;
    while(rot >= 360) rot -= 360;

    float ang = (rot*(M_PI/180));
    m_ren.rot = rot;

    if(m_resize)
    {
        int sw = abs(cos(ang)*width() + sin(ang)*height());
        int sh = abs(sin(ang)*width() + cos(ang)*height());
        m_ren.scaleW = sw;
        m_ren.scaleH = sh;
        i = i.scaled(sw, sh, Qt::KeepAspectRatio,Qt::SmoothTransformation);

        int cX = (sw - i.width())/2;
        int cY = (sh - i.height())/2;
        sw = abs(cos(ang)*cX + sin(ang)*cY);
        sh = abs(sin(ang)*cX + cos(ang)*cY);

        m_ren.centerX = sw;
        m_ren.centerY = sh;
    }

    int x = i.width();
    int y = i.height();

    float scaleX = 0;
    float scaleY = 0;

    if(ang < M_PI)
        scaleY -= sin(ang)*y;
    else
        scaleX -= sin(ang - M_PI)*x;

    if(ang > M_PI/2 && ang < M_PI*1.5)
    {
        scaleX -= sin(ang - M_PI/2)*x;
        scaleY -= sin(ang - M_PI/2)*y;
    }

    m_ren.transX = scaleX;
    m_ren.transY = scaleY;
    update();
}

void ViewWidget::resizeEvent(QResizeEvent *ev)
{
    updateRenderData();
}

void ViewWidget::fpsTime()
{
    static qint64 last = QDateTime::currentMSecsSinceEpoch();

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    m_fps = float(m_frames)/(float(now-last)/1000);

    m_frames = 0;
    last = now;
    update();
}

void ViewWidget::fpsAvg()
{
    static qint64 last = QDateTime::currentMSecsSinceEpoch();

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    m_avgFps = float(m_framesAvg)/(float(now-last)/1000);

    m_framesAvg = 0;
    last = now;
    update();
}
