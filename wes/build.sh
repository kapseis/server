#!/usr/bin/env bash

set -eux

CC="${CC:-gcc}"

if [[ -e ./build.custom.sh ]]; then
	source ./build.custom.sh
fi

$CC main.c ${CFLAGS:-} -g3 -fsanitize=address,undefined -fno-omit-frame-pointer -I.. -o wes -std=c17 -DENABLE_ASSERT -DENABLE_UNREACHABLE -lmbedcrypto -lmbedtls -lmbedx509

