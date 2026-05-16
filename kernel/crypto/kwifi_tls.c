#include "crypto/kwifi_tls.h"

static void ktls_zero(uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
        p[i] = 0u;
}

static void ktls_copy(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
        dst[i] = src[i];
}

static uint16_t ktls_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t ktls_read_be24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static void ktls_set_error(kwifi_tls_state *state)
{
    if (state)
        state->error = 1u;
}

void kwifi_tls_init(kwifi_tls_state *state)
{
    if (!state)
        return;
    ktls_zero((uint8_t *)state, sizeof(*state));
    state->active = 1u;
}

const char *kwifi_tls_version_name(uint8_t major, uint8_t minor)
{
    if (major == 0x03u && minor == 0x01u)
        return "TLS1.0";
    if (major == 0x03u && minor == 0x02u)
        return "TLS1.1";
    if (major == 0x03u && minor == 0x03u)
        return "TLS1.2";
    if (major == 0x03u && minor == 0x04u)
        return "TLS1.3";
    return "TLS?";
}

const char *kwifi_tls_cipher_name(uint16_t cipher_suite)
{
    switch (cipher_suite)
    {
    case 0x0004u:
        return "TLS_RSA_WITH_RC4_128_MD5";
    case 0x0005u:
        return "TLS_RSA_WITH_RC4_128_SHA";
    case 0x000Au:
        return "TLS_RSA_WITH_3DES_EDE_CBC_SHA";
    case 0x002Fu:
        return "TLS_RSA_WITH_AES_128_CBC_SHA";
    case 0x0035u:
        return "TLS_RSA_WITH_AES_256_CBC_SHA";
    case 0x003Cu:
        return "TLS_RSA_WITH_AES_128_CBC_SHA256";
    case 0x003Du:
        return "TLS_RSA_WITH_AES_256_CBC_SHA256";
    case 0x009Cu:
        return "TLS_RSA_WITH_AES_128_GCM_SHA256";
    case 0x009Du:
        return "TLS_RSA_WITH_AES_256_GCM_SHA384";
    case 0xC013u:
        return "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA";
    case 0xC014u:
        return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA";
    case 0xC02Fu:
        return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
    case 0xC030u:
        return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
    default:
        return "TLS_CIPHER_UNKNOWN";
    }
}

uint32_t kwifi_tls_cipher_is_rsa_key_exchange(uint16_t cipher_suite)
{
    switch (cipher_suite)
    {
    case 0x0004u:
    case 0x0005u:
    case 0x000Au:
    case 0x002Fu:
    case 0x0035u:
    case 0x003Cu:
    case 0x003Du:
    case 0x009Cu:
    case 0x009Du:
        return 1u;
    default:
        return 0u;
    }
}

uint32_t kwifi_tls_cipher_is_ecdhe_key_exchange(uint16_t cipher_suite)
{
    switch (cipher_suite)
    {
    case 0xC013u:
    case 0xC014u:
    case 0xC02Fu:
    case 0xC030u:
        return 1u;
    default:
        return 0u;
    }
}

static uint32_t ktls_parse_server_hello(kwifi_tls_state *state, const uint8_t *body, uint32_t len)
{
    uint32_t pos = 0u;
    uint32_t sid_len;

    if (!state || !body || len < 38u)
        return KWIFI_TLS_EVENT_ERROR;

    state->server_major = body[pos++];
    state->server_minor = body[pos++];
    ktls_copy(state->server_random, body + pos, sizeof(state->server_random));
    pos += 32u;

    sid_len = body[pos++];
    if (sid_len > sizeof(state->session_id) || pos + sid_len + 3u > len)
        return KWIFI_TLS_EVENT_ERROR;
    state->session_id_len = (uint8_t)sid_len;
    if (sid_len)
        ktls_copy(state->session_id, body + pos, sid_len);
    pos += sid_len;

    state->cipher_suite = ktls_read_be16(body + pos);
    pos += 2u;
    state->compression_method = body[pos++];
    (void)pos;

    state->server_hello_seen = 1u;
    return KWIFI_TLS_EVENT_SERVER_HELLO;
}

