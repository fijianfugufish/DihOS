#include "wifi/kwifi.h"
#include "asm/asm.h"
#include "crypto/kwifi_crypto.h"
#include "crypto/kwifi_tls.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"
#include "pci/pci_kernel_nic_probe.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"

#include <stdint.h>

#define KWIFI_CONNECT_SSID_MAX 64u
#define KWIFI_CONNECT_USER_MAX 96u
#define KWIFI_CONNECT_PASS_MAX 128u

#define KWIFI_EAPOL_TYPE_EAP_PACKET 0u
#define KWIFI_EAPOL_TYPE_KEY 3u
#define KWIFI_EAP_CODE_REQUEST 1u
#define KWIFI_EAP_CODE_RESPONSE 2u
#define KWIFI_EAP_CODE_SUCCESS 3u
#define KWIFI_EAP_CODE_FAILURE 4u
#define KWIFI_EAP_TYPE_IDENTITY 1u
#define KWIFI_EAP_TYPE_PEAP 25u
#define KWIFI_PEAP_FLAG_LENGTH 0x80u
#define KWIFI_PEAP_FLAG_MORE 0x40u
#define KWIFI_PEAP_FLAG_START 0x20u
#define KWIFI_PEAP_VERSION 0u
#define KWIFI_TLS_CLIENT_HELLO_MAX 256u

typedef struct
{
    uint8_t requested;
    uint8_t connected;
    uint8_t enterprise;
    uint8_t eap_phase;
    uint8_t peap_phase;
    uint8_t control_port_authorized;
    uint8_t supplicant_ready;
    uint8_t eapol_start_sent;
    uint8_t eapol_start_attempts;
    uint8_t eapol_poll_ticks;
    uint8_t eap_identity_sent;
    uint8_t eap_peap_seen;
    uint8_t eapol_key_seen;
    uint8_t eap_last_identifier;
    uint8_t peap_version;
    uint8_t peap_tls_clienthello_sent;
    uint8_t peap_tls_fragment_ack_sent;
    uint8_t peap_tls_server_data_seen;
    uint8_t mschap_nt_hash_ready;
    uint8_t beacon_miss_count;
    uint8_t reconnect_attempts;
    uint8_t assoc_kick_attempts;
    uint16_t peap_clienthello_len;
    uint32_t peap_tls_rx_bytes;
    uint32_t peap_tls_rx_total_len;
    char ssid[KWIFI_CONNECT_SSID_MAX];
    char username[KWIFI_CONNECT_USER_MAX];
    char password[KWIFI_CONNECT_PASS_MAX];
    uint8_t mschap_nt_hash[16];
    uint8_t peap_clienthello[KWIFI_TLS_CLIENT_HELLO_MAX];
    kwifi_tls_state tls;
    char auth_mode[24];
    char eap_phase_text[32];
    char peap_phase_text[40];
    char status[96];
} kwifi_connect_state;

static kwifi_connect_state g_kwifi_connect;
static const uint32_t g_kwifi_enterprise_fallback_channels_mhz[] = {
    5200u, 5180u, 5220u, 5240u, 5745u, 5680u};

static uint32_t kwifi_try_assoc_kick(void)
{
    uint32_t assoc_done;

    assoc_done = pci_kernel_wifi_peer_assoc_done();
    if (assoc_done || !g_kwifi_connect.requested || !g_kwifi_connect.ssid[0])
        return assoc_done;
    if (g_kwifi_connect.assoc_kick_attempts >= 3u)
        return assoc_done;

    g_kwifi_connect.assoc_kick_attempts++;
    terminal_print("[K:WIFI] enterprise assoc kick attempt=");
    terminal_print_inline_hex64(g_kwifi_connect.assoc_kick_attempts);
    terminal_print(" ssid=");
    terminal_print(g_kwifi_connect.ssid);
    terminal_flush_log();

    if (pci_kernel_wifi_connect_ssid(g_kwifi_connect.ssid))
        (void)pci_kernel_wifi_poll_events(128u);

    return pci_kernel_wifi_peer_assoc_done();
}

static uint64_t kwifi_pages_for_bytes(uint64_t bytes)
{
    return (bytes + 4095ull) >> 12;
}

static uint32_t kwifi_cstr_len_cap(const char *text, uint32_t cap)
{
    uint32_t len = 0u;

    if (!text)
        return 0u;

    while (text[len] && len < cap)
        ++len;
    return len;
}

static int kwifi_str_eq(const char *a, const char *b)
{
    uint32_t i = 0u;

    if (!a || !b)
        return 0;

    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        ++i;
    }

    return a[i] == b[i];
}

static void kwifi_copy_trunc(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;

    if (!dst || cap == 0u)
        return;
    if (!src)
        src = "";

    while (src[i] && i + 1u < cap)
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int kwifi_hex_nibble(char ch, uint8_t *out)
{
    if (!out)
        return 0;
    if (ch >= '0' && ch <= '9')
    {
        *out = (uint8_t)(ch - '0');
        return 1;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        *out = (uint8_t)(ch - 'a' + 10);
        return 1;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        *out = (uint8_t)(ch - 'A' + 10);
        return 1;
    }
    return 0;
}

static int kwifi_parse_bssid(const char *text, uint8_t out[6])
{
    uint32_t pos = 0u;
    uint32_t bi = 0u;

    if (!text || !out)
        return 0;

    while (text[pos] && bi < 6u)
    {
        uint8_t hi;
        uint8_t lo;

        if (!kwifi_hex_nibble(text[pos], &hi) || !text[pos + 1u] || !kwifi_hex_nibble(text[pos + 1u], &lo))
            return 0;
        out[bi++] = (uint8_t)((hi << 4) | lo);
        pos += 2u;
        if (bi < 6u)
        {
            if (text[pos] != ':' && text[pos] != '-')
                return 0;
            pos++;
        }
    }

    return (bi == 6u && text[pos] == 0) ? 1 : 0;
}

static int kwifi_parse_u32(const char *text, uint32_t *out)
{
    uint64_t acc = 0u;
    uint32_t i = 0u;

    if (!text || !text[0] || !out)
        return 0;

    while (text[i])
    {
        char ch = text[i++];
        if (ch < '0' || ch > '9')
            return 0;
        acc = acc * 10u + (uint64_t)(ch - '0');
        if (acc > 0xFFFFFFFFull)
            return 0;
    }

    *out = (uint32_t)acc;
    return 1;
}

static uint32_t kwifi_have_visible_network(const char *ssid)
{
    uint32_t count = kwifi_network_count();

    for (uint32_t i = 0u; i < count; ++i)
    {
        const char *name;
        if (kwifi_network_hidden(i))
            continue;

        name = kwifi_network_name(i);
        if (name && kwifi_str_eq(name, ssid))
            return 1u;
    }

    return 0u;
}

static void kwifi_set_status(const char *status)
{
    kwifi_copy_trunc(g_kwifi_connect.status, sizeof(g_kwifi_connect.status), status);
}

static void kwifi_set_eap_phase(uint8_t phase, const char *name)
{
    g_kwifi_connect.eap_phase = phase;
    kwifi_copy_trunc(g_kwifi_connect.eap_phase_text, sizeof(g_kwifi_connect.eap_phase_text), name ? name : "none");
}

static void kwifi_set_peap_phase(uint8_t phase, const char *name)
{
    g_kwifi_connect.peap_phase = phase;
    kwifi_copy_trunc(g_kwifi_connect.peap_phase_text, sizeof(g_kwifi_connect.peap_phase_text), name ? name : "none");
}

static uint16_t kwifi_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void kwifi_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFu);
    p[1] = (uint8_t)(value & 0xFFu);
}

static uint32_t kwifi_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void kwifi_write_be24(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)((value >> 16) & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)(value & 0xFFu);
}

