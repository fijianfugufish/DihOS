#pragma once
#include <stdint.h>
#include <stddef.h>
/* Core-0 transport-owned TLS engine. No app pointers or socket operations. */
int knet_tls_begin(const char* host);
void knet_tls_end(void);
int knet_tls_handshake(void); /* 0=complete, 1=needs transport, negative=failure */
int knet_tls_authenticated(void);
int knet_tls_feed(const void*,uint32_t);
uint32_t knet_tls_input_pending(void);
const uint8_t* knet_tls_out(uint32_t*);
void knet_tls_out_ack(uint32_t);
int knet_tls_write(const void*,uint32_t); /* positive=accepted, 0=needs transport */
int knet_tls_read(void*,uint32_t); /* positive=data, 0=needs transport, -2=clean close */
uint32_t knet_tls_verify_flags(void);
int knet_tls_last_error(void);
const char* knet_tls_version(void);
const char* knet_tls_error_description(void);
uint64_t knet_tls_memory_pages(void);
