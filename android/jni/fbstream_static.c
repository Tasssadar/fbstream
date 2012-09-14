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

#define FB_SIZE (480*320*2)
#define ZLIB_RATIO 1
#define FRAME_BUFF 10

static pthread_t net_thread;
static pthread_t fbread_thread;

struct data_out_item
{
    char data[FB_SIZE];
    uint32_t size;
};

struct data_out_list
{
    pthread_mutex_t mutex;
    struct data_out_item data[FRAME_BUFF];
    int ritr;
    int witr;
};

struct netinfo
{
    char *address;
    uint16_t port;
};

static struct data_out_list data_queue;

static void init_out_list(void)
{
    pthread_mutex_init(&data_queue.mutex, NULL);
    data_queue.witr = 0;
    data_queue.ritr = 0;

    int i = 0;
    for(; i < FRAME_BUFF; ++i)
        strcpy(data_queue.data[i].data, "FRAM\n");
}

static int incItr(int itr)
{
    ++itr;
    return itr >= FRAME_BUFF ? 0 : itr;
}

static void queue_add(char *data, uint32_t size, uint32_t size_uncompressed)
{
    pthread_mutex_lock(&data_queue.mutex);

    int new_witr = incItr(data_queue.witr);
    if(new_witr == data_queue.ritr)
        data_queue.ritr = incItr(data_queue.ritr);

    uint32_t alloc_size = size + 14;
    if(alloc_size >= FB_SIZE)
    {
        printf("Failed to add frame, buffer is too small!\n");
        return;
    }

    data_queue.data[data_queue.witr].size = alloc_size;
    char *dst = data_queue.data[data_queue.witr].data;

    dst[6] = (size) >> 24;
    dst[7] = (size) >> 16;
    dst[8] = (size) >> 8;
    dst[9] = (size);

    dst[10] = (size_uncompressed) >> 24;
    dst[11] = (size_uncompressed) >> 16;
    dst[12] = (size_uncompressed) >> 8;
    dst[13] = (size_uncompressed);
    memcpy(dst+14, data, size);

    data_queue.witr = new_witr;

    pthread_mutex_unlock(&data_queue.mutex);
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
        pthread_mutex_lock(&data_queue.mutex);
        if(data_queue.ritr == data_queue.witr)
            it = NULL;
        else
        {
            it = &data_queue.data[data_queue.ritr];
            data_queue.ritr = incItr(data_queue.ritr);
        }
        pthread_mutex_unlock(&data_queue.mutex);

        while(it)
        {
            char *itr = it->data;
            char *end = it->data+it->size;
            uint16_t chunk;
            while(itr < end)
            {
                chunk = it->size > 60000 ? 60000 : it->size;
                if(sendto(sock, itr, chunk, 0, &si_other, sizeof(si_other)) == -1)
                    printf("errror sending %s %d\n", strerror(errno), errno);
                itr += chunk;
                it->size -= chunk;
            }
            pthread_mutex_lock(&data_queue.mutex);
            if(data_queue.ritr == data_queue.witr)
                itr = NULL;
            else
            {
                it = &data_queue.data[data_queue.ritr];
                data_queue.ritr = incItr(data_queue.ritr);
            }
            pthread_mutex_unlock(&data_queue.mutex);
        }
        usleep(1000);
    }

    close(sock);
    return NULL;
}

int main(int argc, char* argv[])
{
    printf("FBStream - streaming framebuffer via wifi\n");
    if(argc < 3)
    {
        printf("Usage: %s <viewer's address> <port> [compression ratio (1-9)]\n", argv[0]);
        return 0;
    }

    struct netinfo info;
    info.address = malloc(sizeof(char)*(strlen(argv[1])+1));
    strcpy(info.address, argv[1]);
    info.port = atoi(argv[2]);

    int ratio = ZLIB_RATIO;
    if(argc >= 4)
    {
        ratio = atoi(argv[3]);
        if(ratio < 1)
            ratio = 1;
        else if(ratio > 9)
            ratio = 9;
    }

    printf("\nSending data to %s:%u\nCompression ratio: %d\n\n", info.address, info.port, ratio);

    init_out_list();

    pthread_create(&net_thread, NULL, udp_thread_work, &info);

    int fd = open("/dev/graphics/fb0", O_RDONLY);
    if(fd == -1)
    {
        printf("failed to open framebuffer: %s (%d)\n", strerror(errno), errno);
        return 0;
    }

    char buff[FB_SIZE];
    char zbuff[FB_SIZE+100];

    unsigned long len_buff;
    int len_read;

    while(1)
    {
        len_read = read(fd, buff, FB_SIZE);
        if(len_read > 0)
        {
            len_buff = FB_SIZE;
            int res = compress2((Bytef *)zbuff, &len_buff, buff, len_read, ratio);
            if(res == Z_OK)
                queue_add(zbuff, len_buff, len_read);
        }
        lseek(fd, 0, SEEK_SET);
        usleep(1000);
    }

    close(fd);

    return 0;
}
