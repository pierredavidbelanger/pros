
ARCH ?= aarch64

.PHONY: default
default: bin/$(ARCH)/initrd

../user/bin/$(ARCH)/init:
	$(MAKE) -C ../user ARCH=$(ARCH)

bin/$(ARCH)/initrd: ../user/bin/$(ARCH)/init $(shell find data -type f)
	mkdir -p bin/$(ARCH)/stage/bin
	cp -r data/* bin/$(ARCH)/stage/
	cp ../user/bin/$(ARCH)/* bin/$(ARCH)/stage/bin/
	cd bin/$(ARCH)/stage && tar --format=ustar -cf ../initrd * && cd ../../..

.PHONY: clean
clean:
	rm -rf bin
