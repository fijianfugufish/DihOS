#include <stdint.h>

/*
 * Freestanding Stage2 does not link the usual CRT/compiler-rt helpers.
 * Clang/LLD may still emit stack-probe references for large frames.
 * A no-op probe is sufficient here because Stage2 controls its stack.
 */
void __chkstk(void) {}
void __chkstk_ms(void) {}