static void kwifi_write_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)((value >> 24) & 0xFFu);
    p[1] = (uint8_t)((value >> 16) & 0xFFu);
    p[2] = (uint8_t)((value >> 8) & 0xFFu);
    p[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t kwifi_eapol_header_plausible(const uint8_t *p, uint32_t len)
{
    uint32_t body_len;

    if (!p || len < 4u)
        return 0u;
    if (p[0] < 1u || p[0] > 3u)
        return 0u;
    if (p[1] > 4u)
        return 0u;

    body_len = kwifi_read_be16(p + 2u);
    if (body_len + 4u > len)
        return 0u;

    if (p[1] == KWIFI_EAPOL_TYPE_EAP_PACKET)
    {
        uint32_t eap_len;

        if (body_len < 4u)
            return 0u;
        if (p[4] < KWIFI_EAP_CODE_REQUEST || p[4] > KWIFI_EAP_CODE_FAILURE)
            return 0u;
        eap_len = kwifi_read_be16(p + 6u);
        if (eap_len < 4u || eap_len > body_len)
            return 0u;
    }
    else if (p[1] == KWIFI_EAPOL_TYPE_KEY && body_len < 40u)
    {
        return 0u;
    }
    else if ((p[1] == 1u || p[1] == 2u) && body_len != 0u)
    {
        return 0u;
    }

    return 1u;
}

static uint32_t kwifi_find_eapol_payload(const uint8_t *frame,
                                         uint32_t len,
                                         const uint8_t **out,
                                         uint32_t *out_len)
{
    static const uint8_t snap_eapol[8] = {0xAAu, 0xAAu, 0x03u, 0x00u, 0x00u, 0x00u, 0x88u, 0x8Eu};
    uint16_t ethertype;

    if (!frame || !out || !out_len)
        return 0u;

    if (len >= 18u)
    {
        ethertype = (uint16_t)(((uint16_t)frame[12] << 8) | (uint16_t)frame[13]);
        if (ethertype == 0x888Eu)
        {
            *out = frame + 14u;
            *out_len = len - 14u;
            return 1u;
        }
    }

    if (len >= 32u)
    {
        uint16_t fc = (uint16_t)(((uint16_t)frame[1] << 8) | (uint16_t)frame[0]);
        uint32_t type = (fc >> 2) & 0x3u;
        uint32_t subtype = (fc >> 4) & 0xFu;
        uint32_t hdr_len = 24u;

        if (type == 2u)
        {
            if (subtype & 0x8u)
                hdr_len += 2u;
            if ((fc & 0x8000u) && (subtype & 0x8u))
                hdr_len += 4u;

            if (hdr_len + sizeof(snap_eapol) + 4u <= len &&
                frame[hdr_len + 0u] == 0xAAu &&
                frame[hdr_len + 1u] == 0xAAu &&
                frame[hdr_len + 2u] == 0x03u &&
                frame[hdr_len + 3u] == 0x00u &&
                frame[hdr_len + 4u] == 0x00u &&
                frame[hdr_len + 5u] == 0x00u &&
                frame[hdr_len + 6u] == 0x88u &&
                frame[hdr_len + 7u] == 0x8Eu &&
                kwifi_eapol_header_plausible(frame + hdr_len + sizeof(snap_eapol),
                                             len - hdr_len - (uint32_t)sizeof(snap_eapol)))
            {
                *out = frame + hdr_len + sizeof(snap_eapol);
                *out_len = len - hdr_len - (uint32_t)sizeof(snap_eapol);
                return 1u;
            }
        }
    }

    if (len >= 36u &&
        frame[24] == 0xAAu && frame[25] == 0xAAu && frame[26] == 0x03u &&
        frame[30] == 0x88u && frame[31] == 0x8Eu &&
        kwifi_eapol_header_plausible(frame + 32u, len - 32u))
    {
        *out = frame + 32u;
        *out_len = len - 32u;
        return 1u;
    }

    if (len >= 4u && kwifi_eapol_header_plausible(frame, len))
    {
        *out = frame;
        *out_len = len;
        return 1u;
    }

    for (uint32_t off = 0u; off + sizeof(snap_eapol) + 4u <= len; ++off)
    {
        uint32_t match = 1u;
        for (uint32_t i = 0u; i < sizeof(snap_eapol); ++i)
        {
            if (frame[off + i] != snap_eapol[i])
            {
                match = 0u;
                break;
            }
        }

        if (match &&
            kwifi_eapol_header_plausible(frame + off + sizeof(snap_eapol),
                                         len - off - (uint32_t)sizeof(snap_eapol)))
        {
            *out = frame + off + sizeof(snap_eapol);
            *out_len = len - off - (uint32_t)sizeof(snap_eapol);
            return 1u;
        }
    }

    for (uint32_t off = 0u; off + 4u <= len; ++off)
    {
        if (kwifi_eapol_header_plausible(frame + off, len - off))
        {
            *out = frame + off;
            *out_len = len - off;
            return 1u;
        }
    }

    return 0u;
}

static int kwifi_send_eap_response_identity(uint8_t identifier)
{
    uint8_t frame[256];
    uint8_t src[6];
    uint8_t dst[6];
    uint32_t user_len;
    uint32_t eap_len;
    uint32_t total_len;
    uint32_t tx_len;
    uint8_t have_bssid;
    static const uint8_t eapol_pae_group[6] = {0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x03u};

    if (g_kwifi_connect.eap_identity_sent)
        return 1;
    if (!pci_kernel_wifi_get_local_mac(src))
        return 0;

    have_bssid = pci_kernel_wifi_get_bssid(dst) ? 1u : 0u;
    user_len = kwifi_cstr_len_cap(g_kwifi_connect.username, KWIFI_CONNECT_USER_MAX - 1u);
    eap_len = 5u + user_len;
    total_len = 14u + 4u + eap_len;
    if (total_len > sizeof(frame))
        return 0;

    for (uint32_t i = 0u; i < sizeof(frame); ++i)
        frame[i] = 0u;
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        frame[i] = have_bssid ? dst[i] : eapol_pae_group[i];
        frame[6u + i] = src[i];
    }
    frame[12] = 0x88u;
    frame[13] = 0x8Eu;
    frame[14] = 0x02u; /* EAPOL v2 */
    frame[15] = KWIFI_EAPOL_TYPE_EAP_PACKET;
    kwifi_write_be16(frame + 16u, (uint16_t)eap_len);
    frame[18] = KWIFI_EAP_CODE_RESPONSE;
    frame[19] = identifier;
    kwifi_write_be16(frame + 20u, (uint16_t)eap_len);
    frame[22] = KWIFI_EAP_TYPE_IDENTITY;
    for (uint32_t i = 0u; i < user_len; ++i)
        frame[23u + i] = (uint8_t)g_kwifi_connect.username[i];

    tx_len = total_len < 60u ? 60u : total_len;
    if (!pci_kernel_wifi_tx_l2_frame(frame, tx_len))
        return 0;

    g_kwifi_connect.eap_identity_sent = 1u;
    terminal_print("[K:WIFI] EAP Response/Identity queued id=");
    terminal_print_inline_hex64(identifier);
    terminal_print(" user_len=");
    terminal_print_inline_hex64(user_len);
    terminal_flush_log();
    return 1;
}

static uint32_t kwifi_peap_seed_from_state(void)
{
    uint8_t mac[6];
    uint32_t seed = (uint32_t)dihos_time_ticks() ^ 0xA5C35A3Du;

    if (pci_kernel_wifi_get_local_mac(mac))
    {
        for (uint32_t i = 0u; i < sizeof(mac); ++i)
            seed = (seed * 16777619u) ^ (uint32_t)mac[i];
    }

    for (uint32_t i = 0u; g_kwifi_connect.ssid[i] && i < KWIFI_CONNECT_SSID_MAX; ++i)
        seed = (seed * 16777619u) ^ (uint8_t)g_kwifi_connect.ssid[i];
    for (uint32_t i = 0u; g_kwifi_connect.username[i] && i < KWIFI_CONNECT_USER_MAX; ++i)
        seed = (seed * 16777619u) ^ (uint8_t)g_kwifi_connect.username[i];
    for (uint32_t i = 0u; i < sizeof(g_kwifi_connect.mschap_nt_hash); ++i)
        seed = (seed * 16777619u) ^ g_kwifi_connect.mschap_nt_hash[i];

    return seed ? seed : 0x6D2B79F5u;
}

static uint32_t kwifi_peap_prng_next(uint32_t *state)
{
    uint32_t x = state && *state ? *state : 0x6D2B79F5u;

    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    if (state)
        *state = x ? x : 0x6D2B79F5u;
    return x;
}

