#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>

// ---------------- 全局变量 ----------------
struct camera_buffer { void *start; size_t length; };
struct image_frame { unsigned char *data; size_t size; };

#define FRAME_QUEUE_CAPACITY 4
static sigset_t signal_set;

struct frame_queue {
    struct image_frame frames[FRAME_QUEUE_CAPACITY];
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

static struct frame_queue queue = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER
};

// 自定义帧头，必须和 PC 端一致
struct frame_header {
    uint32_t frame_size;
    uint32_t frame_id;
    uint32_t magic;
} __attribute__((packed));

int g_sockfd = -1; // 全局网络套接字

// ---------------- 队列功能 ----------------
int queue_push(struct image_frame image) {
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == FRAME_QUEUE_CAPACITY && !queue.closed)
        pthread_cond_wait(&queue.not_full, &queue.mutex);
    if (queue.closed) { pthread_mutex_unlock(&queue.mutex); return -1; }

    queue.frames[queue.write_pos] = image;
    queue.write_pos = (queue.write_pos + 1) % FRAME_QUEUE_CAPACITY;
    queue.count++;
    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);
    return 0;
}

int queue_pop(struct image_frame *image) {
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == 0 && !queue.closed)
        pthread_cond_wait(&queue.not_empty, &queue.mutex);
    if (queue.count == 0) { pthread_mutex_unlock(&queue.mutex); return -1; }

    *image = queue.frames[queue.read_pos];
    queue.frames[queue.read_pos].data = NULL;
    queue.frames[queue.read_pos].size = 0;
    queue.read_pos = (queue.read_pos + 1) % FRAME_QUEUE_CAPACITY;
    queue.count--;
    pthread_cond_signal(&queue.not_full);
    pthread_mutex_unlock(&queue.mutex);
    return 0;
}

void queue_close(void) {
    pthread_mutex_lock(&queue.mutex);
    queue.closed = 1;
    pthread_cond_broadcast(&queue.not_empty);
    pthread_cond_broadcast(&queue.not_full);
    pthread_mutex_unlock(&queue.mutex);
}

int queue_is_closed(void) {
    int closed;
    pthread_mutex_lock(&queue.mutex);
    closed = queue.closed;
    pthread_mutex_unlock(&queue.mutex);
    return closed;
}

// ---------------- 网络发送功能 ----------------
ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t total = 0; const char *p = buf;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        total += n;
    }
    return total;
}

int connect_to_server(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) { perror("socket"); return -1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    printf("Connecting to %s:%d...\n", ip, port);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    printf("Connected to server successfully!\n");
    return sockfd;
}

// ---------------- 采集线程 ----------------
static void *capture_thread(void *arg) {
    (void)arg;
    int fd;
    struct v4l2_format fmt = {0};
    struct v4l2_requestbuffers req = {0};
    struct camera_buffer *buffers;
    unsigned int i, mapped = 0;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_buffer frame = {0};

    printf("capture thread start\n");
    fd = open("/dev/video1", O_RDWR | O_NONBLOCK);
    if (fd == -1) { perror("open camera"); return NULL; }
    printf("camera fd is opened\n");

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640; fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) { perror("set fmt"); close(fd); return NULL; }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "not MJPEG\n"); close(fd); return NULL;
    }

    req.count = 4; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) { perror("reqbufs"); close(fd); return NULL; }
    
    buffers = calloc(req.count, sizeof(*buffers));
    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.index = i; buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) break;
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) break;
        mapped++;
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) break;
    }

    if (i == req.count) {
        if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) perror("streamon");
        else {
            printf("capture started\n");
            // 持续采集，直到队列被要求关闭
            for (unsigned int n = 0; !queue_is_closed(); ) {
                struct image_frame image = {0};
                struct pollfd pfd = {0};
                pfd.fd = fd; pfd.events = POLLIN;
                int ready = poll(&pfd, 1, 3000);
                
                if (ready <= 0) { if(errno == EINTR) continue; break; }
                if (!(pfd.revents & POLLIN)) continue;

                memset(&frame, 0, sizeof(frame));
                frame.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                frame.memory = V4L2_MEMORY_MMAP;
                if (ioctl(fd, VIDIOC_DQBUF, &frame) == -1) {
                    if (errno == EAGAIN || errno == EINTR) continue;
                    break;
                }

                if (!(frame.flags & V4L2_BUF_FLAG_ERROR) && frame.bytesused > 0 && frame.bytesused <= buffers[frame.index].length) {
                    image.size = frame.bytesused;
                    image.data = malloc(image.size);
                    if (image.data) {
                        memcpy(image.data, buffers[frame.index].start, image.size);
                        ioctl(fd, VIDIOC_QBUF, &frame); // 先归还驱动缓冲
                        
                        if (queue_push(image) == -1) { // 再放入队列
                            free(image.data); break;
                        }
                        n++;
                    }
                } else {
                    ioctl(fd, VIDIOC_QBUF, &frame);
                }
            }
        }
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    
    for (i = 0; i < mapped; i++) munmap(buffers[i].start, buffers[i].length);
    free(buffers);
    close(fd);
    printf("capture thread finished\n");
    return NULL;
}

// ---------------- 发送线程 ----------------
static void *storage_thread(void *arg) {
    int sockfd = *(int *)arg;
    struct image_frame image = {0};
    uint32_t frame_id = 0;

    printf("network send thread started\n");
    while (queue_pop(&image) == 0) {
        frame_id++;
        struct frame_header hdr;
        hdr.frame_size = htonl(image.size);
        hdr.frame_id = htonl(frame_id);
        hdr.magic = htonl(0x4D4A5047);

        // 发送头，再发送图片
        if (send_all(sockfd, &hdr, sizeof(hdr)) <= 0 || 
            send_all(sockfd, image.data, image.size) <= 0) {
            perror("send failed");
            free(image.data);
            break;
        }
        
        printf("Sent frame %u, size: %zu\n", frame_id, image.size);
        free(image.data);
    }
    printf("network send thread finished\n");
    return NULL;
}

// ---------------- 信号处理线程 ----------------
static void *signal_thread(void *arg) {
    (void)arg;
    for (;;) {
        int signo, ret = sigwait(&signal_set, &signo);
        if (ret != 0) { queue_close(); return NULL; }
        if (signo == SIGINT) {
            printf("stop requested\n");
            queue_close();
        }
        if (signo == SIGUSR1) return NULL;
    }
}

// ---------------- 主函数 ----------------
int main(void) {
    signal(SIGPIPE, SIG_IGN); 

    // 请在这里填写你 Ubuntu 实际的 IP 地址和端口！
    g_sockfd = connect_to_server("192.168.50.20", 8888);
    if (g_sockfd == -1) {
        printf("Failed to connect to server, exiting.\n");
        return 1;
    }

    pthread_t capture_tid, storage_tid, signal_tid;
    int ret;

    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

    pthread_create(&signal_tid, NULL, signal_thread, NULL);
    pthread_create(&storage_tid, NULL, storage_thread, &g_sockfd);
    pthread_create(&capture_tid, NULL, capture_thread, NULL);

    pthread_join(capture_tid, NULL);
    queue_close();
    pthread_join(storage_tid, NULL);
    
    pthread_kill(signal_tid, SIGUSR1);
    pthread_join(signal_tid, NULL);
    
    close(g_sockfd);
    printf("netcam stopped\n");
    return 0;
}
