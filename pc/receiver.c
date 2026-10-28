#include "receiver.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

// 内部辅助函数：完整接收
static ssize_t recv_all(int fd, void *buf, size_t len) {
    size_t total = 0; char *p = buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n == 0) return 0;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        total += n;
    }
    return total;
}

// 线程实现
void *receiver_thread(void *arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(g_port); // 使用外面的全局变量

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);
    printf("[接收线程] 等待板子连接端口 %d...\n", g_port);

    g_client_fd = accept(server_fd, NULL, NULL);
    printf("[接收线程] 板子已连接！开始接收画面...\n");

    struct timeval tv = {3, 0};
    setsockopt(g_client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (keep_running) {
        struct frame_header hdr;
        ssize_t n = recv_all(g_client_fd, &hdr, sizeof(hdr));
        
        if (n == 0) { printf("[接收线程] 板子主动断开。\n"); break; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) printf("[接收线程] 接收超时（断网）。\n");
            else perror("[接收线程] 网络错误");
            break;
        }

        uint32_t image_size = ntohl(hdr.frame_size);
        uint32_t magic = ntohl(hdr.magic);
        if (magic != 0x4D4A5047) break;

        unsigned char *buf = malloc(image_size);
        if (recv_all(g_client_fd, buf, image_size) != (ssize_t)image_size) {
            free(buf); break;
        }

        pthread_mutex_lock(&shared.mutex);
        if (shared.jpeg_data) free(shared.jpeg_data);
        shared.jpeg_data = buf;
        shared.jpeg_size = image_size;
        shared.updated = 1;
        pthread_mutex_unlock(&shared.mutex);
    }

    if (g_client_fd != -1) close(g_client_fd);
    close(server_fd);
    printf("[接收线程] 退出。\n");
    return NULL;
}
