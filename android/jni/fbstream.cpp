#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <zlib.h>
#include <netdb.h>

#include <binder/IMemory.h>
#include <gui/SurfaceComposerClient.h>

#include "skia/images/SkImageEncoder.h"
#include "skia/core/SkBitmap.h"
#include "skia/core/SkData.h"
#include "skia/core/SkStream.h"

#define FB_SIZE (800*1280*4)
#define ZLIB_RATIO 1
#define CON_UDP 0
#define CON_TCP 1
#define CON_DEFAULT CON_UDP

#define SCREEN_THREADS 16
#define NET_THREADS 16

static pthread_t net_thread;
static pthread_t screen_threads[SCREEN_THREADS];
static int quality = 60;
static int scale = 100;

struct data_out_item
{
    char *data;
    uint32_t size;
    uint32_t allocd;
};

#define ITEM_MAX 20
class data_out_list
{
public:
    data_out_list()
    {
        pthread_mutex_init(&m_send_mutex, NULL);
        pthread_mutex_init(&m_free_mutex, NULL);
        m_sendR = m_sendW = 0;
        m_freeR = m_freeW = 0;
    } 

    struct data_out_item *getSend()
    {
        pthread_mutex_lock(&m_send_mutex);
        if(empty_send())
        {
            pthread_mutex_unlock(&m_send_mutex);
            return NULL;
        }
        else
        {
            data_out_item *res = pop_send();
            pthread_mutex_unlock(&m_send_mutex);
            return res;
        }
    }

    void addSend(data_out_item *it)
    {
        pthread_mutex_lock(&m_send_mutex);
        if(!push_back_send(it))
        {
            pthread_mutex_unlock(&m_send_mutex);
            delete[] it->data;
            delete it;
        }
        else
            pthread_mutex_unlock(&m_send_mutex);
    }
    
    struct data_out_item *getFree(int size)
    {
        struct data_out_item *res;
        pthread_mutex_lock(&m_free_mutex);
        if(empty_free())
        {
            pthread_mutex_unlock(&m_free_mutex);

            res = new data_out_item;
            res->data = new char[size+10];
            res->size = size+10;
            res->allocd = size+10;

            strcpy(res->data, "FRAM\n");
        }
        else
        {
            res = pop_free();
            pthread_mutex_unlock(&m_free_mutex);
            if(res->allocd < size+10)
            {
                printf("allocd %d to %d\n", res->allocd, size+10);
                delete[] res->data;
                res->data = new char[size+10];
                res->allocd = size+10;
                strcpy(res->data, "FRAM\n");
            }
            res->size = size+10;
        }
        memcpy(res->data+6, (char*)&size, 4);
        return res;
    }

    void addFree(struct data_out_item *it)
    {
        pthread_mutex_lock(&m_free_mutex);
        if(!push_back_free(it))
        {
            pthread_mutex_unlock(&m_free_mutex);
            delete[] it->data;
            delete it;
        }
        else
            pthread_mutex_unlock(&m_free_mutex);
    }

private:
    bool empty_free() const { return m_freeR == m_freeW; }
    data_out_item *pop_free()
    {
        data_out_item *res = m_free[m_freeR];
        m_freeR = inc(m_freeR);
        return res;
    }

    bool push_back_free(data_out_item *it)
    {
        int next_witr = inc(m_freeW);
        if(next_witr == m_freeR)
        {
            printf("free item queue full\n");
            return false;
        }
        m_free[m_freeW] = it;
        m_freeW = next_witr;
        return true;
    }
    
    bool empty_send() const { return m_sendR == m_sendW; }
    data_out_item *pop_send()
    {
        data_out_item *res = m_send[m_sendR];
        m_sendR = inc(m_sendR);
        return res;
    }

    bool push_back_send(data_out_item *it)
    {
        int next_witr = inc(m_sendW);
        if(next_witr == m_sendR)
        {
            printf("free item queue full\n");
            return false;
        }
        m_send[m_sendW] = it;
        m_sendW = next_witr;
        return true;
    }
    
    int inc(int i)
    {
        ++i;
        return i >= ITEM_MAX ? 0 : i;
    }
    
    struct data_out_item *m_free[ITEM_MAX];
    struct data_out_item *m_send[ITEM_MAX];
    pthread_mutex_t m_free_mutex;
    pthread_mutex_t m_send_mutex;
    int m_sendR;
    int m_sendW;
    int m_freeR;
    int m_freeW;
};

struct netinfo
{
    char *address;
    uint16_t port;
};

static class data_out_list data_queue;

static void queue_add(SkDynamicMemoryWStream* stream)
{
    int size = stream->bytesWritten();

    struct data_out_item *it = data_queue.getFree(size);

    stream->read(it->data+10, 0, size);

    data_queue.addSend(it);
}

