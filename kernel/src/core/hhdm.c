#include "core/hhdm.h"

#include "core/boot.h"

static uint64_t hhdm_offset = 0;

void hhdm_init(void) {
    if (!hhdm_request.response) return;
    hhdm_offset = hhdm_request.response->offset;
}

uint64_t hhdm_get_offset(void) {
    return hhdm_offset;
}
