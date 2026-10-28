#ifndef RECEIVER_H
#define RECEIVER_H

#include <pthread.h>
#include <stddef.h>

// 定义共享结构体
struct SharedFrame {
    unsigned char *jpeg_data;
    size_t jpeg_size;
    int updated;
    pthread_mutex_t mutex;
};

// 用 extern 告诉编译器，这些全局变量在 main.c 里面已经定义了。
extern struct SharedFrame shared;
extern volatile int keep_running;
extern int g_client_fd;
extern int g_port;

// 声明线程函数
void *receiver_thread(void *arg);

#endif