static uint32_t kwifi_build_tls_client_hello(uint8_t *out, uint32_t out_cap)
{
    static const uint16_t cipher_suites[] = {
        0x002Fu, 0x0035u, 0x003Cu, 0x003Du,
        0x009Cu, 0x009Du, 0x000Au, 0x0005u,
        0x0004u, 0x00FFu};
    static const uint16_t groups[] = {0x001Du, 0x0017u, 0x0018u};
    static const uint16_t sig_algs[] = {0x0401u, 0x0501u, 0x0601u, 0x0201u, 0x0403u, 0x0503u};
    uint32_t p = 0u;
    uint32_t rec_len_pos;
    uint32_t hs_start;
    uint32_t hs_len_pos;
    uint32_t body_start;
    uint32_t cipher_len_pos;
    uint32_t ext_len_pos;
    uint32_t ext_start;
    uint32_t seed;

    if (!out || out_cap < KWIFI_TLS_CLIENT_HELLO_MAX)
        return 0u;

    out[p++] = 22u;   /* handshake record */
    out[p++] = 0x03u; /* TLS record legacy version 1.0 */
    out[p++] = 0x01u;
    rec_len_pos = p;
    p += 2u;

    hs_start = p;
    out[p++] = 1u; /* ClientHello */
    hs_len_pos = p;
    p += 3u;
    body_start = p;

    out[p++] = 0x03u; /* client_version TLS 1.2 */
    out[p++] = 0x03u;

    seed = kwifi_peap_seed_from_state();
    kwifi_write_be32(out + p, (uint32_t)dihos_time_seconds());
    p += 4u;
    for (uint32_t i = 4u; i < 32u; ++i)
    {
        if ((i & 3u) == 0u)
            seed = kwifi_peap_prng_next(&seed);
        out[p++] = (uint8_t)(seed >> ((i & 3u) * 8u));
    }

    out[p++] = 0u; /* session_id length */

    cipher_len_pos = p;
    p += 2u;
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(cipher_suites) / sizeof(cipher_suites[0])); ++i)
    {
        kwifi_write_be16(out + p, cipher_suites[i]);
        p += 2u;
    }
    kwifi_write_be16(out + cipher_len_pos, (uint16_t)(p - cipher_len_pos - 2u));

    out[p++] = 1u; /* compression_methods length */
    out[p++] = 0u; /* null compression */

    ext_len_pos = p;
    p += 2u;
    ext_start = p;

    kwifi_write_be16(out + p, 0x000Au); /* supported_groups */
    p += 2u;
    kwifi_write_be16(out + p, (uint16_t)(2u + sizeof(groups)));
    p += 2u;
    kwifi_write_be16(out + p, (uint16_t)sizeof(groups));
    p += 2u;
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(groups) / sizeof(groups[0])); ++i)
    {
        kwifi_write_be16(out + p, groups[i]);
        p += 2u;
    }

    kwifi_write_be16(out + p, 0x000Bu); /* ec_point_formats */
    p += 2u;
    kwifi_write_be16(out + p, 2u);
    p += 2u;
    out[p++] = 1u;
    out[p++] = 0u;

    kwifi_write_be16(out + p, 0x000Du); /* signature_algorithms */
    p += 2u;
    kwifi_write_be16(out + p, (uint16_t)(2u + sizeof(sig_algs)));
    p += 2u;
    kwifi_write_be16(out + p, (uint16_t)sizeof(sig_algs));
    p += 2u;
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(sig_algs) / sizeof(sig_algs[0])); ++i)
    {
        kwifi_write_be16(out + p, sig_algs[i]);
        p += 2u;
    }

    kwifi_write_be16(out + p, 0x0017u); /* extended_master_secret */
    p += 2u;
    kwifi_write_be16(out + p, 0u);
    p += 2u;

    kwifi_write_be16(out + p, 0xFF01u); /* renegotiation_info */
    p += 2u;
    kwifi_write_be16(out + p, 1u);
    p += 2u;
    out[p++] = 0u;

    kwifi_write_be16(out + ext_len_pos, (uint16_t)(p - ext_start));
    kwifi_write_be24(out + hs_len_pos, p - body_start);
    kwifi_write_be16(out + rec_len_pos, (uint16_t)(p - hs_start));

    return p;
}

static int kwifi_send_eap_peap_tls(uint8_t identifier,
                                   const uint8_t *tls_payload,
                                   uint32_t tls_payload_len,
                                   uint8_t peap_flags,
                                   uint8_t include_tls_len)
{
    uint8_t frame[512];
    uint8_t src[6];
    uint8_t dst[6];
    uint32_t eap_len;
    uint32_t total_len;
    uint32_t pos;
    uint32_t tx_len;
    uint8_t have_bssid;
    static const uint8_t eapol_pae_group[6] = {0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x03u};

    if (!pci_kernel_wifi_get_local_mac(src))
        return 0;
    if (tls_payload_len && !tls_payload)
        return 0;

    eap_len = 5u + 1u + (include_tls_len ? 4u : 0u) + tls_payload_len;
    total_len = 14u + 4u + eap_len;
    if (total_len > sizeof(frame) || eap_len > 0xFFFFu)
        return 0;

    have_bssid = pci_kernel_wifi_get_bssid(dst) ? 1u : 0u;
    for (uint32_t i = 0u; i < sizeof(frame); ++i)
        frame[i] = 0u;
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        frame[i] = have_bssid ? dst[i] : eapol_pae_group[i];
        frame[6u + i] = src[i];
    }

    frame[12] = 0x88u;
    frame[13] = 0x8Eu;
    frame[14] = 0x02u;
    frame[15] = KWIFI_EAPOL_TYPE_EAP_PACKET;
    kwifi_write_be16(frame + 16u, (uint16_t)eap_len);
    frame[18] = KWIFI_EAP_CODE_RESPONSE;
    frame[19] = identifier;
    kwifi_write_be16(frame + 20u, (uint16_t)eap_len);
    frame[22] = KWIFI_EAP_TYPE_PEAP;
    frame[23] = (uint8_t)((peap_flags & 0xFCu) | KWIFI_PEAP_VERSION);
    pos = 24u;

    if (include_tls_len)
    {
        kwifi_write_be32(frame + pos, tls_payload_len);
        pos += 4u;
    }
    for (uint32_t i = 0u; i < tls_payload_len; ++i)
        frame[pos + i] = tls_payload[i];

    tx_len = total_len < 60u ? 60u : total_len;
    if (!pci_kernel_wifi_tx_l2_frame(frame, tx_len))
        return 0;

    terminal_print("[K:WIFI] EAP Response/PEAP queued id=");
    terminal_print_inline_hex64(identifier);
    terminal_print(" flags=");
    terminal_print_inline_hex8(frame[23]);
    terminal_print(" tls_len=");
    terminal_print_inline_hex64(tls_payload_len);
    terminal_flush_log();
    return 1;
}

static int kwifi_send_peap_client_hello(uint8_t identifier)
{
    if (!g_kwifi_connect.tls.active)
        kwifi_tls_init(&g_kwifi_connect.tls);

    if (!g_kwifi_connect.peap_clienthello_len)
    {
        uint32_t tls_len = kwifi_build_tls_client_hello(g_kwifi_connect.peap_clienthello,
                                                        sizeof(g_kwifi_connect.peap_clienthello));
        if (!tls_len)
        {
            kwifi_set_status("PEAP TLS ClientHello build failed");
            return 0;
        }
        g_kwifi_connect.peap_clienthello_len = (uint16_t)tls_len;
    }

    if (!kwifi_send_eap_peap_tls(identifier,
                                 g_kwifi_connect.peap_clienthello,
                                 g_kwifi_connect.peap_clienthello_len,
                                 0u,
                                 0u))
    {
        kwifi_set_status("PEAP TLS ClientHello transmit failed");
        return 0;
    }

    g_kwifi_connect.peap_tls_clienthello_sent = 1u;
    kwifi_set_eap_phase(5u, "peap-tls");
    kwifi_set_peap_phase(6u, "tls-clienthello-sent");
    kwifi_set_status("PEAP TLS ClientHello sent; waiting for ServerHello");
    return 1;
}

static void kwifi_log_tls_events(uint32_t events)
{
    kwifi_tls_state *tls = &g_kwifi_connect.tls;

    if (events & KWIFI_TLS_EVENT_SERVER_HELLO)
    {
        terminal_print("[K:TLS] ServerHello version=");
        terminal_print_inline(kwifi_tls_version_name(tls->server_major, tls->server_minor));
        terminal_print(" cipher=");
        terminal_print_inline(kwifi_tls_cipher_name(tls->cipher_suite));
        terminal_print(" suite=");
        terminal_print_inline_hex64(tls->cipher_suite);
        terminal_print(" session_id_len=");
        terminal_print_inline_hex64(tls->session_id_len);
    }

    if (events & KWIFI_TLS_EVENT_CERTIFICATE)
    {
        terminal_print("[K:TLS] Certificate chain_len=");
        terminal_print_inline_hex64(tls->certificate_chain_len);
        terminal_print(" cert_count=");
        terminal_print_inline_hex64(tls->certificate_count);
        terminal_print(" first_len=");
        terminal_print_inline_hex64(tls->first_certificate_len);
    }

    if (events & KWIFI_TLS_EVENT_SERVER_KEY_EXCHANGE)
    {
        terminal_print("[K:TLS] ServerKeyExchange group=");
        terminal_print_inline_hex64(tls->ecdhe_named_group);
    }

    if (events & KWIFI_TLS_EVENT_CERTIFICATE_REQUEST)
        terminal_print("[K:TLS] CertificateRequest received");

    if (events & KWIFI_TLS_EVENT_SERVER_HELLO_DONE)
        terminal_print("[K:TLS] ServerHelloDone received");

    if (events & KWIFI_TLS_EVENT_ALERT)
    {
        terminal_print("[K:TLS] Alert level=");
        terminal_print_inline_hex64(tls->alert_level);
        terminal_print(" desc=");
        terminal_print_inline_hex64(tls->alert_description);
    }

    if (events & KWIFI_TLS_EVENT_ERROR)
        terminal_print("[K:TLS] parse/handshake error");
}

