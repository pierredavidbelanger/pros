
ARCH ?= aarch64

include config.$(ARCH).mk

PROGRAMS := $(patsubst %/,%,$(patsubst src/%,%,$(wildcard src/*/)))

.PHONY: all
all: $(PROGRAMS:%=bin/$(ARCH)/%)

define PROGRAM_RULE
bin/$(ARCH)/$(1): src/$(1)/*.c
	mkdir -p bin/$(ARCH)
	$$(CC) -nostdinc -ffreestanding $$(CFLAGS) -I include $$^ $$(LDFLAGS) -o $$@
endef

$(foreach p,$(PROGRAMS),$(eval $(call PROGRAM_RULE,$(p))))

.PHONY: clean
clean:
	rm -rf bin