static void *udp_thread_work(void *param)
{
    struct netinfo *netinfo = (struct netinfo*)param;
    struct sockaddr_in si_other;
    int sock;

    if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
    {
        printf("errror opening %s %d\n", strerror(errno), errno);
        return NULL;
    }

    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(netinfo->port);
    if (inet_aton(netinfo->address, &si_other.sin_addr)==0)
    {
        printf("errror inet %s %d\n", strerror(errno), errno);
        return NULL;
    }

    struct data_out_item *it;
    while(1)
    {
        it = data_queue.getSend();

        while(it)
        {
            char *itr = it->data;
            char *end = it->data+it->size;
            uint16_t chunk;
            while(itr < end)
            {
                chunk = it->size > 65000 ? 65000 : it->size;
                if(sendto(sock, itr, chunk, 0, (const sockaddr*)&si_other, sizeof(si_other)) != chunk)
                    printf("errror sending %s %d\n", strerror(errno), errno);

                itr += chunk;
                it->size -= chunk;
            }
            printf("sent\n");

            data_queue.addFree(it);
            it = data_queue.getSend();
        }
        usleep(1000);
    }

    close(sock);
    return NULL;
}

static void *tcp_worker_thread(void *data)
{
    int sock = *((int*)data);
    struct data_out_item *it;
    while(1)
    {
        it = data_queue.getSend();

        while(it)
        {
            write(sock, it->data, it->size);
            if(errno != 0)
                printf("errror sending %s %d\n", strerror(errno), errno);

            data_queue.addFree(it);
            it = data_queue.getSend();
        }
        usleep(1000);
    }
}

static void *tcp_thread_work(void *param)
{
    struct netinfo *netinfo = (struct netinfo*)param;
    struct sockaddr_in addr;
    struct hostent *server;
    int sock;

    if ((sock=socket(AF_INET, SOCK_STREAM, 0))==-1)
    {
        printf("errror opening %s %d\n", strerror(errno), errno);
        return NULL;
    }
    
    server = gethostbyname(netinfo->address);
    if(!server)
    {
        printf("Could not find host %s", netinfo->address);
        return NULL;
    }

    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(netinfo->port);
    memcpy((char*)&addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        printf("errror connecting %s %d\n", strerror(errno), errno);
        return NULL;
    }

    pthread_t workers[3];
    for(int i = 0; i < 3; ++i)
        pthread_create(&workers[i], NULL, tcp_worker_thread, (void*)&sock);

    while(true) { sleep(1); }
    
    close(sock);
    return NULL;
}

void resize(char *in, char *out, int w, int h, int resW, int resH)
{
    int a, b, c, d, x, y, index,  blue, red, green;
    float x_ratio = ((float)(w-1))/resW;
    float y_ratio = ((float)(h-1))/resH;
    float x_diff, y_diff;
    int offset = 0;

    for(int i = 0; i < resH; ++i)
    {
        for(int j = 0; j < resW; ++j)
        {
            x_diff = (x_ratio * j);
            y_diff = (y_ratio * i);
            x = (int)x_diff;
            y = (int)y_diff;
            x_diff -= x;
            y_diff -= y;
            index = y*w + x;
            a = *((int*)(in + (index*4)));
            b = *((int*)(in + ((index+1)*4)));
            c = *((int*)(in + ((index+w)*4)));
            d = *((int*)(in + ((index+w+1)*4)));

            // blue element
            // Yb = Ab(1-w)(1-h) + Bb(w)(1-h) + Cb(h)(1-w) + Db(wh)
            out[offset++] = ((a)&0xff)*(1-x_diff)*(1-y_diff) + ((b)&0xff)*(x_diff)*(1-y_diff) +
                   ((c)&0xff)*(y_diff)*(1-x_diff)   + ((d)&0xff)*(x_diff*y_diff);

            // green element
            // Yg = Ag(1-w)(1-h) + Bg(w)(1-h) + Cg(h)(1-w) + Dg(wh)
            out[offset++] = ((a>>8)&0xff)*(1-x_diff)*(1-y_diff) + ((b>>8)&0xff)*(x_diff)*(1-y_diff) +
                    ((c>>8)&0xff)*(y_diff)*(1-x_diff)   + ((d>>8)&0xff)*(x_diff*y_diff);

            // red element
            // Yr = Ar(1-w)(1-h) + Br(w)(1-h) + Cr(h)(1-w) + Dr(wh)
            out[offset++] = ((a>>16)&0xff)*(1-x_diff)*(1-y_diff) + ((b>>16)&0xff)*(x_diff)*(1-y_diff) +
                  ((c>>16)&0xff)*(y_diff)*(1-x_diff)   + ((d>>16)&0xff)*(x_diff*y_diff);

           out[offset++] = 0xFF; // alpha
        }
    }
}