static uint32_t ktls_parse_certificate(kwifi_tls_state *state, const uint8_t *body, uint32_t len)
{
    uint32_t pos = 3u;
    uint32_t chain_len;
    uint32_t count = 0u;

    if (!state || !body || len < 3u)
        return KWIFI_TLS_EVENT_ERROR;

    chain_len = ktls_read_be24(body);
    state->certificate_chain_len = chain_len;
    state->certificate_seen = 1u;

    if (chain_len > len - 3u)
    {
        state->handshake_truncated = 1u;
        return KWIFI_TLS_EVENT_CERTIFICATE;
    }

    while (pos + 3u <= len && pos - 3u < chain_len)
    {
        uint32_t cert_len = ktls_read_be24(body + pos);
        pos += 3u;
        if (count == 0u)
            state->first_certificate_len = cert_len;
        if (pos + cert_len > len)
        {
            state->handshake_truncated = 1u;
            break;
        }
        pos += cert_len;
        count++;
    }

    state->certificate_count = count;
    return KWIFI_TLS_EVENT_CERTIFICATE;
}

static uint32_t ktls_parse_server_key_exchange(kwifi_tls_state *state, const uint8_t *body, uint32_t len)
{
    if (!state)
        return KWIFI_TLS_EVENT_ERROR;

    state->server_key_exchange_seen = 1u;
    state->ecdhe_named_group = 0u;

    if (body && len >= 4u && body[0] == 3u)
        state->ecdhe_named_group = ktls_read_be16(body + 1u);

    return KWIFI_TLS_EVENT_SERVER_KEY_EXCHANGE;
}

static uint32_t ktls_process_handshake(kwifi_tls_state *state,
                                       uint8_t type,
                                       const uint8_t *body,
                                       uint32_t len,
                                       uint8_t truncated)
{
    uint32_t event = 0u;

    if (!state)
        return KWIFI_TLS_EVENT_ERROR;

    state->handshakes_seen++;
    if (truncated)
        state->handshake_truncated = 1u;

    if (type == 2u)
        event = ktls_parse_server_hello(state, body, len);
    else if (type == 11u)
        event = ktls_parse_certificate(state, body, len);
    else if (type == 12u)
        event = ktls_parse_server_key_exchange(state, body, len);
    else if (type == 13u)
    {
        state->certificate_request_seen = 1u;
        event = KWIFI_TLS_EVENT_CERTIFICATE_REQUEST;
    }
    else if (type == 14u)
    {
        state->server_hello_done_seen = 1u;
        event = KWIFI_TLS_EVENT_SERVER_HELLO_DONE;
    }

    if (event & KWIFI_TLS_EVENT_ERROR)
        ktls_set_error(state);
    return event;
}

static uint32_t ktls_feed_handshake(kwifi_tls_state *state, const uint8_t *data, uint32_t len)
{
    uint32_t events = 0u;
    uint32_t pos = 0u;

    if (!state || (!data && len))
        return KWIFI_TLS_EVENT_ERROR;

    while (pos < len)
    {
        if (state->handshake_header_have < 4u)
        {
            state->handshake_header[state->handshake_header_have++] = data[pos++];
            if (state->handshake_header_have < 4u)
                continue;

            state->handshake_type = state->handshake_header[0];
            state->handshake_len = ktls_read_be24(state->handshake_header + 1u);
            state->handshake_seen = 0u;
            state->handshake_truncated = 0u;
            if (state->handshake_len == 0u)
            {
                events |= ktls_process_handshake(state, state->handshake_type, 0, 0u, 0u);
                state->handshake_header_have = 0u;
            }
        }
        else
        {
            uint32_t remain = state->handshake_len - state->handshake_seen;
            uint32_t take = len - pos;
            uint32_t capture_left;

            if (take > remain)
                take = remain;

            capture_left = (state->handshake_seen < KWIFI_TLS_HANDSHAKE_CAPTURE_MAX)
                               ? (KWIFI_TLS_HANDSHAKE_CAPTURE_MAX - state->handshake_seen)
                               : 0u;
            if (capture_left)
            {
                uint32_t copy_len = take;
                if (copy_len > capture_left)
                    copy_len = capture_left;
                ktls_copy(state->handshake_capture + state->handshake_seen, data + pos, copy_len);
            }
            if (state->handshake_seen + take > KWIFI_TLS_HANDSHAKE_CAPTURE_MAX)
                state->handshake_truncated = 1u;

            state->handshake_seen += take;
            pos += take;

            if (state->handshake_seen == state->handshake_len)
            {
                uint32_t captured = state->handshake_len;
                if (captured > KWIFI_TLS_HANDSHAKE_CAPTURE_MAX)
                    captured = KWIFI_TLS_HANDSHAKE_CAPTURE_MAX;
                events |= ktls_process_handshake(state,
                                                 state->handshake_type,
                                                 state->handshake_capture,
                                                 captured,
                                                 state->handshake_truncated);
                state->handshake_header_have = 0u;
                state->handshake_type = 0u;
                state->handshake_len = 0u;
                state->handshake_seen = 0u;
            }
        }

        if (state->error)
            return events | KWIFI_TLS_EVENT_ERROR;
    }

    return events;
}