static void kwifi_set_peap_tls_status(void)
{
    const kwifi_tls_state *tls = &g_kwifi_connect.tls;

    if (tls->alert_seen)
    {
        kwifi_set_peap_phase(8u, tls->alert_level == 2u ? "tls-fatal-alert" : "tls-alert");
        kwifi_set_status(tls->alert_level == 2u ? "PEAP TLS fatal alert from server" :
                                                   "PEAP TLS warning alert from server");
        return;
    }

    if (tls->error)
    {
        kwifi_set_peap_phase(8u, "tls-parse-error");
        kwifi_set_status("PEAP TLS parse error; check server flight");
        return;
    }

    if (tls->server_hello_done_seen)
    {
        if (tls->server_major == 0x03u && tls->server_minor >= 0x04u)
        {
            kwifi_set_peap_phase(8u, "tls13-needed");
            kwifi_set_status("PEAP TLS 1.3 selected; TLS 1.3 engine needed");
        }
        else if (kwifi_tls_cipher_is_ecdhe_key_exchange(tls->cipher_suite))
        {
            kwifi_set_peap_phase(8u, "tls-ecdhe-needed");
            kwifi_set_status("PEAP TLS server done; ECDHE key exchange needed");
        }
        else if (kwifi_tls_cipher_is_rsa_key_exchange(tls->cipher_suite))
        {
            kwifi_set_peap_phase(8u, "tls-rsa-needed");
            kwifi_set_status("PEAP TLS server done; RSA key exchange needed");
        }
        else
        {
            kwifi_set_peap_phase(8u, "tls-cipher-unsupported");
            kwifi_set_status("PEAP TLS server done; unsupported cipher suite");
        }
        return;
    }

    if (tls->certificate_request_seen)
    {
        kwifi_set_peap_phase(7u, "tls-cert-request");
        kwifi_set_status("PEAP TLS client certificate requested; unsupported for password PEAP");
        return;
    }

    if (tls->certificate_seen)
    {
        kwifi_set_peap_phase(7u, "tls-certificate");
        kwifi_set_status("PEAP TLS certificate received; waiting for ServerHelloDone");
        return;
    }

    if (tls->server_key_exchange_seen)
    {
        kwifi_set_peap_phase(7u, "tls-server-key");
        kwifi_set_status("PEAP TLS ServerKeyExchange received; waiting for ServerHelloDone");
        return;
    }

    if (tls->server_hello_seen)
    {
        kwifi_set_peap_phase(7u, "tls-serverhello");
        kwifi_set_status("PEAP TLS ServerHello received; waiting for certificate");
        return;
    }

    kwifi_set_peap_phase(7u, "tls-buffering");
    kwifi_set_status("PEAP TLS server data received; buffering TLS records");
}

static uint32_t kwifi_process_peap_request(const uint8_t *eap, uint32_t eap_len)
{
    uint8_t identifier;
    uint8_t flags_ver;
    uint8_t flags;
    uint8_t version;
    uint32_t pos = 6u;
    uint32_t tls_data_len = 0u;
    uint32_t tls_total_len = 0u;
    uint32_t tls_events = 0u;

    if (!eap || eap_len < 6u)
        return 0u;

    identifier = eap[1];
    flags_ver = eap[5];
    flags = (uint8_t)(flags_ver & 0xFCu);
    version = (uint8_t)(flags_ver & 0x03u);

    g_kwifi_connect.eap_peap_seen = 1u;
    g_kwifi_connect.peap_version = version;
    kwifi_set_eap_phase(5u, "peap-request");

    if (flags & KWIFI_PEAP_FLAG_START)
    {
        g_kwifi_connect.peap_tls_clienthello_sent = 0u;
        g_kwifi_connect.peap_tls_fragment_ack_sent = 0u;
        g_kwifi_connect.peap_tls_server_data_seen = 0u;
        g_kwifi_connect.peap_clienthello_len = 0u;
        g_kwifi_connect.peap_tls_rx_bytes = 0u;
        g_kwifi_connect.peap_tls_rx_total_len = 0u;
        kwifi_tls_init(&g_kwifi_connect.tls);
        kwifi_set_peap_phase(5u, "peap-start");
    }

    if (flags & KWIFI_PEAP_FLAG_LENGTH)
    {
        if (eap_len < pos + 4u)
        {
            kwifi_set_status("PEAP packet too short for TLS length");
            return 0u;
        }
        tls_total_len = kwifi_read_be32(eap + pos);
        pos += 4u;
        g_kwifi_connect.peap_tls_rx_total_len = tls_total_len;
    }

    if (eap_len > pos)
    {
        tls_data_len = eap_len - pos;
        g_kwifi_connect.peap_tls_server_data_seen = 1u;
        if (g_kwifi_connect.peap_tls_rx_bytes <= 0xFFFFFFFFu - tls_data_len)
            g_kwifi_connect.peap_tls_rx_bytes += tls_data_len;
        tls_events = kwifi_tls_feed(&g_kwifi_connect.tls, eap + pos, tls_data_len);
        kwifi_log_tls_events(tls_events);
        if (tls_events & KWIFI_TLS_EVENT_ERROR)
            kwifi_set_peap_tls_status();
    }

    terminal_print("[K:WIFI] PEAP request flags=");
    terminal_print_inline_hex8(flags_ver);
    terminal_print(" tls_fragment=");
    terminal_print_inline_hex64(tls_data_len);
    terminal_print(" tls_total=");
    terminal_print_inline_hex64(g_kwifi_connect.peap_tls_rx_total_len);
    terminal_print(" version=");
    terminal_print_inline_hex64(version);
    terminal_flush_log();

    if (flags & KWIFI_PEAP_FLAG_MORE)
    {
        if (tls_events & KWIFI_TLS_EVENT_ERROR)
            return 1u;
        if (kwifi_send_eap_peap_tls(identifier, 0, 0u, 0u, 0u))
        {
            g_kwifi_connect.peap_tls_fragment_ack_sent = 1u;
            kwifi_set_peap_phase(6u, "tls-fragment-ack");
            kwifi_set_status("PEAP TLS fragment ACK sent; waiting for next fragment");
        }
        return 1u;
    }

    if ((flags & KWIFI_PEAP_FLAG_START) || !g_kwifi_connect.peap_tls_clienthello_sent)
    {
        (void)tls_data_len;
        return kwifi_send_peap_client_hello(identifier) ? 1u : 0u;
    }

    if (tls_data_len)
    {
        kwifi_set_peap_tls_status();
        return 1u;
    }

    kwifi_set_peap_phase(6u, "tls-clienthello-sent");
    kwifi_set_status("PEAP keepalive/ACK received; waiting for TLS server flight");
    return 1u;
}

static uint32_t kwifi_process_eapol_payload(const uint8_t *eapol, uint32_t len)
{
    uint32_t body_len;
    uint8_t packet_type;

    if (!eapol || len < 4u)
        return 0u;

    packet_type = eapol[1];
    body_len = kwifi_read_be16(eapol + 2u);
    if (body_len + 4u > len)
        return 0u;

    if (packet_type == KWIFI_EAPOL_TYPE_KEY)
    {
        g_kwifi_connect.eapol_key_seen = 1u;
        kwifi_set_status("RSN key frame received; PTK/GTK derivation needed");
        terminal_print("[K:WIFI] EAPOL-Key frame received len=");
        terminal_print_inline_hex64(body_len);
        terminal_flush_log();
        return 1u;
    }

    if (packet_type == KWIFI_EAPOL_TYPE_EAP_PACKET && body_len >= 4u)
    {
        const uint8_t *eap = eapol + 4u;
        uint8_t code = eap[0];
        uint8_t identifier = eap[1];
        uint32_t eap_len = kwifi_read_be16(eap + 2u);
        uint8_t eap_type = (eap_len >= 5u && body_len >= 5u) ? eap[4] : 0u;

        if (eap_len > body_len)
            return 0u;

        g_kwifi_connect.eap_last_identifier = identifier;
        terminal_print("[K:WIFI] EAP packet code=");
        terminal_print_inline_hex64(code);
        terminal_print(" id=");
        terminal_print_inline_hex64(identifier);
        terminal_print(" type=");
        terminal_print_inline_hex64(eap_type);
        terminal_print(" len=");
        terminal_print_inline_hex64(eap_len);
        terminal_flush_log();

        if (code == KWIFI_EAP_CODE_REQUEST && eap_type == KWIFI_EAP_TYPE_IDENTITY)
        {
            if (kwifi_send_eap_response_identity(identifier))
            {
                kwifi_set_eap_phase(4u, "identity-sent");
                kwifi_set_peap_phase(4u, "waiting-peap");
                kwifi_set_status("EAP identity response sent; waiting for PEAP TLS");
            }
            return 1u;
        }

        if (code == KWIFI_EAP_CODE_REQUEST && eap_type == KWIFI_EAP_TYPE_PEAP)
            return kwifi_process_peap_request(eap, eap_len);

        if (code == KWIFI_EAP_CODE_SUCCESS)
        {
            kwifi_set_status("EAP success received; waiting for RSN key frames");
            return 1u;
        }

        if (code == KWIFI_EAP_CODE_FAILURE)
        {
            kwifi_set_status("EAP failure received from authenticator");
            return 1u;
        }
    }

    return 0u;
}

