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

#define ANDROID_RGB
#include <jpeg/jpeglib.h>

#define FB_SIZE (480*320*2)
#define ZLIB_RATIO 1
#define CON_UDP 0
#define CON_TCP 1
#define CON_DEFAULT CON_UDP

static pthread_t net_thread;
static pthread_t fbread_thread;
static struct jpeg_compress_struct cinfo;
static struct jpeg_error_mgr jerr;

typedef struct {
    struct jpeg_destination_mgr pub; /* base class */
    JOCTET* buffer; /* buffer start address */
    int bufsize; /* size of buffer */
    size_t datasize; /* final size of compressed data */
    int* outsize; /* user pointer to datasize */
    int errcount; /* counts up write errors due to buffer overruns */
} memory_destination_mgr;

typedef memory_destination_mgr* mem_dest_ptr;

/* ------------------------------------------------------------- */
/* MEMORY DESTINATION INTERFACE METHODS */
/* ------------------------------------------------------------- */


/* This function is called by the library before any data gets written */
METHODDEF(void)
init_destination (j_compress_ptr cinfo)
{
    mem_dest_ptr dest = (mem_dest_ptr)cinfo->dest;
    
    dest->pub.next_output_byte = dest->buffer; /* set destination buffer */
    dest->pub.free_in_buffer = dest->bufsize; /* input buffer size */
    dest->datasize = 0; /* reset output size */
    dest->errcount = 0; /* reset error count */
}

/* This function is called by the library if the buffer fills up
 * 
 * I just reset destination pointer and buffer size here.
 * Note that this behavior, while preventing seg faults
 * will lead to invalid output streams as data is over-
 * written.
 */
METHODDEF(boolean)
empty_output_buffer (j_compress_ptr cinfo)
{
    mem_dest_ptr dest = (mem_dest_ptr)cinfo->dest;
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = dest->bufsize;
    ++dest->errcount; /* need to increase error count */
    
    return TRUE;
}

/* Usually the library wants to flush output here.
 * 
 * I will calculate output buffer size here.
 * Note that results become incorrect, once
 * empty_output_buffer was called.
 * This situation is notified by errcount.
 */
METHODDEF(void)
term_destination (j_compress_ptr cinfo)
{
    mem_dest_ptr dest = (mem_dest_ptr)cinfo->dest;
    dest->datasize = dest->bufsize - dest->pub.free_in_buffer;
    if (dest->outsize) *dest->outsize += (int)dest->datasize;
}

/* Override the default destination manager initialization
 * provided by jpeglib. Since we want to use memory-to-memory
 * compression, we need to use our own destination manager.
 */
GLOBAL(void)
jpeg_memory_dest (j_compress_ptr cinfo, JOCTET* buffer, int bufsize, int* outsize)
{
    mem_dest_ptr dest;
    
    /* first call for this instance - need to setup */
    if (cinfo->dest == 0) {
        cinfo->dest = (struct jpeg_destination_mgr *)
        (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
                                    sizeof (memory_destination_mgr));
    }
    
    dest = (mem_dest_ptr) cinfo->dest;
    dest->bufsize = bufsize;
    dest->buffer = buffer;
    dest->outsize = outsize;
    /* set method callbacks */
    dest->pub.init_destination = init_destination;
    dest->pub.empty_output_buffer = empty_output_buffer;
    dest->pub.term_destination = term_destination;
}

/* ------------------------------------------------------------- */
/* MEMORY SOURCE INTERFACE METHODS */
/* ------------------------------------------------------------- */

/* Called before data is read */
METHODDEF(void)
init_source (j_decompress_ptr dinfo)
{
    /* nothing to do here, really. I mean. I'm not lazy or something, but...
     *we're actually through here. */
}

/* Called if the decoder wants some bytes that we cannot provide... */
METHODDEF(boolean)
fill_input_buffer (j_decompress_ptr dinfo)
{
    /* we can't do anything about this. This might happen if the provided
     * buffer is either invalid with regards to its content or just a to
     * small bufsize has been given. */
    
    /* fail. */
    return FALSE;
}

/* From IJG docs: "it's not clear that being smart is worth much trouble"
 * So I save myself some trouble by ignoring this bit.
 */