static uint32_t ktls_process_record(kwifi_tls_state *state)
{
    uint32_t events = KWIFI_TLS_EVENT_RECORD;

    if (!state)
        return KWIFI_TLS_EVENT_ERROR;

    state->records_seen++;
    if (state->record_type == 22u)
    {
        events |= ktls_feed_handshake(state, state->record_buf, state->record_len);
    }
    else if (state->record_type == 21u)
    {
        state->alert_seen = 1u;
        if (state->record_len >= 2u)
        {
            state->alert_level = state->record_buf[0];
            state->alert_description = state->record_buf[1];
            if (state->alert_level == 2u)
                state->error = 1u;
        }
        events |= KWIFI_TLS_EVENT_ALERT;
    }
    else if (state->record_type == 20u)
    {
        state->change_cipher_spec_seen = 1u;
        events |= KWIFI_TLS_EVENT_CHANGE_CIPHER_SPEC;
    }
    else if (state->record_type == 23u)
    {
        state->app_data_seen = 1u;
    }
    else
    {
        ktls_set_error(state);
        events |= KWIFI_TLS_EVENT_ERROR;
    }

    if (state->error)
        events |= KWIFI_TLS_EVENT_ERROR;
    return events;
}

uint32_t kwifi_tls_feed(kwifi_tls_state *state, const uint8_t *data, uint32_t len)
{
    uint32_t pos = 0u;
    uint32_t events = 0u;

    if (!state || (!data && len))
        return KWIFI_TLS_EVENT_ERROR;
    if (!state->active)
        kwifi_tls_init(state);
    if (state->error)
        return KWIFI_TLS_EVENT_ERROR;

    while (pos < len)
    {
        if (state->record_header_have < 5u)
        {
            state->record_header[state->record_header_have++] = data[pos++];
            if (state->record_header_have < 5u)
                continue;

            state->record_type = state->record_header[0];
            state->record_major = state->record_header[1];
            state->record_minor = state->record_header[2];
            state->record_len = ktls_read_be16(state->record_header + 3u);
            state->record_have = 0u;
            if (state->record_len > KWIFI_TLS_RECORD_MAX)
            {
                ktls_set_error(state);
                return events | KWIFI_TLS_EVENT_ERROR;
            }
            if (state->record_len == 0u)
            {
                events |= ktls_process_record(state);
                state->record_header_have = 0u;
            }
        }
        else
        {
            uint32_t remain = (uint32_t)state->record_len - (uint32_t)state->record_have;
            uint32_t take = len - pos;

            if (take > remain)
                take = remain;
            ktls_copy(state->record_buf + state->record_have, data + pos, take);
            state->record_have = (uint16_t)(state->record_have + take);
            state->total_in += take;
            pos += take;

            if (state->record_have == state->record_len)
            {
                events |= ktls_process_record(state);
                state->record_header_have = 0u;
                state->record_have = 0u;
                state->record_len = 0u;
            }
        }

        if (state->error)
            return events | KWIFI_TLS_EVENT_ERROR;
    }

    return events;
}
