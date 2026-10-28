#ifndef JPEG_DECODE_H
#define JPEG_DECODE_H

#include <stddef.h>

// 声明：告诉别人有这么一个函数
unsigned char *decode_jpeg_to_rgb(const unsigned char *jpeg_data, size_t jpeg_size, int *out_w, int *out_h);

#endif
