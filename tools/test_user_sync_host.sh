#!/bin/sh
set -eu
mkdir -p build/user-sync
g++ -std=c++20 -g -O1 -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isdk/sacx/include tools/user_sync_host_tests.cpp -o build/user-sync/host-tests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 timeout 45s ./build/user-sync/host-tests
