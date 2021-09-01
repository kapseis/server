#!/usr/bin/env bash

set -eux

CC="${CC:-gcc}"

if [[ -e ./build.custom.sh ]]; then
	source ./build.custom.sh
fi

$CC main.c ${CFLAGS:-} -g3 -I.. -o wes -std=gnu17 -O2 -DENABLE_ASSERT -DENABLE_UNREACHABLE -lmbedcrypto -lmbedtls -lmbedx509 -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-overflow=2 -Wformat-truncation=2 -Wformat-security -Wnull-dereference -Wstack-protector -Wtrampolines -Walloca -Wvla -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wshift-overflow=2 -Wcast-qual -Wstringop-overflow=4 -Wconversion -Warith-conversion -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wformat-signedness -Wshadow -Wstrict-overflow=4 -Wundef -Wstrict-prototypes -Wswitch-default -Wswitch-enum -Wstack-usage=1000000 -Wcast-align=strict -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fstack-clash-protection -fPIE -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=leak -fno-omit-frame-pointer -fsanitize=undefined -fsanitize=bounds-strict -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow

