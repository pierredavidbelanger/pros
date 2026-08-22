#ifndef PROS_LDISC_H
#define PROS_LDISC_H

#include "stdc.h"

#define LDISC_BUF_SIZE 256

struct ldisc {
    uint8_t buf[LDISC_BUF_SIZE];
    uint64_t len;        // bytes in buf
    uint64_t consumed;   // how much of a finished line the reader already took
    bool complete;       // buf holds a finished line, waiting to be drained
    void (*echo)(char);  // callback
};

void ldisc_init(struct ldisc *ld, void (*echo)(char));
bool ldisc_feed(struct ldisc *ld, uint8_t byte);
bool ldisc_ready(const struct ldisc *ld);
uint64_t ldisc_drain(struct ldisc *ld, uint8_t *out, uint64_t size);

#endif  // PROS_LDISC_H
