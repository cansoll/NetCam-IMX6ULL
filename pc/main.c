#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <sys/socket.h>
#include <SDL2/SDL.h>

#include "receiver.h"
#include "jpeg_decode.h"

// ---------------- 全局共享数据与配置 ----------------
struct SharedFrame shared = {0};
volatile int keep_running = 1;
int g_client_fd = -1;
int g_port = 8888;
int g_enable_save = 0;

// 统计变量
uint64_t total_frames = 0;
time_t start_time;

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "p:s")) != -1) {
        switch (opt) {
            case 'p': g_port = atoi(optarg); break;
            case 's': g_enable_save = 1; break;
            default:
                fprintf(stderr, "用法: %s [-p 端口] [-s]\n", argv[0]);
                return 1;
        }
    }

    if (g_enable_save) {
        printf("[提示] 抓拍功能已开启。在预览窗口按 'S' 键即可保存当前画面。\n");
    }

    start_time = time(NULL);

    pthread_mutex_init(&shared.mutex, NULL);
    
    // 启动接收线程
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receiver_thread, NULL);

    // 初始化 SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL Init 失败: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("NetCam Live Preview", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        printf("窗口创建失败: %s\n", SDL_GetError());
        return 1;
    }

    // 【关键修改】使用软件渲染，避免 VMware 虚拟机里的硬件加速死锁卡死
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL) {
        printf("渲染器创建失败: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 640, 480);

    int running = 1;
    SDL_Event event;
    int snapshot_count = 0;
    int save_next_frame = 0;

    printf("[主线程] 窗口初始化成功，开始预览。\n");

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_s && g_enable_save) {
                    save_next_frame = 1;
                    printf("[主线程] 按下 S 键，准备抓拍...\n");
                }
            }
        }

        int has_new_frame = 0;
        unsigned char *local_jpeg = NULL;
        size_t local_size = 0;

        pthread_mutex_lock(&shared.mutex);
        if (shared.updated) {
            local_jpeg = shared.jpeg_data;
            local_size = shared.jpeg_size;
            shared.jpeg_data = NULL; 
            shared.updated = 0;
            has_new_frame = 1;
        }
        pthread_mutex_unlock(&shared.mutex);

        if (has_new_frame) {
            total_frames++; 
            
            if (save_next_frame) {
                save_next_frame = 0; 
                snapshot_count++;
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                char filename[128];
                snprintf(filename, sizeof(filename), "snapshot_%04d_%02d%02d%02d.jpg", 
                         snapshot_count, t->tm_hour, t->tm_min, t->tm_sec);
                FILE *fp = fopen(filename, "wb");
                if (fp) {
                    fwrite(local_jpeg, 1, local_size, fp);
                    fclose(fp);
                    printf("[保存成功] %s\n", filename);
                }
            }

            int w = 0, h = 0;
            unsigned char *rgb = decode_jpeg_to_rgb(local_jpeg, local_size, &w, &h);
            if (rgb) {
                SDL_UpdateTexture(texture, NULL, rgb, w * 3);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
                free(rgb);
            }
            free(local_jpeg); 
        }

        SDL_Delay(10); 
    }

    // 退出清理
    printf("\n[主线程] 用户关闭窗口，通知接收线程退出...\n");
    keep_running = 0; 
    if (g_client_fd != -1) {
        shutdown(g_client_fd, SHUT_RDWR);
    }
    pthread_join(recv_tid, NULL);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    pthread_mutex_destroy(&shared.mutex);

    // 打印测试报告
    time_t end_time = time(NULL);
    double duration = difftime(end_time, start_time);
    
    printf("\n================ 测试报告 ================\n");
    printf("持续运行时间 : %.0f 秒 (约 %.2f 分钟)\n", duration, duration / 60.0);
    printf("累计接收帧数 : %llu 帧\n", (unsigned long long)total_frames);
    if (duration > 0) {
        printf("平均接收帧率 : %.2f FPS\n", total_frames / duration);
    }
    printf("==========================================\n");
    printf("[主线程] 程序安全退出。\n");
    
    return 0;
}
