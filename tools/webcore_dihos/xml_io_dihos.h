#pragma once
/* The XML parser accepts memory/callback streams. This profile has no POSIX
 * descriptor or directory authority. These are explicit failures, never I/O
 * emulation or successful no-ops. Do not export POSIX symbols to other code. */
#include <stddef.h>
#include <errno.h>
/* Implemented by the separately verified native libm archive. */
double log10(double);
#ifndef ENOSYS
#define ENOSYS 38
#endif
static inline int dihos_xml_read(int fd, void *buffer, size_t size)
{ (void)fd; (void)buffer; (void)size; errno = ENOSYS; return -1; }
static inline int dihos_xml_write(int fd, const void *buffer, size_t size)
{ (void)fd; (void)buffer; (void)size; errno = ENOSYS; return -1; }
static inline int dihos_xml_close(int fd)
{ (void)fd; errno = ENOSYS; return -1; }
static inline char *dihos_xml_getcwd(char *buffer, size_t size)
{ (void)buffer; (void)size; errno = ENOSYS; return NULL; }
#define read dihos_xml_read
#define write dihos_xml_write
#define close dihos_xml_close
#define getcwd dihos_xml_getcwd
/* The native C subset does not export labs yet. */
static inline long dihos_xml_labs(long value) { return value < 0 ? -value : value; }
#define labs dihos_xml_labs
