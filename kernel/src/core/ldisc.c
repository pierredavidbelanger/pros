#include "core/ldisc.h"

#include "core/memory.h"

// both are real, xterm sends DEL, other senders send BS, we take either
#define LDISC_BS 0x08
#define LDISC_DEL 0x7f

static void ld_echo(struct ldisc *ld, char c) {
    if (ld->echo) ld->echo(c);
}

static void ld_echo_str(struct ldisc *ld, const char *s) {
    for (; *s; s++) ld_echo(ld, *s);
}

void ldisc_init(struct ldisc *ld, void (*echo)(char)) {
    if (!ld) return;
    ld->len = 0;
    ld->consumed = 0;
    ld->complete = false;
    ld->echo = echo;
}

bool ldisc_feed(struct ldisc *ld, uint8_t byte) {
    if (!ld) return false;
    // caller must drain before typing more
    if (ld->complete) return true;

    switch (byte) {
        case LDISC_BS:
        case LDISC_DEL:
            if (ld->len > 0) {
                ld->len--;
                // back up, paint over it, back up again
                ld_echo_str(ld, "\b \b");
            }
            return false;
        case '\r':
        case '\n':
            // the reader gets the newline too, thats what canonical mode means
            if (ld->len < LDISC_BUF_SIZE) ld->buf[ld->len++] = '\n';
            ld_echo(ld, '\n');
            ld->complete = true;
            return true;
        default:
            // no other control chars in a line, yet
            if (byte < 0x20) return false;
            // full, keep room for the \n
            if (ld->len >= LDISC_BUF_SIZE - 1) return false;
            ld->buf[ld->len++] = (char)byte;
            ld_echo(ld, (char)byte);
            return false;
    }
}

bool ldisc_ready(const struct ldisc *ld) {
    return ld && ld->complete;
}

uint64_t ldisc_drain(struct ldisc *ld, uint8_t *out, uint64_t size) {
    if (!ld || !out || !ld->complete) return 0;

    uint64_t avail = ld->len - ld->consumed;
    uint64_t n = size < avail ? size : avail;
    memcpy(out, ld->buf + ld->consumed, n);
    ld->consumed += n;

    if (ld->consumed == ld->len) {
        ld->len = 0;
        ld->consumed = 0;
        ld->complete = false;
    }

    return n;
}