static uint32_t kwifi_enterprise_process_rx(void)
{
    static uint8_t frame[2048];
    uint32_t processed = 0u;

    if (!g_kwifi_connect.requested || !g_kwifi_connect.enterprise)
        return 0u;

    for (uint32_t i = 0u; i < 8u; ++i)
    {
        const uint8_t *eapol = 0;
        uint32_t eapol_len = 0u;
        uint32_t frame_len = 0u;
        uint32_t frame_kind = 0u;

        if (!pci_kernel_wifi_rx_frame_pop(frame, sizeof(frame), &frame_len, &frame_kind))
            break;

        if (kwifi_find_eapol_payload(frame, frame_len, &eapol, &eapol_len))
            processed += kwifi_process_eapol_payload(eapol, eapol_len);
    }

    return processed;
}

static int kwifi_send_eapol_start(void)
{
    uint8_t frame[64];
    uint8_t src[6];
    uint8_t dst[6];
    uint8_t have_bssid = 0u;
    uint8_t use_pae_group = 1u;
    int sent_data;
    static const uint8_t eapol_pae_group[6] = {0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x03u};

    if (g_kwifi_connect.eapol_start_attempts >= 4u)
        return 1;

    if (!pci_kernel_wifi_get_local_mac(src))
        return 0;
    have_bssid = pci_kernel_wifi_get_bssid(dst) ? 1u : 0u;
    if (have_bssid)
        use_pae_group = 0u;

    for (uint32_t i = 0u; i < sizeof(frame); ++i)
        frame[i] = 0u;
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        frame[i] = use_pae_group ? eapol_pae_group[i] : dst[i];
        frame[6u + i] = src[i];
    }
    frame[12] = 0x88u;
    frame[13] = 0x8Eu;
    frame[14] = 0x01u; /* EAPOL v1 */
    frame[15] = 0x01u; /* EAPOL-Start */
    frame[16] = 0x00u; /* body len hi */
    frame[17] = 0x00u; /* body len lo */

    sent_data = pci_kernel_wifi_tx_l2_frame(frame, 60u);
    if (!sent_data)
        return 0;

    g_kwifi_connect.eapol_start_sent = 1u;
    g_kwifi_connect.eapol_start_attempts++;
    g_kwifi_connect.eapol_poll_ticks = 0u;
    terminal_print("[K:WIFI] enterprise EAPOL-Start queued attempt=");
    terminal_print_inline_hex64(g_kwifi_connect.eapol_start_attempts);
    terminal_print(" via data path dst=");
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        terminal_print_inline_hex8(frame[i]);
        if (i != 5u)
            terminal_print(":");
    }
    terminal_print(" dst_mode=");
    terminal_print_inline(use_pae_group ? "pae-group" : "bssid");
    terminal_print(" tx_paths=data");
    return 1;
}

static void kwifi_set_status_with_chan_prefix(const char *prefix, uint32_t chan_mhz)
{
    char msg[96];
    uint32_t p = 0u;
    uint32_t n = 0u;

    if (!prefix)
        prefix = "";
    while (prefix[p] && p + 1u < sizeof(msg))
    {
        msg[p] = prefix[p];
        ++p;
    }
    if (p + 1u < sizeof(msg))
        msg[p++] = ' ';
    if (p + 1u < sizeof(msg))
        msg[p++] = '@';
    if (p + 1u < sizeof(msg))
        msg[p++] = ' ';

    if (chan_mhz == 0u)
    {
        if (p + 1u < sizeof(msg))
            msg[p++] = '0';
    }
    else
    {
        char digits[12];
        while (chan_mhz > 0u && n < sizeof(digits))
        {
            digits[n++] = (char)('0' + (chan_mhz % 10u));
            chan_mhz /= 10u;
        }
        while (n > 0u && p + 1u < sizeof(msg))
            msg[p++] = digits[--n];
    }

    if (p + 1u < sizeof(msg))
        msg[p++] = 'M';
    if (p + 1u < sizeof(msg))
        msg[p++] = 'H';
    if (p + 1u < sizeof(msg))
        msg[p++] = 'z';
    msg[p] = 0;
    kwifi_set_status(msg);
}

static int kwifi_enterprise_reconnect_fallback(void)
{
    uint32_t idx;
    uint32_t chan_mhz;
    uint32_t n;

    if (!g_kwifi_connect.requested || !g_kwifi_connect.enterprise || !g_kwifi_connect.ssid[0])
        return 0;

    n = (uint32_t)(sizeof(g_kwifi_enterprise_fallback_channels_mhz) / sizeof(g_kwifi_enterprise_fallback_channels_mhz[0]));
    if (n == 0u)
        return 0;

    idx = (uint32_t)g_kwifi_connect.reconnect_attempts % n;
    chan_mhz = g_kwifi_enterprise_fallback_channels_mhz[idx];
    g_kwifi_connect.reconnect_attempts = (uint8_t)(g_kwifi_connect.reconnect_attempts + 1u);

    (void)pci_kernel_wifi_set_connect_override(0, 0u, chan_mhz);
    if (!pci_kernel_wifi_connect_ssid(g_kwifi_connect.ssid))
    {
        kwifi_set_status_with_chan_prefix("beacon-miss reconnect failed on fallback channel", chan_mhz);
        terminal_print("[K:WIFI] enterprise fallback reconnect failed chan=");
        terminal_print_inline_hex64(chan_mhz);
        return 0;
    }

    g_kwifi_connect.connected = 0u;
    g_kwifi_connect.control_port_authorized = 0u;
    g_kwifi_connect.eapol_start_sent = 0u;
    g_kwifi_connect.eapol_start_attempts = 0u;
    g_kwifi_connect.eapol_poll_ticks = 0u;
    g_kwifi_connect.eap_identity_sent = 0u;
    g_kwifi_connect.eap_peap_seen = 0u;
    g_kwifi_connect.eapol_key_seen = 0u;
    g_kwifi_connect.peap_tls_clienthello_sent = 0u;
    g_kwifi_connect.peap_tls_fragment_ack_sent = 0u;
    g_kwifi_connect.peap_tls_server_data_seen = 0u;
    g_kwifi_connect.peap_clienthello_len = 0u;
    g_kwifi_connect.peap_tls_rx_bytes = 0u;
    g_kwifi_connect.peap_tls_rx_total_len = 0u;
    kwifi_tls_init(&g_kwifi_connect.tls);
    kwifi_set_eap_phase(1u, "assoc-staged");
    kwifi_set_peap_phase(1u, "supplicant-pending");
    kwifi_set_status_with_chan_prefix("beacon-miss detected; reconnecting enterprise link on fallback channel", chan_mhz);
    terminal_print("[K:WIFI] enterprise fallback reconnect chan=");
    terminal_print_inline_hex64(chan_mhz);
    return 1;
}

static int kwifi_fw_have_kind(const boot_info *bi, uint32_t kind)
{
    if (!bi)
        return 0;

    for (uint32_t i = 0; i < bi->wifi_fw_count && i < BOOTINFO_WIFI_FW_MAX; ++i)
    {
        if (bi->wifi_fw[i].kind == kind && bi->wifi_fw[i].base_phys && bi->wifi_fw[i].size_bytes)
            return 1;
    }

    return 0;
}