METHODDEF(void)
skip_input_data (j_decompress_ptr dinfo, INT32 num_bytes)
{
    /* There might be more data to skip than available in buffer.
     *This clearly is an error, so screw this mess. */
    if ((size_t)num_bytes > dinfo->src->bytes_in_buffer) {
        dinfo->src->next_input_byte = 0; /* no buffer byte */
        dinfo->src->bytes_in_buffer = 0; /* no input left */
    } else {
        dinfo->src->next_input_byte += num_bytes;
        dinfo->src->bytes_in_buffer -= num_bytes;
    }
}

/* Finished with decompression */
METHODDEF(void)
term_source (j_decompress_ptr dinfo)
{
    /* Again. Absolute laziness. Nothing to do here. Boring. */
}

GLOBAL(void)
jpeg_memory_src (j_decompress_ptr dinfo, unsigned char* buffer, size_t size)
{
    struct jpeg_source_mgr* src;
    
    /* first call for this instance - need to setup */
    if (dinfo->src == 0) {
        dinfo->src = (struct jpeg_source_mgr *)
        (*dinfo->mem->alloc_small) ((j_common_ptr) dinfo, JPOOL_PERMANENT,
                                    sizeof (struct jpeg_source_mgr));
    }
    
    src = dinfo->src;
    src->next_input_byte = buffer;
    src->bytes_in_buffer = size;
    src->init_source = init_source;
    src->fill_input_buffer = fill_input_buffer;
    src->skip_input_data = skip_input_data;
    src->term_source = term_source;
    /* IJG recommend to use their function - as I don't know ****
     *about how to do better, I follow this recommendation */
    src->resync_to_restart = jpeg_resync_to_restart;
}

struct data_out_item
{
    struct data_out_item *prev;
    struct data_out_item *next;

    char *data;
    uint32_t size;
};

struct data_out_list
{
    struct data_out_item *head;
    struct data_out_item *tail;

    pthread_mutex_t mutex;
    uint32_t size;
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
    data_queue.head = NULL;
    data_queue.tail = NULL;
    data_queue.size = 0;
}

// must be locked
static void queue_pop_front(void)
{
    struct data_out_item *it;

    if(data_queue.size == 0)
        return;

    it = data_queue.head;

    if(it->next)
        it->next->prev = it->prev;

    if(it->prev)
        it->prev->next = it->next;

    data_queue.head = it->next;
    --data_queue.size;

    if(data_queue.tail == it)
        data_queue.tail = data_queue.head;


    free(it->data);
    free(it);
}

static void queue_add(char *data, uint32_t size, uint32_t size_uncompressed)
{
    uint32_t alloc_size = size + 14;

    struct data_out_item *it = malloc(sizeof(struct data_out_item));
    it->next = NULL;
    it->data = malloc(sizeof(char)*alloc_size);
    it->size = alloc_size;

    strcpy(it->data, "FRAM\n");
    it->data[6] = (size) >> 24;
    it->data[7] = (size) >> 16;
    it->data[8] = (size) >> 8;
    it->data[9] = (size);

    it->data[10] = (size_uncompressed) >> 24;
    it->data[11] = (size_uncompressed) >> 16;
    it->data[12] = (size_uncompressed) >> 8;
    it->data[13] = (size_uncompressed);
    memcpy(it->data+14, data, size);

    pthread_mutex_lock(&data_queue.mutex);

    if(data_queue.size >= 100)
        queue_pop_front();

    it->prev = data_queue.tail;

    if(data_queue.tail)
        data_queue.tail->next = it;

    if(!data_queue.head)
        data_queue.head = it;
    data_queue.tail = it;

    ++data_queue.size;

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
        it = data_queue.head;
        pthread_mutex_unlock(&data_queue.mutex);

        while(it)
        {
            char *itr = it->data;
            char *end = it->data+it->size;
            uint16_t chunk;
            while(itr < end)
            {
                chunk = it->size > 65000 ? 65000 : it->size;
                if(sendto(sock, itr, chunk, 0, &si_other, sizeof(si_other)) != chunk)
                    printf("errror sending %s %d\n", strerror(errno), errno);

                itr += chunk;
                it->size -= chunk;
            }
            pthread_mutex_lock(&data_queue.mutex);
            queue_pop_front();
            it = data_queue.head;
            pthread_mutex_unlock(&data_queue.mutex);
        }
        usleep(1000);
    }

    close(sock);
    return NULL;
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

    struct data_out_item *it;
    while(1)
    {
        pthread_mutex_lock(&data_queue.mutex);
        it = data_queue.head;
        pthread_mutex_unlock(&data_queue.mutex);

        while(it)
        {
            char *itr = it->data;
            char *end = it->data+it->size;
            uint16_t chunk;
            while(itr < end)
            {
                chunk = it->size > 65000 ? 65000 : it->size;
                if(write(sock, itr, chunk) != chunk)
                    printf("errror sending %s %d\n", strerror(errno), errno);
                itr += chunk;
                it->size -= chunk;
            }
            pthread_mutex_lock(&data_queue.mutex);
            queue_pop_front();
            it = data_queue.head;
            pthread_mutex_unlock(&data_queue.mutex);
        }
        usleep(1000);
    }

    close(sock);
    return NULL;
}

