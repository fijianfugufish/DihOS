#pragma once
#include <stddef.h>
#include <stdint.h>
#include_next <sys/types.h>
/* Private types for Fontconfig's denied filesystem adapter, not a public ABI. */
typedef uint64_t dev_t;
typedef unsigned uid_t;
typedef int pid_t;
