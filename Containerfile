FROM docker.io/library/ubuntu:resolute

RUN apt-get -y update && apt-get -y install \
      make build-essential \
      gcc-arm-none-eabi \
      gcc-riscv64-unknown-elf \
      qemu-system \
      u-boot-tools && \
    apt-get clean