int jpeg(char *dst, int *dst_len, char *src, uint32_t src_len)
{
    JSAMPROW row_ptr[1];
    
    
    jpeg_start_compress(&cinfo, TRUE);
    
    while (cinfo.next_scanline < cinfo.image_height) {
      //  printf("scanline %d %d %d\n", cinfo.next_scanline, cinfo.image_width, cinfo.image_height);
        row_ptr[0] = &src[cinfo.next_scanline * (320*2)];
        jpeg_write_scanlines(&cinfo, row_ptr, 1);
    }
    //printf("finish\n");
    
    jpeg_finish_compress(&cinfo);
    return 0; 
}

int main(int argc, char* argv[])
{
    printf("FBStream - streaming framebuffer via wifi\n");
    if(argc < 3)
    {
        printf("Usage: %s <viewer's address> <port> [tcp/udp] [compression ratio (1-9)]\n", argv[0]);
        printf("Example: %s 192.168.0.100 33333 udp 1\n", argv[0]);
        return 0;
    }

    struct netinfo info;
    int con = CON_DEFAULT;
    int ratio = ZLIB_RATIO;
    
    switch(argc)
    {
        default:
        case 5:
             ratio = atoi(argv[4]);
            if(ratio < 1)
                ratio = 1;
            else if(ratio > 9)
                ratio = 9;
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
            info.address = malloc(sizeof(char)*(strlen(argv[1])+1));
            strcpy(info.address, argv[1]);
            info.port = atoi(argv[2]);
            break;
    }

    printf("\nSending data to %s:%u (%s)\nCompression ratio: %d\n\n", info.address, info.port, con == CON_UDP ? "udp" : "tcp", ratio);

    init_out_list();

    // Init net thread
    if(con == CON_UDP)
        pthread_create(&net_thread, NULL, udp_thread_work, &info);
    else
        pthread_create(&net_thread, NULL, tcp_thread_work, &info);
    
    
    // init jpeg
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    
    cinfo.image_width = 320;
    cinfo.image_height = 480;
    cinfo.input_components = 2;
    cinfo.in_color_space = JCS_RGB_565;
    jpeg_set_defaults(&cinfo);

    
    // start reading from fb
    int fd = open("/dev/graphics/fb0", O_RDONLY);
    if(fd == -1)
    {
        printf("failed to open framebuffer: %s (%d)\n", strerror(errno), errno);
        return 0;
    }

    char buff[FB_SIZE];
    char zbuff[FB_SIZE+100];

    int len_buff = 0;
    int len_read;

    jpeg_memory_dest(&cinfo, (JOCTET*)zbuff, FB_SIZE+100, &len_buff);
    
    while(1)
    {
        len_read = read(fd, buff, FB_SIZE);
        if(len_read > 0)
        {
            len_buff = 0;
#if 0
            int res = compress2((Bytef *)zbuff, &len_buff, buff, len_read, ratio);

            if(res == Z_OK)
                queue_add(zbuff, len_buff, len_read);
#else
            if(jpeg(zbuff, &len_buff, buff, len_read) >= 0)
            {
                //printf("len buff %d len read %d\n", len_buff, len_read);
                queue_add(zbuff, len_buff, len_read);
            }
            
#endif

        }
        lseek(fd, 0, SEEK_SET);
        usleep(1000);
    }

    close(fd);

    return 0;
}
