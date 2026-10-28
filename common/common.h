#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// 两端必须一致的帧头结构
struct frame_header {
    uint32_t frame_size;
    uint32_t frame_id;
    uint32_t magic;
} __attribute__((packed));

#endif
