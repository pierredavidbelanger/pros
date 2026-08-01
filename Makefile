
bash: build
	podman run --rm -it -v ./src:/usr/local/src/pros:Z -w /usr/local/src/pros pros-build bash

build:
	podman build -t pros-build .
