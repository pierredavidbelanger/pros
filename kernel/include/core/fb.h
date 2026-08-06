#ifndef PROS_FB_H
#define PROS_FB_H

#include "stdc.h"

#include <limine.h>

void fb_init(struct limine_framebuffer_response *framebuffer_response);

void fb_draw_string(size_t start_x, size_t start_y, const char *str, uint32_t fg_color, uint32_t bg_color);

#endif //PROS_FB_H
