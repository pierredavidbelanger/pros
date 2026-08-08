#ifndef PROS_FB_H
#define PROS_FB_H

#include "stdc.h"

#include <limine.h>

void fb_init(struct limine_framebuffer_response *framebuffer_response);

void fb_terminal_print_char(char c);

bool fb_is_active(void);

#endif //PROS_FB_H