static int kwifi_fw_load_one_from_fs(boot_info *bi, uint32_t kind, const char *label, const char *const *paths)
{
    if (!bi || !label || !paths || bi->wifi_fw_count >= BOOTINFO_WIFI_FW_MAX || kwifi_fw_have_kind(bi, kind))
        return -1;

    for (uint32_t p = 0; paths[p]; ++p)
    {
        KFile f = (KFile){0};
        uint64_t size;
        uint64_t pages;
        void *buf;
        uint32_t got = 0;

        if (kfile_open(&f, paths[p], KFILE_READ) != 0)
            continue;

        size = kfile_size(&f);
        pages = kwifi_pages_for_bytes(size);
        buf = (size && pages) ? pmem_alloc_pages_lowdma(pages) : 0;
        if (!buf)
        {
            kfile_close(&f);
            terminal_print("[K:WIFI-FW] alloc failed for ");
            terminal_print(label);
            return -2;
        }

        if (kfile_read(&f, buf, (uint32_t)size, &got) != 0 || (uint64_t)got != size)
        {
            kfile_close(&f);
            terminal_print("[K:WIFI-FW] read failed for ");
            terminal_print(label);
            terminal_print(" got=");
            terminal_print_inline_hex64(got);
            terminal_print(" size=");
            terminal_print_inline_hex64(size);
            return -3;
        }

        kfile_close(&f);
        asm_dma_clean_range(buf, size);

        bi->wifi_fw[bi->wifi_fw_count].kind = kind;
        bi->wifi_fw[bi->wifi_fw_count].base_phys = pmem_virt_to_phys(buf);
        bi->wifi_fw[bi->wifi_fw_count].size_bytes = size;
        bi->wifi_fw_count++;

        terminal_print("[K:WIFI-FW] loaded ");
        terminal_print(label);
        terminal_print(" path=");
        terminal_print(paths[p]);
        terminal_print(" base=");
        terminal_print_inline_hex64(pmem_virt_to_phys(buf));
        terminal_print(" size=");
        terminal_print_inline_hex64(size);
        return 0;
    }

    terminal_print("[K:WIFI-FW] missing ");
    terminal_print(label);
    return -4;
}

static void kwifi_fw_load_from_fs_if_needed(boot_info *bi, int mounted)
{
    static const char *amss_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/ncm865/amss.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/ncm865/amss.bin",
        "0:/ath12k/WCN7850/hw2.0/ncm865/amss.bin",
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/amss.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/amss.bin",
        "0:/ath12k/WCN7850/hw2.0/amss.bin",
        0};
    static const char *m3_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/ncm865/m3.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/ncm865/m3.bin",
        "0:/ath12k/WCN7850/hw2.0/ncm865/m3.bin",
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/m3.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/m3.bin",
        "0:/ath12k/WCN7850/hw2.0/m3.bin",
        0};
    static const char *board_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/board-2.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/board-2.bin",
        "0:/ath12k/WCN7850/hw2.0/board-2.bin",
        0};

    if (!bi)
        return;

    if (bi->wifi_fw_count >= BOOTINFO_WIFI_FW_MAX)
        return;

    if (!mounted)
    {
        terminal_print("[K:WIFI-FW] kernel FAT not mounted; firmware fallback skipped");
        return;
    }

    (void)kwifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_AMSS, "amss.bin", amss_paths);
    (void)kwifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_M3, "m3.bin", m3_paths);
    (void)kwifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_BOARD, "board-2.bin", board_paths);

    terminal_print("[K:WIFI-FW] count after kernel fallback: ");
    terminal_print_inline_hex64(bi->wifi_fw_count);
}

static void kwifi_print_stage2_firmware(const boot_info *bi)
{
    if (!bi)
        return;

    terminal_print("Stage2 WiFi FW count: ");
    terminal_print_inline_hex64(bi->wifi_fw_count);
    for (uint32_t i = 0; i < bi->wifi_fw_count && i < BOOTINFO_WIFI_FW_MAX; ++i)
    {
        terminal_print("WiFi FW kind=");
        terminal_print_inline_hex64(bi->wifi_fw[i].kind);
        terminal_print(" base=");
        terminal_print_inline_hex64(bi->wifi_fw[i].base_phys);
        terminal_print(" size=");
        terminal_print_inline_hex64(bi->wifi_fw[i].size_bytes);
    }
}

static void kwifi_print_stage2_nic_hints(const boot_info *bi)
{
    if (!bi)
        return;

    terminal_print("Stage2 NIC hint count: ");
    terminal_print_inline_hex64(bi->pci_nic_count);
    for (uint32_t i = 0; i < bi->pci_nic_count && i < BOOTINFO_PCI_NIC_MAX; ++i)
    {
        terminal_print("NIC seg=");
        terminal_print_inline_hex64(bi->pci_nics[i].segment);
        terminal_print(" bdf=");
        terminal_print_inline_hex64(((uint64_t)bi->pci_nics[i].bus << 16) |
                                    ((uint64_t)bi->pci_nics[i].dev << 8) |
                                    (uint64_t)bi->pci_nics[i].fn);
        terminal_print(" vid=");
        terminal_print_inline_hex64(bi->pci_nics[i].vendor_id);
        terminal_print(" did=");
        terminal_print_inline_hex64(bi->pci_nics[i].device_id);
        terminal_print(" class=");
        terminal_print_inline_hex64(bi->pci_nics[i].class_code);
        terminal_print(" sub=");
        terminal_print_inline_hex64(bi->pci_nics[i].subclass);
        terminal_print(" if=");
        terminal_print_inline_hex64(bi->pci_nics[i].prog_if);
        terminal_print(" bar0=");
        terminal_print_inline_hex64(bi->pci_nics[i].bar0_mmio_base);
    }
}

static void kwifi_print_acpi_net_hints(uint32_t net_hints)
{
    if ((net_hints & (DIHOS_NET_HINT_WLAN | DIHOS_NET_HINT_WIFI | DIHOS_NET_HINT_WCN)) != 0u)
        terminal_print("[NET] ACPI suggests WLAN/WCN platform path");
    else
        terminal_print("[NET] ACPI has no explicit WLAN/WIFI/WCN device marker");
    if ((net_hints & DIHOS_NET_HINT_SDIO) != 0u)
        terminal_print("[NET] ACPI suggests SDIO/SDHost network path");
    if ((net_hints & (DIHOS_NET_HINT_WWAN | DIHOS_NET_HINT_MHI)) != 0u)
        terminal_print("[NET] ACPI suggests WWAN/MHI path (non-PCI NIC likely)");
    else if ((net_hints & DIHOS_NET_HINT_USB) != 0u)
        terminal_print("[NET] ACPI suggests USB network path");
}

static void kwifi_print_acpi_resource_windows(void)
{
    uint32_t nres = acpi_probe_net_resource_count();
    const acpi_net_resource_window *res = acpi_probe_net_resources();

    terminal_print("[NET] ACPI resource window count:");
    terminal_print_inline_hex32(nres);
    for (uint32_t i = 0; i < nres; ++i)
    {
        terminal_print("[NET] res dev=");
        terminal_print(res[i].dev_name);
        terminal_print(" hid=");
        terminal_print(res[i].hid_name);
        terminal_print(" kind=");
        terminal_print_inline_hex32(res[i].kind);
        terminal_print(" rtype=");
        terminal_print_inline_hex32(res[i].rtype);
        terminal_print(" min=");
        terminal_print_inline_hex64(res[i].min_addr);
        terminal_print(" max=");
        terminal_print_inline_hex64(res[i].max_addr);
        terminal_print(" len=");
        terminal_print_inline_hex64(res[i].span_len);
    }
}

uint32_t kwifi_network_count(void)
{
    return pci_kernel_wifi_network_count();
}

const char *kwifi_network_name(uint32_t index)
{
    return pci_kernel_wifi_network_name(index);
}

uint32_t kwifi_network_hidden(uint32_t index)
{
    return pci_kernel_wifi_network_hidden(index);
}

int kwifi_network_refresh(void)
{
    return pci_kernel_wifi_trigger_scan();
}

int kwifi_network_poll(uint32_t rounds)
{
    return pci_kernel_wifi_poll_events(rounds);
}

uint32_t kwifi_network_scan_running(void)
{
    return pci_kernel_wifi_scan_running();
}

uint32_t kwifi_rx_queue_count(void)
{
    return pci_kernel_wifi_rx_queue_count();
}

uint32_t kwifi_rx_queue_dropped(void)
{
    return pci_kernel_wifi_rx_queue_dropped();
}

int kwifi_rx_frame_pop(uint8_t *out_frame, uint32_t out_cap, uint32_t *out_len, uint32_t *out_kind)
{
    return pci_kernel_wifi_rx_frame_pop(out_frame, out_cap, out_len, out_kind);
}

