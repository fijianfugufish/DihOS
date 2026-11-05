#pragma once
extern "C" __attribute__((noreturn)) void dihos_user_libcxx_abort(const char *reason) noexcept;
#define _LIBCPP_ASSERTION_HANDLER(message) ::dihos_user_libcxx_abort(message)
