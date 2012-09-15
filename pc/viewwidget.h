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


struct renderData
{
    int rot;
    float transX;
    float transY;
    int scaleW;
    int scaleH;
    int centerX;
    int centerY;
};

class ViewWidget : public QWidget
{
    Q_OBJECT
Q_SIGNALS:
    void rotChanged(int rot);

public:
    explicit ViewWidget(QWidget *parent = 0);

public slots:
    void processData(QByteArray data);

    void setResize(bool resize);
    void setResX(int res);
    void setResY(int res);
    void setRotation(int rot);
    void rotateLeft() { setRotation(m_rot-90); }
    void rotateRight() {  setRotation(m_rot+90); }

protected:
    void paintEvent(QPaintEvent *ev);
    void resizeEvent(QResizeEvent *ev);

private slots:
    void fpsTime();
    void fpsAvg();

private:
    void setMinimumSizeRot();
    void updateRenderData();

    packet m_readPkt;
    QByteArray m_draw;

    float m_fps;
    float m_avgFps;
    quint32 m_framesAvg;
    quint32 m_frames;

    bool m_resize;
    int m_resX;
    int m_resY;
    int m_rot;
    renderData m_ren;
};

#endif // VIEWWIDGET_H
