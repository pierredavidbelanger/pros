
ARCH ?= aarch64

.PHONY: all
all: bin/$(ARCH)/initrd

USER_BIN := ../user/bin/$(ARCH)
USER_PROGRAMS := $(wildcard $(USER_BIN)/*)

bin/$(ARCH)/initrd: $(USER_PROGRAMS) $(shell find data -type f)
	test -n "$(USER_PROGRAMS)" || { echo "no programs in $(USER_BIN) : build user first (ARCH=$(ARCH))" >&2; exit 1; }
	mkdir -p bin/$(ARCH)/stage/bin
	cp -r data/* bin/$(ARCH)/stage/
	cp $(USER_PROGRAMS) bin/$(ARCH)/stage/bin/
	cd bin/$(ARCH)/stage && tar --format=ustar -cf ../initrd * && cd ../../..

.PHONY: clean
clean:
	rm -rf bin
