
ARCH ?= aarch64

include config.$(ARCH).mk

PROGRAMS := $(filter-out bin,$(patsubst %/,%,$(wildcard */)))

.PHONY: default
default: $(PROGRAMS:%=bin/$(ARCH)/%)

define PROGRAM_RULE
bin/$(ARCH)/$(1): $(1)/$(1).c
	mkdir -p bin/$(ARCH)
	$$(CC) -nostdinc -ffreestanding $$< $$(LDFLAGS) -o $$@
endef

$(foreach p,$(PROGRAMS),$(eval $(call PROGRAM_RULE,$(p))))

.PHONY: clean
clean:
	rm -rf bin
