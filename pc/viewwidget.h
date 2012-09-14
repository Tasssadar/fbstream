#ifndef VIEWWIDGET_H
#define VIEWWIDGET_H

#include <QWidget>

struct packet
{
    packet()
    {
        len = 0;
        data = dataItr = NULL;
        pos = 0;
        empty = true;
    }

    void initLen(quint32 len)
    {
        empty = len == 0;
        pos = 0;
        this->len = len;
        data = (uchar*)realloc(data, sizeof(uchar)*len);
        dataItr = data;
    }

    bool empty;
    quint32 len;
    quint32 pos;
    uchar* data;
    uchar* dataItr;
};

class ViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ViewWidget(QWidget *parent = 0);
    
public slots:
    void processData(QByteArray data);

    void fpsTime();
protected:
    void paintEvent(QPaintEvent *ev);

    packet m_readPkt;
    packet m_drawPkt;
    QByteArray m_draw;
    float m_fps;
    quint32 m_frames;
};

#endif // VIEWWIDGET_H
