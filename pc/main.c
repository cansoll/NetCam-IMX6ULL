#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <SDL2/SDL.h>

#include "receiver.h"
#include "jpeg_decode.h"

// 【关键】：前面用 extern 声明的全局变量，必须在某个 .c 文件里真正定义出来
struct SharedFrame shared = {0};
volatile int keep_running = 1;
int g_client_fd = -1;
int g_port = 8888;
int g_enable_save = 0;

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "p:s")) != -1) {
        switch (opt) {
            case 'p': g_port = atoi(optarg); break;
            case 's': g_enable_save = 1; break;
            default: fprintf(stderr, "用法: %s [-p 端口] [-s]\n", argv[0]); return 1;
        }
    }

    if (g_enable_save) printf("[提示] 抓拍功能已开启。在预览窗口按 'S' 键即可保存当前画面。\n");

    pthread_mutex_init(&shared.mutex, NULL);
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receiver_thread, NULL);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("NetCam Live Preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 640, 480);

    int running = 1;
    SDL_Event event;
    int snapshot_count = 0;
    int save_next_frame = 0;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            else if (event.type == SDL_KEYDOWN) {
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
            if (save_next_frame) {
                save_next_frame = 0;
                snapshot_count++;
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                char filename[128];
                snprintf(filename, sizeof(filename), "snapshot_%04d_%02d%02d%02d.jpg", snapshot_count, t->tm_hour, t->tm_min, t->tm_sec);

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

    printf("[主线程] 用户关闭窗口...\n");
    keep_running = 0; 
    if (g_client_fd != -1) shutdown(g_client_fd, SHUT_RDWR);
    pthread_join(recv_tid, NULL);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    pthread_mutex_destroy(&shared.mutex);
    printf("[主线程] 程序安全退出。\n");
    return 0;
}
