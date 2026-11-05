#!/bin/sh
set -eu
mkdir -p build/user-keys
g++ -std=c++20 -g -O1 -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DDIHOS_USER_SYNC_TEST -DDIHOS_USER_KEYS_TEST -Isdk/sacx/include \
  sdk/sacx/user_runtime/thread_keys.cpp tools/user_sync_host_tests.cpp -o build/user-keys/host-tests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 timeout 60s ./build/user-keys/host-tests