int kwifi_connect_request(const char *ssid,
                          const char *username,
                          const char *password,
                          const char *bssid_text,
                          const char *channel_text)
{
    uint32_t ssid_len;
    uint32_t user_len;
    uint32_t pass_len;
    uint32_t found;
    uint8_t bssid[6];
    uint32_t bssid_valid = 0u;
    uint32_t chan_mhz = 0u;
    uint32_t chan_value = 0u;

    if (!ssid || !ssid[0])
    {
        terminal_print("[K:WIFI] connect rejected: missing ssid");
        return 0;
    }

    ssid_len = kwifi_cstr_len_cap(ssid, KWIFI_CONNECT_SSID_MAX);
    if (ssid_len == 0u || ssid_len >= KWIFI_CONNECT_SSID_MAX)
    {
        terminal_print("[K:WIFI] connect rejected: ssid too long");
        return 0;
    }

    user_len = kwifi_cstr_len_cap(username, KWIFI_CONNECT_USER_MAX);
    pass_len = kwifi_cstr_len_cap(password, KWIFI_CONNECT_PASS_MAX);
    if (user_len >= KWIFI_CONNECT_USER_MAX)
    {
        terminal_print("[K:WIFI] connect rejected: username too long");
        return 0;
    }
    if (pass_len >= KWIFI_CONNECT_PASS_MAX)
    {
        terminal_print("[K:WIFI] connect rejected: password too long");
        return 0;
    }
    if (pass_len == 0u)
    {
        terminal_print("[K:WIFI] connect rejected: missing password");
        return 0;
    }

    if (bssid_text && bssid_text[0])
    {
        if (!kwifi_parse_bssid(bssid_text, bssid))
        {
            terminal_print("[K:WIFI] connect rejected: invalid bssid format");
            return 0;
        }
        bssid_valid = 1u;
    }

    if (channel_text && channel_text[0])
    {
        if (!kwifi_parse_u32(channel_text, &chan_value))
        {
            terminal_print("[K:WIFI] connect rejected: invalid channel value");
            return 0;
        }
        if (chan_value >= 2000u)
            chan_mhz = chan_value;
        else if (chan_value == 14u)
            chan_mhz = 2484u;
        else if (chan_value >= 1u && chan_value <= 13u)
            chan_mhz = 2407u + chan_value * 5u;
        else if (chan_value >= 32u && chan_value <= 177u)
            chan_mhz = 5000u + chan_value * 5u;
        else
        {
            terminal_print("[K:WIFI] connect rejected: channel unsupported");
            return 0;
        }
    }

    found = kwifi_have_visible_network(ssid);
    if (!found && !kwifi_network_scan_running())
    {
        (void)kwifi_network_refresh();
        (void)kwifi_network_poll(256u);
        found = kwifi_have_visible_network(ssid);
    }

    g_kwifi_connect.requested = 1u;
    g_kwifi_connect.connected = 0u;
    g_kwifi_connect.enterprise = (username && username[0]) ? 1u : 0u;
    g_kwifi_connect.control_port_authorized = g_kwifi_connect.enterprise ? 0u : 1u;
    g_kwifi_connect.supplicant_ready = 0u;
    g_kwifi_connect.eapol_start_sent = 0u;
    g_kwifi_connect.eapol_start_attempts = 0u;
    g_kwifi_connect.eapol_poll_ticks = 0u;
    g_kwifi_connect.eap_identity_sent = 0u;
    g_kwifi_connect.eap_peap_seen = 0u;
    g_kwifi_connect.eapol_key_seen = 0u;
    g_kwifi_connect.eap_last_identifier = 0u;
    g_kwifi_connect.peap_version = 0u;
    g_kwifi_connect.peap_tls_clienthello_sent = 0u;
    g_kwifi_connect.peap_tls_fragment_ack_sent = 0u;
    g_kwifi_connect.peap_tls_server_data_seen = 0u;
    g_kwifi_connect.peap_clienthello_len = 0u;
    g_kwifi_connect.peap_tls_rx_bytes = 0u;
    g_kwifi_connect.peap_tls_rx_total_len = 0u;
    for (uint32_t i = 0u; i < sizeof(g_kwifi_connect.peap_clienthello); ++i)
        g_kwifi_connect.peap_clienthello[i] = 0u;
    kwifi_tls_init(&g_kwifi_connect.tls);
    g_kwifi_connect.mschap_nt_hash_ready = 0u;
    for (uint32_t i = 0u; i < sizeof(g_kwifi_connect.mschap_nt_hash); ++i)
        g_kwifi_connect.mschap_nt_hash[i] = 0u;
    g_kwifi_connect.beacon_miss_count = 0u;
    g_kwifi_connect.reconnect_attempts = 0u;
    g_kwifi_connect.assoc_kick_attempts = 0u;
    kwifi_copy_trunc(g_kwifi_connect.ssid, sizeof(g_kwifi_connect.ssid), ssid);
    kwifi_copy_trunc(g_kwifi_connect.username, sizeof(g_kwifi_connect.username), username ? username : "");
    kwifi_copy_trunc(g_kwifi_connect.password, sizeof(g_kwifi_connect.password), password);
    if (g_kwifi_connect.enterprise)
    {
        kwifi_mschapv2_nt_password_hash(g_kwifi_connect.password, g_kwifi_connect.mschap_nt_hash);
        g_kwifi_connect.mschap_nt_hash_ready = 1u;
        terminal_print("[K:WIFI] MSCHAPv2 NT password hash prepared");
    }
    kwifi_copy_trunc(g_kwifi_connect.auth_mode, sizeof(g_kwifi_connect.auth_mode),
                     g_kwifi_connect.enterprise ? "wpa2-enterprise" : "non-enterprise");
    kwifi_set_eap_phase(0u, "none");
    kwifi_set_peap_phase(0u, "none");

    if (!pci_kernel_wifi_set_connect_override(bssid, bssid_valid, chan_mhz))
    {
        terminal_print("[K:WIFI] connect rejected: override setup failed");
        return 0;
    }

    if (!found)
    {
        kwifi_set_status("requested; target SSID not in latest scan yet");
        terminal_print("[K:WIFI] connect request saved; SSID not visible yet");
        return 1;
    }

    if (username && username[0])
    {
        if (!pci_kernel_wifi_connect_ssid(ssid))
        {
            kwifi_set_status("enterprise connect failed before association");
            terminal_print("[K:WIFI] enterprise connect attempt failed before association");
            return 0;
        }

        /*
         * Linux-style model:
         * - association is done by driver/firmware
         * - controlled port remains unauthorized
         * - a real host supplicant must run PEAP/MSCHAPv2, derive keys, and
         *   authorize the port
         */
        kwifi_set_eap_phase(1u, "assoc-staged");
        kwifi_set_peap_phase(1u, "assoc-pending");
        kwifi_set_status("association/auth started; waiting for AP association response");
        terminal_print("[K:WIFI] enterprise connect staged; controlled-port auth now required");
        return 1;
    }

    if (pci_kernel_wifi_connect_ssid(ssid))
    {
        kwifi_set_status("firmware association attempt sent; awaiting auth/link confirmation");
        terminal_print("[K:WIFI] connect attempt sent to firmware");
        return 1;
    }

    kwifi_set_status("connect attempt failed before association");
    terminal_print("[K:WIFI] connect attempt failed");
    return 0;
}

uint32_t kwifi_current_connected(void)
{
    return g_kwifi_connect.connected ? 1u : 0u;
}

const char *kwifi_current_ssid(void)
{
    return g_kwifi_connect.requested ? g_kwifi_connect.ssid : "";
}

const char *kwifi_current_username(void)
{
    return g_kwifi_connect.requested ? g_kwifi_connect.username : "";
}

const char *kwifi_current_status(void)
{
    return g_kwifi_connect.requested ? g_kwifi_connect.status : "idle";
}

const char *kwifi_current_auth_mode(void)
{
    return g_kwifi_connect.requested ? g_kwifi_connect.auth_mode : "none";
}

const char *kwifi_current_eap_phase(void)
{
    return g_kwifi_connect.requested ? g_kwifi_connect.eap_phase_text : "none";
}

const char *kwifi_current_peap_phase(void)
{
    return g_kwifi_connect.requested ? g_kwifi_connect.peap_phase_text : "none";
}

