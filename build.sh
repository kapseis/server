#!/usr/bin/env bash

set -eux

CC="${CC:-gcc}"

if [[ -e ./build.custom.sh ]]; then
	source ./build.custom.sh
fi

$CC main.c ${CFLAGS:-} -g3 -fsanitize=address,undefined -fno-omit-frame-pointer -I. -o server -std=c17 -lmbedcrypto -lmbedtls -lmbedx509

