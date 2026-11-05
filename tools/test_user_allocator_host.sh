#!/bin/sh
set -eu
# Run from the repository root in native ARM64 Linux/WSL.
mkdir -p build/user-allocator
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DDIHOS_USER_TLSF -Isdk/sacx/user_runtime/include \
  -c third_party/tlsf/tlsf.c -o build/user-allocator/tlsf-host.o
g++ -std=c++20 -g -O1 -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isdk/sacx/user_runtime/include -Ithird_party/tlsf \
  sdk/sacx/user_runtime/allocator.cpp tools/user_allocator_host_tests.cpp \
  build/user-allocator/tlsf-host.o -o build/user-allocator/host-tests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/user-allocator/host-tests
