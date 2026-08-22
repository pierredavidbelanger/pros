#include "core/ldisc.h"
#include "core/memory.h"
#include "core/test/test.h"

#define TEST_LDISC_TAG "LDISC"

#define TEST_LDISC_OUT_SIZE (LDISC_BUF_SIZE + 8)
#define TEST_LDISC_ECHO_SIZE 1024

// the echo callback carries no context pointer, so the capture must be file static
static char test_ldisc_echo_buf[TEST_LDISC_ECHO_SIZE];
static uint64_t test_ldisc_echo_len;

static void test_ldisc_echo(char c) {
    if (test_ldisc_echo_len < TEST_LDISC_ECHO_SIZE) test_ldisc_echo_buf[test_ldisc_echo_len++] = c;
}

static void test_ldisc_echo_reset(void) {
    test_ldisc_echo_len = 0;
}

// feed a whole string, hand back what the last byte reported
static bool test_ldisc_feed_str(struct ldisc *ld, const char *s) {
    bool complete = false;
    for (; *s; s++) complete = ldisc_feed(ld, (uint8_t)*s);
    return complete;
}

void test_ldisc(void) {
    struct ldisc ld;
    uint8_t out[TEST_LDISC_OUT_SIZE];
    uint64_t n;

    // a fresh ldisc has nothing to give
    ldisc_init(&ld, NULL);
    test_report(TEST_LDISC_TAG, "fresh ldisc is not ready", !ldisc_ready(&ld));
    test_report(TEST_LDISC_TAG, "drain before a line yields nothing", ldisc_drain(&ld, out, sizeof(out)) == 0);

    // the plain case: type, press enter, read it back
    test_report(TEST_LDISC_TAG, "plain bytes do not complete a line", !test_ldisc_feed_str(&ld, "hi"));
    test_report(TEST_LDISC_TAG, "newline completes the line", ldisc_feed(&ld, '\n'));
    test_report(TEST_LDISC_TAG, "ready once the line is complete", ldisc_ready(&ld));
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "drain returns the line and its \\n", n == 3 && memcmp(out, "hi\n", 3) == 0);
    test_report(TEST_LDISC_TAG, "drained ldisc is not ready", !ldisc_ready(&ld));

    // DEL is what -serial stdio actually sends, back over a typo and retype it
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld,
                        "helllo\x7f\x7f"
                        "o\n");
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "DEL erases back over a typo", n == 6 && memcmp(out, "hello\n", 6) == 0);

    // BS is Ctrl-H, literal split so the hex escape does not swallow the 'c'
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld,
                        "ab\x08"
                        "c\n");
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "BS erases the last byte too", n == 3 && memcmp(out, "ac\n", 3) == 0);

    // len is unsigned, an underflow here would show up as a huge drain
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld,
                        "\x7f\x7f\x7f"
                        "ok\n");
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "backspace on an empty line no-ops", n == 3 && memcmp(out, "ok\n", 3) == 0);

    // CR or LF depending on the terminal, both end a line, both stored as \n
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld, "cr\r");
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "CR ends the line, stored as \\n", n == 3 && memcmp(out, "cr\n", 3) == 0);

    // every other control byte is dropped, 'b' and 'c' are hex digits so split again
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld,
                        "a\x01"
                        "b\x1f"
                        "c\n");
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "other control bytes are dropped", n == 4 && memcmp(out, "abc\n", 4) == 0);

    // a reader asking for less than a line gets the rest on its next call
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld, "abcdef\n");
    n = ldisc_drain(&ld, out, 4);
    test_report(TEST_LDISC_TAG, "short drain takes only what fits", n == 4 && memcmp(out, "abcd", 4) == 0);
    test_report(TEST_LDISC_TAG, "line stays ready while undrained", ldisc_ready(&ld));
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "next drain returns the remainder", n == 3 && memcmp(out, "ef\n", 3) == 0);
    test_report(TEST_LDISC_TAG, "fully drained line clears ready", !ldisc_ready(&ld));

    // a finished line blocks the next one until someone drains it
    ldisc_init(&ld, NULL);
    test_ldisc_feed_str(&ld, "one\n");
    test_report(TEST_LDISC_TAG, "feed on a complete line reports done", ldisc_feed(&ld, 'x'));
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "bytes typed before a drain are lost", n == 4 && memcmp(out, "one\n", 4) == 0);

    // overflow must cap, never run past buf, and still leave room for the \n
    ldisc_init(&ld, NULL);
    for (uint64_t i = 0; i < LDISC_BUF_SIZE * 2; i++) ldisc_feed(&ld, 'x');
    test_report(TEST_LDISC_TAG, "a full buffer does not complete", !ldisc_ready(&ld));
    test_report(TEST_LDISC_TAG, "the \\n still fits after overflow", ldisc_feed(&ld, '\n'));
    n = ldisc_drain(&ld, out, sizeof(out));
    test_report(TEST_LDISC_TAG, "overflowed line caps at buf size", n == LDISC_BUF_SIZE && out[n - 1] == '\n');

    bool kept = true;
    for (uint64_t i = 0; i + 1 < n; i++) {
        if (out[i] != 'x') kept = false;
    }
    test_report(TEST_LDISC_TAG, "overflowed line keeps its bytes", kept);

    // echo: typed bytes come back, DEL paints over what it removed
    test_ldisc_echo_reset();
    ldisc_init(&ld, test_ldisc_echo);
    test_ldisc_feed_str(&ld, "hi\x7f\n");
    test_report(TEST_LDISC_TAG, "echo shows a destructive backspace",
                test_ldisc_echo_len == 6 && memcmp(test_ldisc_echo_buf, "hi\b \b\n", 6) == 0);

    // nothing was erased, so nothing should be painted over
    test_ldisc_echo_reset();
    ldisc_init(&ld, test_ldisc_echo);
    ldisc_feed(&ld, 0x7f);
    test_report(TEST_LDISC_TAG, "backspace on empty echoes nothing", test_ldisc_echo_len == 0);

    // a dropped byte must stay invisible
    test_ldisc_echo_reset();
    ldisc_init(&ld, test_ldisc_echo);
    ldisc_feed(&ld, 0x01);
    test_report(TEST_LDISC_TAG, "dropped control bytes do not echo", test_ldisc_echo_len == 0);

    // two instances, no shared state
    struct ldisc other;
    ldisc_init(&ld, NULL);
    ldisc_init(&other, NULL);
    test_ldisc_feed_str(&ld, "mine\n");
    test_report(TEST_LDISC_TAG, "instances do not share state", !ldisc_ready(&other));
}
