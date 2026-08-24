#ifndef PROS_CONSOLE_INPUT_H
#define PROS_CONSOLE_INPUT_H

#include "stdc.h"

void console_input_init(void);

void console_input_push(uint8_t byte);

bool console_input_pop(uint8_t *out);

#endif  // PROS_CONSOLE_INPUT_H
