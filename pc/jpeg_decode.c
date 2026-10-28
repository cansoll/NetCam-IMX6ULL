#include "jpeg_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>

// 实现：具体的解码逻辑
unsigned char *decode_jpeg_to_rgb(const unsigned char *jpeg_data, size_t jpeg_size, int *out_w, int *out_h) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = cinfo.output_width;
    int h = cinfo.output_height;
    int ch = cinfo.output_components;
    unsigned char *rgb = malloc(w * h * ch);

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row = rgb + cinfo.output_scanline * w * ch;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    *out_w = w; *out_h = h;
    return rgb;
}
