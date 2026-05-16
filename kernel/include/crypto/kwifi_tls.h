#pragma once

#include <stdint.h>

#define KWIFI_TLS_RECORD_MAX 18432u
#define KWIFI_TLS_HANDSHAKE_CAPTURE_MAX 4096u

#define KWIFI_TLS_EVENT_RECORD 0x00000001u
#define KWIFI_TLS_EVENT_SERVER_HELLO 0x00000002u
#define KWIFI_TLS_EVENT_CERTIFICATE 0x00000004u
#define KWIFI_TLS_EVENT_SERVER_KEY_EXCHANGE 0x00000008u
#define KWIFI_TLS_EVENT_CERTIFICATE_REQUEST 0x00000010u
#define KWIFI_TLS_EVENT_SERVER_HELLO_DONE 0x00000020u
#define KWIFI_TLS_EVENT_ALERT 0x00000040u
#define KWIFI_TLS_EVENT_CHANGE_CIPHER_SPEC 0x00000080u
#define KWIFI_TLS_EVENT_ERROR 0x80000000u

typedef struct
{
    uint8_t active;
    uint8_t error;
    uint8_t alert_seen;
    uint8_t alert_level;
    uint8_t alert_description;
    uint8_t server_hello_seen;
    uint8_t certificate_seen;
    uint8_t server_key_exchange_seen;
    uint8_t certificate_request_seen;
    uint8_t server_hello_done_seen;
    uint8_t change_cipher_spec_seen;
    uint8_t app_data_seen;
    uint8_t server_major;
    uint8_t server_minor;
    uint8_t record_major;
    uint8_t record_minor;
    uint8_t compression_method;
    uint8_t handshake_type;
    uint8_t handshake_header_have;
    uint8_t handshake_truncated;
    uint16_t cipher_suite;
    uint16_t ecdhe_named_group;
    uint16_t record_len;
    uint16_t record_have;
    uint8_t record_type;
    uint8_t record_header_have;
    uint32_t total_in;
    uint32_t records_seen;
    uint32_t handshakes_seen;
    uint32_t handshake_len;
    uint32_t handshake_seen;
    uint32_t certificate_chain_len;
    uint32_t certificate_count;
    uint32_t first_certificate_len;
    uint8_t server_random[32];
    uint8_t session_id[32];
    uint8_t session_id_len;
    uint8_t record_header[5];
    uint8_t handshake_header[4];
    uint8_t handshake_capture[KWIFI_TLS_HANDSHAKE_CAPTURE_MAX];
    uint8_t record_buf[KWIFI_TLS_RECORD_MAX];
} kwifi_tls_state;

void kwifi_tls_init(kwifi_tls_state *state);
uint32_t kwifi_tls_feed(kwifi_tls_state *state, const uint8_t *data, uint32_t len);
const char *kwifi_tls_cipher_name(uint16_t cipher_suite);
const char *kwifi_tls_version_name(uint8_t major, uint8_t minor);
uint32_t kwifi_tls_cipher_is_rsa_key_exchange(uint16_t cipher_suite);
uint32_t kwifi_tls_cipher_is_ecdhe_key_exchange(uint16_t cipher_suite);
