# This file has been generated in whole or in part by a Large Language Model
# Serial bootstrap builds avoid stressing RAM and processes on a small guest.
.PHONY: all kernel userland verify-source image
all: kernel userland
kernel:
	sh buildutils/selfhost-build.sh kernel
userland:
	sh buildutils/selfhost-build.sh userland
verify-source:
	sha256sum -c SOURCE-SHA256SUMS
image:
	sh buildutils/selfhost-image-guest.sh