int kwifi_set_supplicant_ready(uint32_t ready)
{
    uint32_t key_done;
    uint32_t assoc_done;
    uint32_t roam_reason;

    if (!g_kwifi_connect.requested || !g_kwifi_connect.enterprise)
        return 0;

    g_kwifi_connect.supplicant_ready = ready ? 1u : 0u;
    if (!ready)
    {
        (void)pci_kernel_wifi_set_peer_authorize(0u);
        g_kwifi_connect.control_port_authorized = 0u;
        g_kwifi_connect.connected = 0u;
        g_kwifi_connect.eapol_start_sent = 0u;
        g_kwifi_connect.eapol_start_attempts = 0u;
        g_kwifi_connect.eapol_poll_ticks = 0u;
        kwifi_set_eap_phase(2u, "controlled-port-unauthorized");
        kwifi_set_peap_phase(2u, "peap-supplicant-required");
        kwifi_set_status("supplicant marked not-ready; controlled port unauthorized");
        return 1;
    }

    assoc_done = pci_kernel_wifi_peer_assoc_done();
    if (!assoc_done)
        assoc_done = kwifi_try_assoc_kick();
    if (!assoc_done)
    {
        g_kwifi_connect.control_port_authorized = 0u;
        g_kwifi_connect.connected = 0u;
        kwifi_set_eap_phase(2u, "controlled-port-unauthorized");
        kwifi_set_peap_phase(2u, "peap-supplicant-required");
        kwifi_set_status("supplicant ready; AP association pending before controlled-port authorization");
        return 1;
    }

    g_kwifi_connect.control_port_authorized = 0u;
    key_done = pci_kernel_wifi_install_key_done();
    roam_reason = pci_kernel_wifi_roam_reason();
    (void)kwifi_send_eapol_start();
    kwifi_set_eap_phase(3u, "eapol-start-only");
    kwifi_set_peap_phase(3u, "peap-start-pending");

    if (key_done && pci_kernel_wifi_set_peer_authorize(1u))
    {
        g_kwifi_connect.control_port_authorized = 1u;
        g_kwifi_connect.connected = 1u;
        kwifi_set_status("firmware key install complete; link authorized");
        return 1;
    }

    g_kwifi_connect.connected = 0u;
    if (assoc_done && roam_reason != 2u)
        kwifi_set_status("EAPOL-Start sent; waiting for PEAP Identity/Start");
    else
        kwifi_set_status("EAPOL-Start queued; waiting for stable assoc before PEAP");
    return 1;
}

int kwifi_poll_connection(uint32_t rounds)
{
    uint32_t assoc_done;
    uint32_t key_done;
    uint32_t roam_reason;

    if (!g_kwifi_connect.requested || !g_kwifi_connect.enterprise)
        return 0;

    if (rounds == 0u)
        rounds = 32u;
    (void)pci_kernel_wifi_poll_events(rounds);
    (void)kwifi_enterprise_process_rx();
    assoc_done = pci_kernel_wifi_peer_assoc_done();
    key_done = pci_kernel_wifi_install_key_done();
    roam_reason = pci_kernel_wifi_roam_reason();
    if (roam_reason == 2u)
    {
        if (g_kwifi_connect.beacon_miss_count < 0xFFu)
            g_kwifi_connect.beacon_miss_count++;
    }
    else
    {
        g_kwifi_connect.beacon_miss_count = 0u;
    }

    if (g_kwifi_connect.beacon_miss_count >= 3u)
    {
        g_kwifi_connect.beacon_miss_count = 0u;
        (void)kwifi_enterprise_reconnect_fallback();
        return 1;
    }

    /*
     * Proper Linux-style ownership split:
     * - driver/firmware performs association
     * - 802.1X controlled port stays unauthorized
     * - a real supplicant must handle PEAP/MSCHAPv2, key derivation, and
     *   key install before the port can be authorized.
     */
    if (g_kwifi_connect.eap_phase == 1u)
    {
        g_kwifi_connect.control_port_authorized = 0u;
        kwifi_set_eap_phase(2u, "controlled-port-unauthorized");
        kwifi_set_peap_phase(2u, "peap-start-pending");
        kwifi_set_status(assoc_done
                             ? "AP association confirmed; PEAP engine ready"
                             : "waiting for AP association response");
        return 1;
    }

    if (g_kwifi_connect.eap_phase == 2u)
    {
        if (!g_kwifi_connect.supplicant_ready)
        {
            if (!assoc_done)
            {
                kwifi_set_status("association pending; waiting for AP association response");
                return 0;
            }
            if (roam_reason == 2u)
            {
                kwifi_set_status("AP association confirmed but firmware reports beacon-miss; link not stable yet");
                return 0;
            }

            g_kwifi_connect.supplicant_ready = 1u;
            kwifi_set_status("host EAPOL engine starting; sending EAPOL-Start");
        }

        if (!assoc_done)
        {
            assoc_done = kwifi_try_assoc_kick();
            if (!assoc_done)
            {
                kwifi_set_status("supplicant ready; AP association pending before controlled-port authorization");
                return 0;
            }
        }

        g_kwifi_connect.control_port_authorized = 0u;
        (void)kwifi_send_eapol_start();
        kwifi_set_eap_phase(3u, "eapol-start-only");
        kwifi_set_peap_phase(3u, "peap-start-pending");
        if (key_done && pci_kernel_wifi_set_peer_authorize(1u))
        {
            g_kwifi_connect.control_port_authorized = 1u;
            g_kwifi_connect.connected = 1u;
            kwifi_set_status("enterprise key install complete; link authorized");
        }
        else if (assoc_done && roam_reason != 2u)
        {
            g_kwifi_connect.connected = 0u;
            kwifi_set_status("EAPOL-Start sent; waiting for PEAP Identity/Start");
        }
        else if (roam_reason == 2u)
        {
            g_kwifi_connect.connected = 0u;
            kwifi_set_status("EAPOL-Start queued, but firmware reports beacon-miss");
        }
        else
        {
            g_kwifi_connect.connected = 0u;
            kwifi_set_status("EAPOL-Start queued; waiting for PEAP Identity/Start");
        }
        return 1;
    }

    if (g_kwifi_connect.eap_phase >= 3u)
    {
        if (!g_kwifi_connect.eap_identity_sent &&
            !g_kwifi_connect.eap_peap_seen &&
            !g_kwifi_connect.eapol_key_seen &&
            g_kwifi_connect.eapol_start_attempts < 4u)
        {
            if (g_kwifi_connect.eapol_poll_ticks < 0xFFu)
                g_kwifi_connect.eapol_poll_ticks++;
            if (g_kwifi_connect.eapol_poll_ticks >= 2u)
                (void)kwifi_send_eapol_start();
        }

        if (key_done)
        {
            if (g_kwifi_connect.control_port_authorized || pci_kernel_wifi_set_peer_authorize(1u))
            {
                g_kwifi_connect.control_port_authorized = 1u;
                g_kwifi_connect.connected = 1u;
                kwifi_set_status("enterprise key install complete; link authorized");
            }
            else
            {
                g_kwifi_connect.control_port_authorized = 0u;
                g_kwifi_connect.connected = 0u;
                kwifi_set_status("enterprise key installed; waiting for WMI credit to authorize link");
            }
            return 1;
        }

        g_kwifi_connect.connected = 0u;
        if (g_kwifi_connect.eap_peap_seen)
        {
            if (g_kwifi_connect.peap_tls_server_data_seen)
                kwifi_set_peap_tls_status();
            else if (g_kwifi_connect.peap_tls_fragment_ack_sent)
                kwifi_set_status("PEAP TLS fragment ACK sent; waiting for next fragment");
            else if (g_kwifi_connect.peap_tls_clienthello_sent)
                kwifi_set_status("PEAP TLS ClientHello sent; waiting for ServerHello");
            else
                kwifi_set_status("PEAP request received; preparing TLS ClientHello");
        }
        else if (g_kwifi_connect.eapol_key_seen)
            kwifi_set_status("RSN key frame received; PTK/GTK derivation needed");
        else if (g_kwifi_connect.eap_identity_sent)
            kwifi_set_status("EAP identity response sent; waiting for PEAP TLS");
        else if (g_kwifi_connect.eapol_start_attempts < 4u)
            kwifi_set_status("EAPOL-Start retrying; waiting for Identity request");
        else
            kwifi_set_status("EAPOL-Start retries exhausted; no Identity request seen");
        return 0;
    }

    return 0;
}

void kwifi_init(boot_info *bi, int storage_mounted)
{
    uint32_t net_hints = 0;

    if (!bi)
        return;

    terminal_print("[K:WIFI] init begin");
    kwifi_print_stage2_firmware(bi);
    kwifi_fw_load_from_fs_if_needed(bi, storage_mounted);
    kwifi_print_stage2_nic_hints(bi);

    if (bi->pci_nic_count)
    {
        terminal_print("[K:WIFI] using stage2 NIC hints; kernel MCFG fallback skipped");
        terminal_print("[K:WIFI] init end");
        return;
    }

    terminal_print("Stage2 NIC hints empty; trying kernel MCFG NIC probe");
    net_hints = acpi_probe_net_candidates_from_rsdp(bi->acpi_rsdp);
    pci_kernel_set_net_hints(net_hints);
    kwifi_print_acpi_net_hints(net_hints);
    kwifi_print_acpi_resource_windows();
    pci_kernel_probe_nics_from_mcfg(bi);
    terminal_print("[K:WIFI] init end");
}