static int w;
static int h;

#define BUFF_SIZE 50

class framebuffer
{
public:
    framebuffer()
    {
        pthread_mutex_init(&mutex, NULL);
        m_ritr = 0;
        m_witr = 0;
    }

    char *get()
    {
        pthread_mutex_lock(&mutex);
        if(empty())
        {
            pthread_mutex_unlock(&mutex);
            return new char[FB_SIZE];
        }
        else
        {
            char *res = pop_back();
            pthread_mutex_unlock(&mutex);
            return res;
        }
    }
    
    void add(char *mem)
    {
        pthread_mutex_lock(&mutex);
        if(!push_back(mem))
        {
            pthread_mutex_unlock(&mutex);
            delete[] mem;
        }
        else
            pthread_mutex_unlock(&mutex);
    }

    int size() const { return (m_witr - m_ritr); }
private:
    bool empty() const { return m_ritr == m_witr; }
    char *pop_back()
    {
        char *res = m_blocks[m_ritr];
        m_ritr = inc(m_ritr);
        return res;
    }
    
    bool push_back(char *mem)
    {
        int next_witr = inc(m_witr);
        if(next_witr == m_ritr)
        {
            printf("queue is full\n");
            return false;
        }
        m_blocks[m_witr] = mem;
        m_witr = next_witr;
        return true;
    }
    
    int inc(int i)
    {
        ++i;
        return i >= BUFF_SIZE ? 0 : i;
    }

    char *m_blocks[BUFF_SIZE];
    int m_ritr;
    int m_witr;
    pthread_mutex_t mutex;
};

framebuffer fb;

static void *encode_thread(void *data)
{

    SkBitmap b;
    b.setConfig(SkBitmap::kARGB_8888_Config, w, h);
    b.setPixels(data);

    SkDynamicMemoryWStream stream;

    SkImageEncoder::EncodeStream(&stream, b, SkImageEncoder::kJPEG_Type, quality);

    queue_add(&stream);

    fb.add((char*)data);
    return 0;
}

int main(int argc, char* argv[])
{
    printf("FBStream - streaming framebuffer via wifi\n");
    if(argc < 3)
    {
        printf("Usage: %s <viewer's address> <port> [tcp/udp] [jpeg quality (0 - 100, default 60)] [scale percent (default 100)]\n", argv[0]);
        printf("Example: %s 192.168.0.100 33333 udp 40 2 50\n", argv[0]);
        return 0;
    }

    struct netinfo info;
    int con = CON_DEFAULT;

    switch(argc)
    {
        default:
        case 6:
            scale = atoi(argv[5]);
            if(scale < 1)
                scale = 1;
            else if(scale > 100)
                scale = 100;
            //fallthrough
        case 5:
            quality = atoi(argv[4]);
            if(quality < 0)
                quality = 0;
            else if(quality > 100)
                quality = 100;
            // fallthrough
        case 4:
            if(strcmp(argv[3], "tcp") == 0)
                con = CON_TCP;
            else if(strcmp(argv[3], "udp") == 0)
                con = CON_UDP;
            else
            {
                printf("Invalid con type value \"%s\", use \"tcp\" or \"udp\"\n", argv[3]);
                return 0;
            }
            // fallthrough
        case 3:
            info.address = (char*)malloc(sizeof(char)*(strlen(argv[1])+1));
            strcpy(info.address, argv[1]);
            info.port = atoi(argv[2]);
            break;
    }

    printf("\nSending data to %s:%u (%s)\nJPEG quality: %d\n Scale: %d%%\n\n", info.address, info.port, con == CON_UDP ? "udp" : "tcp", quality, scale);

    // Init net thread
    if(con == CON_UDP)
        pthread_create(&net_thread, NULL, udp_thread_work, &info);
    else
        pthread_create(&net_thread, NULL, tcp_thread_work, &info);

    android::ScreenshotClient sc;
    int res = sc.update();
    if(res != android::NO_ERROR)
    {
        printf("Error taking screenshot: %d\n", res);
        return 0;
    }
    w = (sc.getWidth()*scale)/100;
    h = (sc.getHeight()*scale)/100;

    printf("res %dx%d, format %d, size %d\n", w, h, sc.getFormat(), sc.getSize());

    pthread_t t;
    while(sc.update() == android::NO_ERROR)
    {
        char *buff = fb.get();

        if(scale != 100)
            resize((char*)sc.getPixels(), buff, 800, 1280, w, h);
        else
            memcpy(buff, (char*)sc.getPixels(), sc.getSize());

        pthread_create(&t, NULL, encode_thread, buff);
        usleep(1000);
    }

    return 0;
}
