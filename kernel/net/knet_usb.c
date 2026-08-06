#include "net/knet_usb.h"
#include "usb/usb_ethernet.h"
#include "usb/usbh.h"
#include "terminal/terminal_api.h"
#include "kwrappers/string.h"
#include "system/dihos_time.h"
#include "bearssl.h"
#include "asm/asm.h"

#define KNET_ETH_TYPE_IPV4 0x0800u
#define KNET_ETH_TYPE_ARP 0x0806u
#define KNET_IP_PROTO_UDP 17u
#define KNET_IP_PROTO_TCP 6u
#define KNET_DHCP_CLIENT_PORT 68u
#define KNET_DHCP_SERVER_PORT 67u
#define KNET_DHCP_MAGIC 0x63825363u
#define KNET_DHCP_XID 0x4449484Fu /* DIHO */

static knet_usb_status g_knet;
typedef struct knet_dns_cache_entry
{
    char host[128];
    uint32_t ip;
} knet_dns_cache_entry;
static knet_dns_cache_entry g_dns_cache[8];
static uint32_t g_dns_cache_next;
static uint16_t g_dns_id = 0xD105u;
static uint16_t g_dns_port = 49152u;
static uint8_t g_tx[1536];
static uint8_t g_rx[32768];
static uint16_t g_ip_id = 1u;
static uint16_t g_tcp_port = 49152u;
static uint32_t g_tcp_sequence = 0x44494831u;

typedef struct knet_x509_noanchor_context
{
    const br_x509_class *vtable;
    const br_x509_class **inner;
} knet_x509_noanchor_context;

static br_ssl_client_context g_tls_client;
static br_x509_minimal_context g_tls_x509;
static knet_x509_noanchor_context g_tls_noanchor;
static uint8_t g_tls_iobuf[BR_SSL_BUFSIZE_BIDI];
static uint8_t g_tls_pending[2048];
static uint8_t *g_fetch_output;
static uint32_t g_fetch_capacity;
static uint32_t g_fetch_size;
static uint8_t g_fetch_truncated;
static volatile uint32_t *g_fetch_cancelled;
static volatile uint32_t g_fetch_busy;
static uint64_t g_fetch_deadline;

static uint64_t knet_now_ms(void);
static uint32_t g_dhcp_xid = KNET_DHCP_XID;

static uint8_t knet_fetch_cancelled(void)
{
    if (g_fetch_cancelled)
        asm_dma_invalidate_range((const void *)g_fetch_cancelled, sizeof(*g_fetch_cancelled));
    return (g_fetch_cancelled && __atomic_load_n(g_fetch_cancelled, __ATOMIC_ACQUIRE)) ||
           (g_fetch_deadline && knet_now_ms() >= g_fetch_deadline);
}

static void knet_response_emit(const uint8_t *data, uint32_t len)
{
    if (!data || !len)
        return;
    if (g_fetch_output)
    {
        uint32_t room = g_fetch_size < g_fetch_capacity ? g_fetch_capacity - g_fetch_size : 0u;
        uint32_t take = len < room ? len : room;
        if (take)
            memcpy(g_fetch_output + g_fetch_size, data, take);
        g_fetch_size += take;
        if (take != len)
            g_fetch_truncated = 1u;
        return;
    }
    while (len)
    {
        char chunk[257];
        uint32_t take = len > 256u ? 256u : len;
        memcpy(chunk, data, take);
        chunk[take] = 0;
        terminal_print_inline(chunk);
        data += take;
        len -= take;
    }
}

static void knet_log_print(const char *text) { terminal_print(text); }
static void knet_log_inline(const char *text) { terminal_print_inline(text); }
static void knet_log_success(const char *text) { terminal_success(text); }
static void knet_log_warn(const char *text) { terminal_warn(text); }
static void knet_log_error(const char *text) { terminal_error(text); }
static void knet_log_hex32(uint32_t value) { terminal_print_inline_hex32(value); }

#define terminal_print knet_log_print
#define terminal_print_inline knet_log_inline
#define terminal_success knet_log_success
#define terminal_warn knet_log_warn
#define terminal_error knet_log_error
#define terminal_print_inline_hex32 knet_log_hex32

static void knet_x509_start_chain(const br_x509_class **ctx, const char *server_name)
{
    knet_x509_noanchor_context *x = (knet_x509_noanchor_context *)ctx;
    (*x->inner)->start_chain(x->inner, server_name);
}

static void knet_x509_start_cert(const br_x509_class **ctx, uint32_t length)
{
    knet_x509_noanchor_context *x = (knet_x509_noanchor_context *)ctx;
    (*x->inner)->start_cert(x->inner, length);
}

static void knet_x509_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
    knet_x509_noanchor_context *x = (knet_x509_noanchor_context *)ctx;
    (*x->inner)->append(x->inner, buf, len);
}

static void knet_x509_end_cert(const br_x509_class **ctx)
{
    knet_x509_noanchor_context *x = (knet_x509_noanchor_context *)ctx;
    (*x->inner)->end_cert(x->inner);
}

static unsigned knet_x509_end_chain(const br_x509_class **ctx)
{
    knet_x509_noanchor_context *x = (knet_x509_noanchor_context *)ctx;
    unsigned result = (*x->inner)->end_chain(x->inner);
    return result == BR_ERR_X509_NOT_TRUSTED ? 0u : result;
}

static const br_x509_pkey *knet_x509_get_pkey(const br_x509_class *const *ctx,
                                               unsigned *usages)
{
    knet_x509_noanchor_context *x = (knet_x509_noanchor_context *)ctx;
    return (*x->inner)->get_pkey(x->inner, usages);
}

static const br_x509_class g_knet_x509_noanchor_vtable = {
    sizeof(knet_x509_noanchor_context),
    knet_x509_start_chain,
    knet_x509_start_cert,
    knet_x509_append,
    knet_x509_end_cert,
    knet_x509_end_chain,
    knet_x509_get_pkey};

static uint64_t knet_cycle_counter(void)
{
#if defined(DIHOS_ARCH_AARCH64)
    uint64_t value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#elif defined(DIHOS_ARCH_X64)
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return dihos_time_ticks();
#endif
}

static uint64_t knet_now_ms(void)
{
#if defined(DIHOS_ARCH_AARCH64)
    uint64_t counter;
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(counter));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (frequency)
        return (counter / frequency) * 1000u +
               ((counter % frequency) * 1000u) / frequency;
#endif
    return (dihos_time_ticks() * 1000u) / DIHOS_TIME_TICKS_PER_SECOND;
}

static void knet_tls_entropy(uint8_t out[48])
{
    uint64_t x = knet_cycle_counter() ^ dihos_time_ticks() ^ ((uint64_t)g_knet.ip << 17) ^
                 (uint64_t)(uintptr_t)&g_tls_client ^ 0x9E3779B97F4A7C15ull;
    for (uint32_t i = 0u; i < 6u; ++i)
        x ^= (uint64_t)g_knet.mac[i] << ((i * 9u) & 63u);
    for (uint32_t i = 0u; i < 48u; ++i)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        x += knet_cycle_counter() ^ dihos_time_ticks() ^ ((uint64_t)i << 32);
        out[i] = (uint8_t)(x >> ((i & 7u) * 8u));
    }
}

static void knet_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;
    if (!dst || !cap)
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

static int knet_starts_with(const char *text, const char *prefix)
{
    if (!text || !prefix)
        return 0;
    while (*prefix)
    {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static int knet_text_equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint16_t ip_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0u;
    while (len > 1u)
    {
        sum += get_be16(data);
        data += 2u;
        len -= 2u;
    }
    if (len)
        sum += ((uint16_t)data[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t udp_ipv4_checksum(uint32_t src_ip,
                                  uint32_t dst_ip,
                                  const uint8_t *udp,
                                  uint16_t udp_len)
{
    uint32_t sum = 0u;
    uint32_t len = udp_len;

    sum += (src_ip >> 16) & 0xFFFFu;
    sum += src_ip & 0xFFFFu;
    sum += (dst_ip >> 16) & 0xFFFFu;
    sum += dst_ip & 0xFFFFu;
    sum += KNET_IP_PROTO_UDP;
    sum += udp_len;

    while (len > 1u)
    {
        sum += get_be16(udp);
        udp += 2u;
        len -= 2u;
    }
    if (len)
        sum += ((uint16_t)udp[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static void print_dec_inline(uint32_t value)
{
    char tmp[10];
    char buf[11];
    uint32_t len = 0u;
    uint32_t out = 0u;

    if (value == 0u)
    {
        terminal_print_inline("0");
        return;
    }

    while (value && len < sizeof(tmp))
    {
        tmp[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (len && out + 1u < sizeof(buf))
        buf[out++] = tmp[--len];
    buf[out] = 0;
    terminal_print_inline(buf);
}

static void print_ip_inline(uint32_t ip)
{
    print_dec_inline((ip >> 24) & 0xFFu);
    terminal_print_inline(".");
    print_dec_inline((ip >> 16) & 0xFFu);
    terminal_print_inline(".");
    print_dec_inline((ip >> 8) & 0xFFu);
    terminal_print_inline(".");
    print_dec_inline(ip & 0xFFu);
}

static int knet_refresh_link(void)
{
    g_knet.link_online = usb_ethernet_online() ? 1u : 0u;
    if (!g_knet.link_online)
    {
        knet_copy(g_knet.detail, sizeof(g_knet.detail), "USB Ethernet offline");
        return -1;
    }
    if (usb_ethernet_get_mac(g_knet.mac) != 0)
    {
        knet_copy(g_knet.detail, sizeof(g_knet.detail), "USB Ethernet MAC unavailable");
        return -1;
    }
    return 0;
}

static uint32_t build_udp_ipv4(uint8_t *out,
                               const uint8_t dst_mac[6],
                               uint32_t src_ip,
                               uint32_t dst_ip,
                               uint16_t src_port,
                               uint16_t dst_port,
                               const uint8_t *payload,
                               uint16_t payload_len)
{
    uint16_t ip_len = (uint16_t)(20u + 8u + payload_len);
    uint16_t frame_len = (uint16_t)(14u + ip_len);
    for (uint32_t i = 0u; i < 6u; ++i)
        out[i] = dst_mac[i];
    for (uint32_t i = 0u; i < 6u; ++i)
        out[6u + i] = g_knet.mac[i];
    put_be16(out + 12u, KNET_ETH_TYPE_IPV4);

    out[14u] = 0x45u;
    out[15u] = 0u;
    put_be16(out + 16u, ip_len);
    put_be16(out + 18u, 0u);
    put_be16(out + 20u, 0u);
    out[22u] = 64u;
    out[23u] = KNET_IP_PROTO_UDP;
    put_be16(out + 24u, 0u);
    put_be32(out + 26u, src_ip);
    put_be32(out + 30u, dst_ip);
    put_be16(out + 24u, ip_checksum(out + 14u, 20u));

    put_be16(out + 34u, src_port);
    put_be16(out + 36u, dst_port);
    put_be16(out + 38u, (uint16_t)(8u + payload_len));
    put_be16(out + 40u, 0u);
    memcpy(out + 42u, payload, payload_len);
    {
        uint16_t udp_sum = udp_ipv4_checksum(src_ip, dst_ip, out + 34u, (uint16_t)(8u + payload_len));
        put_be16(out + 40u, udp_sum ? udp_sum : 0xFFFFu);
    }

    if (frame_len < 60u)
    {
        for (uint32_t i = frame_len; i < 60u; ++i)
            out[i] = 0u;
        frame_len = 60u;
    }
    return frame_len;
}

static uint32_t build_dhcp(uint8_t *out, uint8_t msg_type, uint32_t requested_ip, uint32_t server_id)
{
    uint32_t p = 0u;
    for (uint32_t i = 0u; i < 548u; ++i)
        out[i] = 0u;

    out[0] = 1u; /* BOOTREQUEST */
    out[1] = 1u; /* Ethernet */
    out[2] = 6u;
    out[3] = 0u;
    put_be32(out + 4u, g_dhcp_xid);
    put_be16(out + 8u, 0u);
    put_be16(out + 10u, 0x8000u);
    for (uint32_t i = 0u; i < 6u; ++i)
        out[28u + i] = g_knet.mac[i];
    put_be32(out + 236u, KNET_DHCP_MAGIC);

    p = 240u;
    out[p++] = 53u;
    out[p++] = 1u;
    out[p++] = msg_type;
    out[p++] = 61u;
    out[p++] = 7u;
    out[p++] = 1u; /* Ethernet client identifier. */
    for (uint32_t i = 0u; i < 6u; ++i)
        out[p++] = g_knet.mac[i];
    out[p++] = 12u;
    out[p++] = 5u;
    out[p++] = 'd';
    out[p++] = 'i';
    out[p++] = 'h';
    out[p++] = 'o';
    out[p++] = 's';
    out[p++] = 55u;
    out[p++] = 4u;
    out[p++] = 1u;  /* subnet */
    out[p++] = 3u;  /* router */
    out[p++] = 6u;  /* dns */
    out[p++] = 15u; /* domain */
    if (requested_ip)
    {
        out[p++] = 50u;
        out[p++] = 4u;
        put_be32(out + p, requested_ip);
        p += 4u;
    }
    if (server_id)
    {
        out[p++] = 54u;
        out[p++] = 4u;
        put_be32(out + p, server_id);
        p += 4u;
    }
    out[p++] = 57u;
    out[p++] = 2u;
    put_be16(out + p, 1500u);
    p += 2u;
    out[p++] = 255u;
    if (p < 300u)
        p = 300u;
    return p;
}

typedef struct dhcp_parse_result
{
    uint8_t msg_type;
    uint32_t yiaddr;
    uint32_t server_id;
    uint32_t mask;
    uint32_t router;
    uint32_t dns;
} dhcp_parse_result;

static int parse_dhcp_packet(const uint8_t *frame, uint32_t len, dhcp_parse_result *out)
{
    uint32_t ip_off = 14u;
    uint32_t udp_off;
    uint32_t dhcp_off;
    uint32_t opts;
    uint32_t end;

    if (!frame || !out || len < 14u + 20u + 8u + 240u)
        return -1;
    if (get_be16(frame + 12u) != KNET_ETH_TYPE_IPV4)
        return -1;
    if ((frame[ip_off] >> 4) != 4u || frame[ip_off + 9u] != KNET_IP_PROTO_UDP)
        return -1;
    udp_off = ip_off + ((uint32_t)(frame[ip_off] & 0x0Fu) * 4u);
    if (udp_off + 8u + 240u > len)
        return -1;
    if (get_be16(frame + udp_off + 2u) != KNET_DHCP_CLIENT_PORT)
        return -1;
    dhcp_off = udp_off + 8u;
    if (frame[dhcp_off] != 2u || get_be32(frame + dhcp_off + 4u) != g_dhcp_xid)
        return -1;
    if (get_be32(frame + dhcp_off + 236u) != KNET_DHCP_MAGIC)
        return -1;

    memset(out, 0, sizeof(*out));
    out->yiaddr = get_be32(frame + dhcp_off + 16u);
    opts = dhcp_off + 240u;
    end = len;
    while (opts + 1u < end)
    {
        uint8_t opt = frame[opts++];
        uint8_t opt_len;
        if (opt == 0u)
            continue;
        if (opt == 255u)
            break;
        if (opts >= end)
            break;
        opt_len = frame[opts++];
        if (opts + opt_len > end)
            break;
        if (opt == 53u && opt_len >= 1u)
            out->msg_type = frame[opts];
        else if (opt == 54u && opt_len >= 4u)
            out->server_id = get_be32(frame + opts);
        else if (opt == 1u && opt_len >= 4u)
            out->mask = get_be32(frame + opts);
        else if (opt == 3u && opt_len >= 4u)
            out->router = get_be32(frame + opts);
        else if (opt == 6u && opt_len >= 4u)
            out->dns = get_be32(frame + opts);
        opts += opt_len;
    }
    return out->msg_type ? 0 : -1;
}

static int dhcp_send(uint8_t msg_type, uint32_t requested_ip, uint32_t server_id)
{
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t dhcp[548];
    uint32_t dhcp_len = build_dhcp(dhcp, msg_type, requested_ip, server_id);
    uint32_t frame_len = build_udp_ipv4(g_tx, bcast, 0u, 0xFFFFFFFFu,
                                        KNET_DHCP_CLIENT_PORT, KNET_DHCP_SERVER_PORT,
                                        dhcp, (uint16_t)dhcp_len);
    return usb_ethernet_send_frame(g_tx, frame_len);
}

static int dhcp_wait(uint8_t want_type, dhcp_parse_result *out, uint32_t rounds)
{
    uint32_t frames = 0u;
    uint32_t ipv4 = 0u;
    uint32_t udp = 0u;
    uint32_t udp68 = 0u;
    uint32_t bootp = 0u;
    uint32_t xid_bad = 0u;
    uint32_t magic_bad = 0u;
    uint32_t type_seen = 0u;
    uint32_t last_len = 0u;
    uint16_t last_eth = 0u;
    uint16_t last_udp_src = 0u;
    uint16_t last_udp_dst = 0u;
    uint32_t last_src_ip = 0u;
    uint32_t usb_polls = 0u;
    uint32_t scanned = 0u;
    uint64_t wait_started = knet_now_ms();
    uint64_t deadline = wait_started + (uint64_t)(rounds ? rounds : 1u) * 1000u;

    while (!knet_fetch_cancelled() && knet_now_ms() < deadline && scanned < 256u)
    {
        uint32_t got = 0u;
        uint8_t from_pending = usb_ethernet_pending_frames() ? 1u : 0u;
        ++scanned;
        if (usb_ethernet_recv_frame(g_rx, sizeof(g_rx), &got) == 0)
        {
            dhcp_parse_result r;
            frames++;
            last_len = got;
            if (got >= 14u)
                last_eth = get_be16(g_rx + 12u);
            if (got >= 14u + 20u + 8u && last_eth == KNET_ETH_TYPE_IPV4 &&
                (g_rx[14u] >> 4) == 4u && g_rx[23u] == KNET_IP_PROTO_UDP)
            {
                uint32_t udp_off = 14u + ((uint32_t)(g_rx[14u] & 0x0Fu) * 4u);
                ipv4++;
                udp++;
                last_src_ip = get_be32(g_rx + 26u);
                if (udp_off + 8u <= got)
                {
                    uint32_t dhcp_off = udp_off + 8u;
                    last_udp_src = get_be16(g_rx + udp_off);
                    last_udp_dst = get_be16(g_rx + udp_off + 2u);
                    if (last_udp_dst == KNET_DHCP_CLIENT_PORT)
                        udp68++;
                    if (last_udp_src == KNET_DHCP_SERVER_PORT && last_udp_dst == KNET_DHCP_CLIENT_PORT &&
                        dhcp_off + 240u <= got && g_rx[dhcp_off] == 2u)
                    {
                        bootp++;
                        if (get_be32(g_rx + dhcp_off + 4u) != g_dhcp_xid)
                            xid_bad++;
                        else if (get_be32(g_rx + dhcp_off + 236u) != KNET_DHCP_MAGIC)
                            magic_bad++;
                    }
                }
            }
            if (parse_dhcp_packet(g_rx, got, &r) == 0 && r.msg_type == want_type)
            {
                *out = r;
                return 0;
            }
            if (parse_dhcp_packet(g_rx, got, &r) == 0)
                type_seen = r.msg_type;
        }
        if (!from_pending)
            ++usb_polls;
    }
    terminal_print_inline("net: DHCP wait saw frames=");
    print_dec_inline(frames);
    terminal_print_inline(" ipv4=");
    print_dec_inline(ipv4);
    terminal_print_inline(" udp=");
    print_dec_inline(udp);
    terminal_print_inline(" udp68=");
    print_dec_inline(udp68);
    terminal_print_inline(" bootp=");
    print_dec_inline(bootp);
    terminal_print_inline(" xid_bad=");
    print_dec_inline(xid_bad);
    terminal_print_inline(" magic_bad=");
    print_dec_inline(magic_bad);
    terminal_print_inline(" type=");
    print_dec_inline(type_seen);
    terminal_print_inline(" last_len=");
    print_dec_inline(last_len);
    terminal_print_inline(" last_src=");
    print_ip_inline(last_src_ip);
    terminal_print_inline(" last_eth=");
    terminal_print_inline_hex32(last_eth);
    terminal_print_inline(" last_udp_src=");
    terminal_print_inline_hex32(last_udp_src);
    terminal_print_inline(" last_udp_dst=");
    terminal_print_inline_hex32(last_udp_dst);
    terminal_print_inline(" usb_polls=");
    print_dec_inline(usb_polls);
    terminal_print_inline(" scanned=");
    print_dec_inline(scanned);
    terminal_print_inline(" wait_ms=");
    print_dec_inline((uint32_t)(knet_now_ms() - wait_started));
    terminal_print("");
    return -1;
}

static int arp_send_request(uint32_t target_ip)
{
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    for (uint32_t i = 0u; i < 6u; ++i)
        g_tx[i] = bcast[i];
    for (uint32_t i = 0u; i < 6u; ++i)
        g_tx[6u + i] = g_knet.mac[i];
    put_be16(g_tx + 12u, KNET_ETH_TYPE_ARP);
    put_be16(g_tx + 14u, 1u); /* Ethernet */
    put_be16(g_tx + 16u, KNET_ETH_TYPE_IPV4);
    g_tx[18u] = 6u;
    g_tx[19u] = 4u;
    put_be16(g_tx + 20u, 1u); /* request */
    for (uint32_t i = 0u; i < 6u; ++i)
        g_tx[22u + i] = g_knet.mac[i];
    put_be32(g_tx + 28u, g_knet.ip);
    for (uint32_t i = 0u; i < 6u; ++i)
        g_tx[32u + i] = 0u;
    put_be32(g_tx + 38u, target_ip);
    for (uint32_t i = 42u; i < 60u; ++i)
        g_tx[i] = 0u;
    return usb_ethernet_send_frame(g_tx, 60u);
}

static int parse_arp_reply(const uint8_t *frame, uint32_t len, uint32_t target_ip, uint8_t out_mac[6])
{
    if (!frame || !out_mac || len < 42u)
        return -1;
    if (get_be16(frame + 12u) != KNET_ETH_TYPE_ARP)
        return -1;
    if (get_be16(frame + 14u) != 1u || get_be16(frame + 16u) != KNET_ETH_TYPE_IPV4)
        return -1;
    if (frame[18u] != 6u || frame[19u] != 4u || get_be16(frame + 20u) != 2u)
        return -1;
    if (get_be32(frame + 28u) != target_ip)
        return -1;
    if (get_be32(frame + 38u) != g_knet.ip)
        return -1;
    for (uint32_t i = 0u; i < 6u; ++i)
        out_mac[i] = frame[22u + i];
    return 0;
}

static int arp_resolve(uint32_t target_ip, uint8_t out_mac[6], uint32_t rounds)
{
    uint32_t usb_polls = 0u;
    uint32_t scanned = 0u;
    uint32_t retries = 0u;
    uint64_t deadline = knet_now_ms() + (uint64_t)(rounds ? rounds : 1u) * 1000u;
    uint64_t retry_at;
    if (!target_ip || !out_mac)
        return -1;
    terminal_print_inline("net: ARP request ");
    print_ip_inline(target_ip);
    terminal_print("");
    if (arp_send_request(target_ip) != 0)
        return -1;
    retry_at = knet_now_ms() + 500u;
    while (!knet_fetch_cancelled() && knet_now_ms() < deadline && scanned < 256u)
    {
        uint32_t got = 0u;
        uint8_t from_pending = usb_ethernet_pending_frames() ? 1u : 0u;
        if (!from_pending && retries < 3u && knet_now_ms() >= retry_at)
        {
            (void)arp_send_request(target_ip);
            ++retries;
            retry_at = knet_now_ms() + 500u;
        }
        ++scanned;
        if (usb_ethernet_recv_frame(g_rx, sizeof(g_rx), &got) == 0 &&
            parse_arp_reply(g_rx, got, target_ip, out_mac) == 0)
            return 0;
        if (!from_pending)
            ++usb_polls;
    }
    return -1;
}

int knet_usb_dhcp(uint32_t rounds)
{
    dhcp_parse_result offer;
    dhcp_parse_result ack;
    uint8_t offer_received = 0u;
    uint8_t ack_received = 0u;

    if (rounds == 0u)
        rounds = 4u;
    if (rounds > 64u)
        rounds = 64u;

    memset(&g_knet, 0, sizeof(g_knet));
    memset(g_dns_cache, 0, sizeof(g_dns_cache));
    g_dns_cache_next = 0u;
    if (knet_refresh_link() != 0)
    {
        terminal_warn("net: USB Ethernet link not ready");
        return -1;
    }

    g_dhcp_xid = KNET_DHCP_XID ^ (uint32_t)knet_now_ms();
    if (usb_ethernet_prepare_receive() != 0)
    {
        knet_copy(g_knet.detail, sizeof(g_knet.detail), "Ethernet receive preparation failed");
        terminal_warn(g_knet.detail);
        return -1;
    }
    terminal_success("net: Ethernet receive path ready");

    for (uint32_t attempt = 0u; attempt < rounds && !knet_fetch_cancelled(); ++attempt)
    {
        terminal_print("net: DHCP discover");
        if (dhcp_send(1u, 0u, 0u) != 0)
        {
            knet_copy(g_knet.detail, sizeof(g_knet.detail), "DHCP discover transmit failed");
            terminal_warn(g_knet.detail);
            return -1;
        }
        if (dhcp_wait(2u, &offer, 1u) == 0)
        {
            offer_received = 1u;
            break;
        }
    }
    if (!offer_received)
    {
        knet_copy(g_knet.detail, sizeof(g_knet.detail), "DHCP offer timeout");
        terminal_warn(g_knet.detail);
        return -1;
    }

    terminal_print_inline("net: DHCP offer ");
    print_ip_inline(offer.yiaddr);
    terminal_print("");

    for (uint32_t attempt = 0u; attempt < rounds && !knet_fetch_cancelled(); ++attempt)
    {
        if (dhcp_send(3u, offer.yiaddr, offer.server_id) != 0)
        {
            knet_copy(g_knet.detail, sizeof(g_knet.detail), "DHCP request transmit failed");
            terminal_warn(g_knet.detail);
            return -1;
        }
        if (dhcp_wait(5u, &ack, 1u) == 0)
        {
            ack_received = 1u;
            break;
        }
    }
    if (!ack_received)
    {
        knet_copy(g_knet.detail, sizeof(g_knet.detail), "DHCP ack timeout");
        terminal_warn(g_knet.detail);
        return -1;
    }

    g_knet.ip = ack.yiaddr ? ack.yiaddr : offer.yiaddr;
    g_knet.mask = ack.mask ? ack.mask : offer.mask;
    g_knet.router = ack.router ? ack.router : offer.router;
    g_knet.dns = ack.dns ? ack.dns : offer.dns;
    g_knet.configured = 1u;
    knet_copy(g_knet.detail, sizeof(g_knet.detail), "DHCP configured");

    terminal_print_inline("net: DHCP ACK ip=");
    print_ip_inline(g_knet.ip);
    terminal_print_inline(" router=");
    print_ip_inline(g_knet.router);
    terminal_print_inline(" dns=");
    print_ip_inline(g_knet.dns);
    terminal_print("");
    if (g_knet.router && arp_resolve(g_knet.router, g_knet.router_mac, rounds) == 0)
    {
        g_knet.router_mac_valid = 1u;
        terminal_success("net: router ARP resolved");
    }
    else if (g_knet.router)
    {
        terminal_warn("net: router ARP timeout");
    }
    terminal_success("net: USB Ethernet IPv4 configured");
    return 0;
}

void knet_usb_get_status(knet_usb_status *out_status)
{
    if (!out_status)
        return;
    *out_status = g_knet;
    if (!out_status->link_online)
    {
        (void)knet_refresh_link();
        *out_status = g_knet;
    }
}

static int knet_usb_request_url(const char *url, uint32_t max_bytes,
                                const char *method, const uint8_t *request_body,
                                uint32_t request_body_size, const char *content_type)
{
    typedef struct tcp_packet
    {
        uint32_t seq;
        uint32_t ack;
        const uint8_t *payload;
        uint16_t payload_len;
        uint8_t flags;
    } tcp_packet;

    static const uint8_t tcp_syn = 0x02u;
    static const uint8_t tcp_rst = 0x04u;
    static const uint8_t tcp_psh = 0x08u;
    static const uint8_t tcp_ack = 0x10u;
    static const uint8_t tcp_fin = 0x01u;
    char host[128];
    char path[512];
    uint8_t request[6144];
    uint8_t next_mac[6];
    uint32_t server_ip = 0u;
    uint16_t server_port = 80u;
    uint16_t local_port;
    uint8_t use_tls = 0u;
    uint32_t tx_seq;
    uint32_t rx_seq = 0u;
    uint32_t request_len = 0u;
    uint32_t printed = 0u;
    uint8_t reused_configuration;
    uint32_t i;
    const char *p;

    ++g_tcp_port;
    if (g_tcp_port < 49152u || g_tcp_port == 0xFFFFu)
        g_tcp_port = 49153u;
    local_port = g_tcp_port;
    g_tcp_sequence += 0x00010001u + (uint32_t)(knet_now_ms() & 0xFFFFu);
    tx_seq = g_tcp_sequence;

    /* Local helpers are expressed as macros here to keep this freestanding C11 unit self-contained. */
#define KNET_TCP_SUM_ADD(sum_, value_) ((sum_) += (uint32_t)(value_))

    if (!url || !url[0])
    {
        terminal_error("net:get needs a URL");
        return -1;
    }
    if (knet_starts_with(url, "https://"))
    {
        use_tls = 1u;
        server_port = 443u;
        p = url + 8u;
    }
    else if (knet_starts_with(url, "http://"))
    {
        p = url + 7u;
    }
    else
    {
        terminal_error("net:get supports http:// or https:// URLs");
        return -1;
    }
    i = 0u;
    while (*p && *p != '/' && *p != ':' && i + 1u < sizeof(host))
        host[i++] = *p++;
    host[i] = 0;
    if (!host[0])
    {
        terminal_error("net:get URL is missing a host");
        return -1;
    }
    if (*p == ':')
    {
        uint16_t wanted_port = use_tls ? 443u : 80u;
        ++p;
        if (wanted_port == 80u && knet_starts_with(p, "80") && (p[2] == '/' || p[2] == 0))
            p += 2u;
        else if (wanted_port == 443u && knet_starts_with(p, "443") &&
                 (p[3] == '/' || p[3] == 0))
            p += 3u;
        else
        {
            terminal_error("net:get supports default HTTP/HTTPS ports only");
            return -1;
        }
    }
    i = 0u;
    if (!*p)
        path[i++] = '/';
    else
        while (*p && i + 1u < sizeof(path))
            path[i++] = *p++;
    path[i] = 0;

    if (max_bytes < 128u)
        max_bytes = 128u;
    if (max_bytes > (g_fetch_output ? g_fetch_capacity : 65536u))
        max_bytes = g_fetch_output ? g_fetch_capacity : 65536u;
    reused_configuration = g_knet.configured;
    if (!g_knet.configured && knet_usb_dhcp(4u) != 0)
        return -1;

    /* A completed TLS connection can leave late ACK/FIN traffic queued in the
       adapter. Before reusing the lease, drain through a real L2 round trip and
       refresh the router MAC so the next DNS/TCP exchange starts cleanly. */
    if (reused_configuration && g_knet.router)
    {
        terminal_print("net: validating router path");
        terminal_flush_log();
        if (arp_resolve(g_knet.router, next_mac, 2u) != 0)
        {
            g_knet.router_mac_valid = 0u;
            terminal_error("net:get router path validation failed");
            return -1;
        }
        memcpy(g_knet.router_mac, next_mac, 6u);
        g_knet.router_mac_valid = 1u;
        terminal_success("net: router path ready");
    }

    /* Parse a dotted IPv4 host, otherwise perform a minimal DNS A query. */
    {
        uint32_t part = 0u;
        uint32_t parts = 0u;
        uint32_t parsed = 0u;
        uint8_t valid = 1u;
        for (i = 0u;; ++i)
        {
            char c = host[i];
            if (c >= '0' && c <= '9')
            {
                part = part * 10u + (uint32_t)(c - '0');
                if (part > 255u)
                    valid = 0u;
            }
            else if (c == '.' || c == 0)
            {
                parsed = (parsed << 8) | part;
                ++parts;
                part = 0u;
                if (!c)
                    break;
            }
            else
            {
                valid = 0u;
                break;
            }
        }
        if (valid && parts == 4u)
            server_ip = parsed;
    }

    if (!server_ip)
        for (uint32_t cache_i = 0u; cache_i < sizeof(g_dns_cache) / sizeof(g_dns_cache[0]); ++cache_i)
            if (g_dns_cache[cache_i].ip && knet_text_equal(host, g_dns_cache[cache_i].host))
            {
                server_ip = g_dns_cache[cache_i].ip;
                terminal_print_inline("net: DNS cache hit ");
                terminal_print(host);
                break;
            }

    if (!server_ip)
    {
        uint8_t dns[512];
        uint32_t d = 12u;
        uint32_t label = 0u;
        uint16_t dns_port = ++g_dns_port;
        uint32_t dns_id = ++g_dns_id;
        uint32_t next_hop = ((g_knet.ip & g_knet.mask) == (g_knet.dns & g_knet.mask)) ? g_knet.dns : g_knet.router;

        if (!g_knet.dns || !next_hop)
        {
            terminal_error("net:get has no DNS server or route");
            return -1;
        }
        if (next_hop == g_knet.router && g_knet.router_mac_valid)
            memcpy(next_mac, g_knet.router_mac, 6u);
        else if (arp_resolve(next_hop, next_mac, 4u) != 0)
        {
            terminal_error("net:get could not resolve DNS next-hop MAC");
            return -1;
        }
        if (next_hop == g_knet.router)
        {
            memcpy(g_knet.router_mac, next_mac, 6u);
            g_knet.router_mac_valid = 1u;
        }

        memset(dns, 0, sizeof(dns));
        put_be16(dns, (uint16_t)dns_id);
        put_be16(dns + 2u, 0x0100u);
        put_be16(dns + 4u, 1u);
        while (host[label])
        {
            uint32_t start = label;
            uint32_t length;
            while (host[label] && host[label] != '.')
                ++label;
            length = label - start;
            if (!length || length > 63u || d + length + 6u >= sizeof(dns))
            {
                terminal_error("net:get invalid DNS hostname");
                return -1;
            }
            dns[d++] = (uint8_t)length;
            while (start < label)
                dns[d++] = (uint8_t)host[start++];
            if (host[label] == '.')
                ++label;
        }
        dns[d++] = 0u;
        put_be16(dns + d, 1u);
        put_be16(dns + d + 2u, 1u);
        d += 4u;

        terminal_print_inline("net: DNS lookup ");
        terminal_print(host);
        terminal_flush_log();
        uint32_t dns_frame_len = build_udp_ipv4(g_tx, next_mac, g_knet.ip, g_knet.dns,
                                                dns_port, 53u, dns, (uint16_t)d);
        if (usb_ethernet_send_frame(g_tx, dns_frame_len) != 0)
        {
            terminal_error("net:get DNS transmit failed");
            return -1;
        }
        {
        uint32_t dns_polls = 0u;
        uint32_t dns_scanned = 0u;
        uint64_t dns_deadline = knet_now_ms() + 8000u;
        uint64_t dns_retry_at = knet_now_ms() + 1000u;
        uint32_t dns_retries = 0u;
        while (!knet_fetch_cancelled() && knet_now_ms() < dns_deadline && !server_ip && dns_scanned < 1024u)
        {
            uint32_t got = 0u;
            uint32_t ip_off = 14u;
            uint32_t udp_off;
            uint32_t dns_off;
            uint32_t dns_len;
            uint32_t off;
            uint16_t qd;
            uint16_t an;
            uint8_t from_pending = usb_ethernet_pending_frames() ? 1u : 0u;
            if (!from_pending && dns_retries < 4u && knet_now_ms() >= dns_retry_at)
            {
                (void)usb_ethernet_send_frame(g_tx, dns_frame_len);
                ++dns_retries;
                dns_retry_at = knet_now_ms() + 1000u;
            }
            ++dns_scanned;
            if (!from_pending)
                ++dns_polls;
            if (usb_ethernet_recv_frame(g_rx, sizeof(g_rx), &got) != 0 || got < 54u)
                continue;
            if (get_be16(g_rx + 12u) != KNET_ETH_TYPE_IPV4 || g_rx[23u] != KNET_IP_PROTO_UDP)
                continue;
            udp_off = ip_off + ((uint32_t)(g_rx[ip_off] & 0x0Fu) * 4u);
            if (udp_off + 8u > got || get_be16(g_rx + udp_off) != 53u ||
                get_be16(g_rx + udp_off + 2u) != dns_port)
                continue;
            dns_off = udp_off + 8u;
            dns_len = get_be16(g_rx + udp_off + 4u) - 8u;
            if (dns_off + dns_len > got || dns_len < 12u || get_be16(g_rx + dns_off) != dns_id)
                continue;
            if ((get_be16(g_rx + dns_off + 2u) & 0x800Fu) != 0x8000u)
                continue;
            qd = get_be16(g_rx + dns_off + 4u);
            an = get_be16(g_rx + dns_off + 6u);
            off = 12u;
            while (qd-- && off < dns_len)
            {
                while (off < dns_len && g_rx[dns_off + off])
                {
                    uint8_t n = g_rx[dns_off + off];
                    if ((n & 0xC0u) == 0xC0u) { off += 2u; break; }
                    off += 1u + n;
                }
                if (off < dns_len && g_rx[dns_off + off] == 0u)
                    ++off;
                off += 4u;
            }
            while (an-- && off + 10u <= dns_len)
            {
                uint16_t type;
                uint16_t klass;
                uint16_t rdlen;
                if ((g_rx[dns_off + off] & 0xC0u) == 0xC0u)
                    off += 2u;
                else
                {
                    while (off < dns_len && g_rx[dns_off + off])
                        off += 1u + g_rx[dns_off + off];
                    ++off;
                }
                if (off + 10u > dns_len)
                    break;
                type = get_be16(g_rx + dns_off + off);
                klass = get_be16(g_rx + dns_off + off + 2u);
                rdlen = get_be16(g_rx + dns_off + off + 8u);
                off += 10u;
                if (off + rdlen > dns_len)
                    break;
                if (type == 1u && klass == 1u && rdlen == 4u)
                {
                    server_ip = get_be32(g_rx + dns_off + off);
                    break;
                }
                off += rdlen;
            }
        }
        }
        if (!server_ip)
        {
            terminal_error("net:get DNS A-record timeout");
            return -1;
        }
        terminal_print_inline("net: DNS answer ");
        print_ip_inline(server_ip);
        terminal_print("");
        knet_copy(g_dns_cache[g_dns_cache_next].host, sizeof(g_dns_cache[g_dns_cache_next].host), host);
        g_dns_cache[g_dns_cache_next].ip = server_ip;
        g_dns_cache_next = (g_dns_cache_next + 1u) % (sizeof(g_dns_cache) / sizeof(g_dns_cache[0]));
    }

    {
        uint32_t next_hop = ((g_knet.ip & g_knet.mask) == (server_ip & g_knet.mask)) ? server_ip : g_knet.router;
        if (!next_hop)
        {
            terminal_error("net:get has no route to HTTP server");
            return -1;
        }
        if (next_hop == g_knet.router && g_knet.router_mac_valid)
            memcpy(next_mac, g_knet.router_mac, 6u);
        else if (arp_resolve(next_hop, next_mac, 4u) != 0)
        {
            terminal_error("net:get next-hop ARP timeout");
            return -1;
        }
        if (next_hop == g_knet.router)
        {
            memcpy(g_knet.router_mac, next_mac, 6u);
            g_knet.router_mac_valid = 1u;
        }
    }

    /* Build and send TCP packets inline; all fields are host-order until encoded. */
#define KNET_SEND_TCP(seq_, ack_, flags_, payload_, payload_len_, syn_opts_) do { \
        uint32_t tcp_hlen_ = (syn_opts_) ? 24u : 20u; \
        uint32_t tcp_len_ = tcp_hlen_ + (uint32_t)(payload_len_); \
        uint32_t frame_len_ = 14u + 20u + tcp_len_; \
        uint32_t sum_ = 0u; \
        uint32_t ci_; \
        memcpy(g_tx, next_mac, 6u); memcpy(g_tx + 6u, g_knet.mac, 6u); \
        put_be16(g_tx + 12u, KNET_ETH_TYPE_IPV4); \
        g_tx[14u] = 0x45u; g_tx[15u] = 0u; put_be16(g_tx + 16u, (uint16_t)(20u + tcp_len_)); \
        put_be16(g_tx + 18u, g_ip_id++); put_be16(g_tx + 20u, 0x4000u); \
        g_tx[22u] = 64u; g_tx[23u] = KNET_IP_PROTO_TCP; put_be16(g_tx + 24u, 0u); \
        put_be32(g_tx + 26u, g_knet.ip); put_be32(g_tx + 30u, server_ip); \
        put_be16(g_tx + 24u, ip_checksum(g_tx + 14u, 20u)); \
        put_be16(g_tx + 34u, local_port); put_be16(g_tx + 36u, server_port); \
        put_be32(g_tx + 38u, (seq_)); put_be32(g_tx + 42u, (ack_)); \
        g_tx[46u] = (uint8_t)((tcp_hlen_ / 4u) << 4); g_tx[47u] = (uint8_t)(flags_); \
        put_be16(g_tx + 48u, 64240u); put_be16(g_tx + 50u, 0u); put_be16(g_tx + 52u, 0u); \
        if (syn_opts_) { g_tx[54u] = 2u; g_tx[55u] = 4u; put_be16(g_tx + 56u, 1460u); } \
        if ((payload_len_)) memcpy(g_tx + 14u + 20u + tcp_hlen_, (payload_), (payload_len_)); \
        KNET_TCP_SUM_ADD(sum_, (g_knet.ip >> 16) & 0xFFFFu); KNET_TCP_SUM_ADD(sum_, g_knet.ip & 0xFFFFu); \
        KNET_TCP_SUM_ADD(sum_, (server_ip >> 16) & 0xFFFFu); KNET_TCP_SUM_ADD(sum_, server_ip & 0xFFFFu); \
        KNET_TCP_SUM_ADD(sum_, KNET_IP_PROTO_TCP); KNET_TCP_SUM_ADD(sum_, tcp_len_); \
        for (ci_ = 0u; ci_ + 1u < tcp_len_; ci_ += 2u) KNET_TCP_SUM_ADD(sum_, get_be16(g_tx + 34u + ci_)); \
        if (tcp_len_ & 1u) KNET_TCP_SUM_ADD(sum_, (uint16_t)g_tx[34u + tcp_len_ - 1u] << 8); \
        while (sum_ >> 16) sum_ = (sum_ & 0xFFFFu) + (sum_ >> 16); \
        put_be16(g_tx + 50u, (uint16_t)~sum_); \
        if (frame_len_ < 60u) { memset(g_tx + frame_len_, 0, 60u - frame_len_); frame_len_ = 60u; } \
        if (usb_ethernet_send_frame(g_tx, frame_len_) != 0) { terminal_error("net:get TCP transmit failed"); return -1; } \
    } while (0)

#define KNET_PARSE_TCP(packet_, ok_) do { \
        uint32_t got_ = 0u; uint32_t ihl_; uint32_t to_; uint32_t thl_; uint32_t total_; \
        (ok_) = 0u; \
        if (usb_ethernet_recv_frame(g_rx, sizeof(g_rx), &got_) == 0 && got_ >= 54u && \
            get_be16(g_rx + 12u) == KNET_ETH_TYPE_IPV4 && g_rx[23u] == KNET_IP_PROTO_TCP && \
            get_be32(g_rx + 26u) == server_ip && get_be32(g_rx + 30u) == g_knet.ip) { \
            ihl_ = (uint32_t)(g_rx[14u] & 0x0Fu) * 4u; to_ = 14u + ihl_; total_ = get_be16(g_rx + 16u); \
            if (to_ + 20u <= got_ && get_be16(g_rx + to_) == server_port && get_be16(g_rx + to_ + 2u) == local_port) { \
                thl_ = (uint32_t)(g_rx[to_ + 12u] >> 4) * 4u; \
                if (thl_ >= 20u && ihl_ + thl_ <= total_ && 14u + total_ <= got_) { \
                    (packet_).seq = get_be32(g_rx + to_ + 4u); (packet_).ack = get_be32(g_rx + to_ + 8u); \
                    (packet_).flags = g_rx[to_ + 13u]; (packet_).payload = g_rx + to_ + thl_; \
                    (packet_).payload_len = (uint16_t)(total_ - ihl_ - thl_); (ok_) = 1u; \
                } \
            } \
        } \
    } while (0)

    terminal_print_inline("net: TCP connect ");
    print_ip_inline(server_ip);
    terminal_print_inline(":");
    print_dec_inline(server_port);
    terminal_print("");
    terminal_flush_log();
    KNET_SEND_TCP(tx_seq, 0u, tcp_syn, 0, 0u, 1u);
    {
    uint32_t tcp_polls = 0u;
    uint32_t tcp_scanned = 0u;
    uint64_t tcp_deadline = knet_now_ms() + 8000u;
    uint64_t tcp_retry_at = knet_now_ms() + 1000u;
    uint32_t tcp_retries = 0u;
    while (!knet_fetch_cancelled() && knet_now_ms() < tcp_deadline && tcp_scanned < 1024u)
    {
        tcp_packet packet;
        uint8_t ok;
        uint8_t from_pending = usb_ethernet_pending_frames() ? 1u : 0u;
        if (!from_pending && tcp_retries < 4u && knet_now_ms() >= tcp_retry_at)
        {
            KNET_SEND_TCP(tx_seq, 0u, tcp_syn, 0, 0u, 1u);
            ++tcp_retries;
            tcp_retry_at = knet_now_ms() + 1000u;
        }
        ++tcp_scanned;
        if (!from_pending)
            ++tcp_polls;
        KNET_PARSE_TCP(packet, ok);
        if (!ok)
            continue;
        if (packet.flags & tcp_rst)
        {
            terminal_error("net:get TCP connection reset during handshake");
            return -1;
        }
        if ((packet.flags & (tcp_syn | tcp_ack)) == (tcp_syn | tcp_ack) && packet.ack == tx_seq + 1u)
        {
            ++tx_seq;
            rx_seq = packet.seq + 1u;
            break;
        }
    }
    }
    if (!rx_seq)
    {
        terminal_error("net:get TCP SYN timeout");
        return -1;
    }
    KNET_SEND_TCP(tx_seq, rx_seq, tcp_ack, 0, 0u, 0u);
    terminal_success("net: TCP connected");

#define KNET_REQ_TEXT(text_) do { \
        const char *s_ = (text_); while (*s_ && request_len < sizeof(request)) request[request_len++] = (uint8_t)*s_++; \
    } while (0)
    if (!method || !method[0])
        method = "GET";
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0)
    {
        terminal_error("net:get unsupported HTTP method");
        return -1;
    }
    if (request_body_size > 4096u || (request_body_size && !request_body))
    {
        terminal_error("net:get HTTP request body is too large");
        return -1;
    }
    KNET_REQ_TEXT(method);
    KNET_REQ_TEXT(" ");
    KNET_REQ_TEXT(path);
    KNET_REQ_TEXT(" HTTP/1.1\r\nHost: ");
    KNET_REQ_TEXT(host);
    KNET_REQ_TEXT("\r\nUser-Agent: Mozilla/5.0 (DIHOS; Dihscover/0.2) AppleWebKit/537.36 Safari/537.36\r\nAccept: */*\r\nAccept-Encoding: identity\r\n");
    if (request_body_size)
    {
        char decimal[16];
        uint32_t value = request_body_size, digits = 0u;
        do { decimal[digits++] = (char)('0' + value % 10u); value /= 10u; } while (value && digits < sizeof(decimal));
        KNET_REQ_TEXT("Content-Type: ");
        KNET_REQ_TEXT(content_type && content_type[0] ? content_type : "application/x-www-form-urlencoded");
        KNET_REQ_TEXT("\r\nContent-Length: ");
        while (digits && request_len < sizeof(request)) request[request_len++] = (uint8_t)decimal[--digits];
        KNET_REQ_TEXT("\r\n");
    }
    KNET_REQ_TEXT("Connection: close\r\n\r\n");
    if (request_body_size)
    {
        if (request_body_size > sizeof(request) - request_len)
        {
            terminal_error("net:get HTTP request is too long");
            return -1;
        }
        memcpy(request + request_len, request_body, request_body_size);
        request_len += request_body_size;
    }
    if (request_len >= sizeof(request))
    {
        terminal_error("net:get HTTP request is too long");
        return -1;
    }

    if (use_tls)
    {
        uint8_t entropy[48];
        uint32_t request_off = 0u;
        uint32_t pending_len = 0u;
        uint32_t pending_off = 0u;
        uint64_t tls_deadline = knet_now_ms() + 30000u;
        uint8_t request_flushed = 0u;
        uint8_t tcp_fin_seen = 0u;

        terminal_warn("net: TLS root trust store not installed; chain and hostname checks only");
        terminal_warn("net: TLS entropy is provisional until a hardware RNG driver is available");
        terminal_warn("net: TLS certificate time is temporarily pinned to 2026-08-05");

        br_ssl_client_init_full(&g_tls_client, &g_tls_x509, 0, 0u);
        br_x509_minimal_set_time(&g_tls_x509, 740198u,
                                 (uint32_t)(dihos_time_seconds() % 86400u));
        g_tls_noanchor.vtable = &g_knet_x509_noanchor_vtable;
        g_tls_noanchor.inner = &g_tls_x509.vtable;
        br_ssl_engine_set_x509(&g_tls_client.eng, &g_tls_noanchor.vtable);
        br_ssl_engine_set_versions(&g_tls_client.eng, BR_TLS12, BR_TLS12);
        br_ssl_engine_set_buffer(&g_tls_client.eng, g_tls_iobuf, sizeof(g_tls_iobuf), 1);
        knet_tls_entropy(entropy);
        br_ssl_engine_inject_entropy(&g_tls_client.eng, entropy, sizeof(entropy));
        memset(entropy, 0, sizeof(entropy));
        if (!br_ssl_client_reset(&g_tls_client, host, 0))
        {
            terminal_error("net:get TLS client reset failed");
            KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
            return -1;
        }

        terminal_print_inline("net: TLS 1.2 handshake ");
        terminal_print(host);
        terminal_flush_log();
        for (uint32_t guard = 0u; !knet_fetch_cancelled() && guard < 8192u; ++guard)
        {
            unsigned state = br_ssl_engine_current_state(&g_tls_client.eng);
            size_t tls_len = 0u;
            unsigned char *tls_buf;

            if (state == BR_SSL_CLOSED)
            {
                int err = br_ssl_engine_last_error(&g_tls_client.eng);
                if (err != 0)
                {
                    terminal_print_inline("net:get TLS error ");
                    print_dec_inline((uint32_t)err);
                    terminal_print("");
                    KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
                    return -1;
                }
                terminal_print("");
                terminal_success("net:get HTTPS response complete");
                KNET_SEND_TCP(tx_seq, rx_seq, tcp_fin | tcp_ack, 0, 0u, 0u);
                return 0;
            }

            if (state & BR_SSL_SENDREC)
            {
                uint32_t take;
                tls_buf = br_ssl_engine_sendrec_buf(&g_tls_client.eng, &tls_len);
                take = (uint32_t)tls_len;
                if (take > 1300u)
                    take = 1300u;
                if (tls_buf && take)
                {
                    KNET_SEND_TCP(tx_seq, rx_seq, tcp_psh | tcp_ack, tls_buf, take, 0u);
                    tx_seq += take;
                    br_ssl_engine_sendrec_ack(&g_tls_client.eng, take);
                    tls_deadline = knet_now_ms() + 30000u;
                    continue;
                }
            }

            if ((state & BR_SSL_SENDAPP) && request_off < request_len)
            {
                uint32_t take;
                tls_buf = br_ssl_engine_sendapp_buf(&g_tls_client.eng, &tls_len);
                take = request_len - request_off;
                if (take > tls_len)
                    take = (uint32_t)tls_len;
                if (tls_buf && take)
                {
                    memcpy(tls_buf, request + request_off, take);
                    request_off += take;
                    br_ssl_engine_sendapp_ack(&g_tls_client.eng, take);
                    if (request_off == request_len && !request_flushed)
                    {
                        br_ssl_engine_flush(&g_tls_client.eng, 0);
                        request_flushed = 1u;
                        terminal_success("net: TLS connected");
                        terminal_print_inline("net: HTTPS ");
                        terminal_print_inline(method);
                        terminal_print_inline(" ");
                        terminal_print(path);
                    }
                    continue;
                }
            }

            if (state & BR_SSL_RECVAPP)
            {
                uint32_t take;
                tls_buf = br_ssl_engine_recvapp_buf(&g_tls_client.eng, &tls_len);
                take = (uint32_t)tls_len;
                if (take > max_bytes - printed)
                    take = max_bytes - printed;
                knet_response_emit(tls_buf, take);
                if (tls_len)
                    br_ssl_engine_recvapp_ack(&g_tls_client.eng, tls_len);
                printed += take;
                tls_deadline = knet_now_ms() + 30000u;
                if (printed >= max_bytes)
                {
                    if (g_fetch_output)
                        g_fetch_truncated = 1u;
                    terminal_warn("net:get output truncated by max= limit");
                    KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
                    return 0;
                }
                continue;
            }

            if (state & BR_SSL_RECVREC)
            {
                if (pending_off < pending_len)
                {
                    uint32_t take = pending_len - pending_off;
                    tls_buf = br_ssl_engine_recvrec_buf(&g_tls_client.eng, &tls_len);
                    if (take > tls_len)
                        take = (uint32_t)tls_len;
                    if (tls_buf && take)
                    {
                        memcpy(tls_buf, g_tls_pending + pending_off, take);
                        pending_off += take;
                        br_ssl_engine_recvrec_ack(&g_tls_client.eng, take);
                        tls_deadline = knet_now_ms() + 30000u;
                        if (pending_off == pending_len)
                            pending_off = pending_len = 0u;
                        continue;
                    }
                }
                else if (tcp_fin_seen)
                {
                    if (printed)
                    {
                        terminal_print("");
                        terminal_success("net:get HTTPS response complete (peer closed TCP)");
                        KNET_SEND_TCP(tx_seq, rx_seq, tcp_fin | tcp_ack, 0, 0u, 0u);
                        return 0;
                    }
                    terminal_error("net:get TLS peer closed before an HTTPS response");
                    KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
                    return -1;
                }
                else
                {
                    tcp_packet packet;
                    uint8_t ok;
                    KNET_PARSE_TCP(packet, ok);
                    if (!ok)
                    {
                        if (knet_now_ms() >= tls_deadline)
                            break;
                        continue;
                    }
                    if (packet.flags & tcp_rst)
                    {
                        terminal_error("net:get TLS TCP connection reset");
                        return -1;
                    }
                    if (packet.payload_len && packet.seq == rx_seq)
                    {
                        if (packet.payload_len > sizeof(g_tls_pending))
                        {
                            terminal_error("net:get TLS TCP segment too large");
                            KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
                            return -1;
                        }
                        memcpy(g_tls_pending, packet.payload, packet.payload_len);
                        pending_len = packet.payload_len;
                        pending_off = 0u;
                        rx_seq += packet.payload_len;
                        tls_deadline = knet_now_ms() + 30000u;
                        KNET_SEND_TCP(tx_seq, rx_seq, tcp_ack, 0, 0u, 0u);
                    }
                    else if (packet.payload_len)
                    {
                        KNET_SEND_TCP(tx_seq, rx_seq, tcp_ack, 0, 0u, 0u);
                    }
                    if (packet.flags & tcp_fin)
                    {
                        if (packet.seq + packet.payload_len == rx_seq)
                            ++rx_seq;
                        tcp_fin_seen = 1u;
                        KNET_SEND_TCP(tx_seq, rx_seq, tcp_ack, 0, 0u, 0u);
                    }
                    continue;
                }
            }
        }

        terminal_print_inline("net:get TLS/HTTPS timeout, engine error ");
        print_dec_inline((uint32_t)br_ssl_engine_last_error(&g_tls_client.eng));
        terminal_print("");
        KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
        return -1;
    }

    terminal_print_inline("net: HTTP ");
    terminal_print_inline(method);
    terminal_print_inline(" ");
    terminal_print(path);
    KNET_SEND_TCP(tx_seq, rx_seq, tcp_psh | tcp_ack, request, request_len, 0u);
    tx_seq += request_len;

    {
    uint32_t http_polls = 0u;
    uint32_t http_scanned = 0u;
    uint64_t http_deadline = knet_now_ms() + 30000u;
    while (!knet_fetch_cancelled() && knet_now_ms() < http_deadline && http_scanned < 4096u)
    {
        tcp_packet packet;
        uint8_t ok;
        uint8_t from_pending = usb_ethernet_pending_frames() ? 1u : 0u;
        ++http_scanned;
        if (!from_pending)
            ++http_polls;
        KNET_PARSE_TCP(packet, ok);
        if (!ok)
            continue;
        if (packet.flags & tcp_rst)
        {
            terminal_error("net:get TCP connection reset");
            return -1;
        }
        if (packet.payload_len && packet.seq == rx_seq)
        {
            uint32_t take = packet.payload_len;
            rx_seq += packet.payload_len;
            if (take > max_bytes - printed)
                take = max_bytes - printed;
            knet_response_emit(packet.payload, take);
            printed += take;
            KNET_SEND_TCP(tx_seq, rx_seq, tcp_ack, 0, 0u, 0u);
            if (printed >= max_bytes)
            {
                if (g_fetch_output)
                    g_fetch_truncated = 1u;
                terminal_warn("net:get output truncated by max= limit");
                KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
                return 0;
            }
        }
        else if (packet.payload_len)
        {
            KNET_SEND_TCP(tx_seq, rx_seq, tcp_ack, 0, 0u, 0u);
        }
        if (packet.flags & tcp_fin)
        {
            if (packet.seq + packet.payload_len == rx_seq)
                ++rx_seq;
            KNET_SEND_TCP(tx_seq, rx_seq, tcp_fin | tcp_ack, 0, 0u, 0u);
            terminal_print("");
            terminal_success("net:get HTTP response complete");
            return 0;
        }
    }
    }

    terminal_error("net:get HTTP response timeout");
    KNET_SEND_TCP(tx_seq, rx_seq, tcp_rst | tcp_ack, 0, 0u, 0u);
    return -1;

#undef KNET_REQ_TEXT
#undef KNET_PARSE_TCP
#undef KNET_SEND_TCP
#undef KNET_TCP_SUM_ADD
}

int knet_usb_get_url(const char *url, uint32_t max_bytes)
{
    return knet_usb_request_url(url, max_bytes, "GET", 0, 0u, 0);
}

int knet_usb_fetch_request(const char *url, const char *method,
                           const uint8_t *body, uint32_t body_size,
                           const char *content_type,
                           uint8_t *response, uint32_t capacity,
                           uint32_t *out_size, uint8_t *out_truncated,
                           volatile uint32_t *cancelled, uint32_t timeout_ms)
{
    int rc;
    uint8_t was_cancelled;
    uint8_t timed_out;
    uint32_t expected = 0u;
    if (out_size)
        *out_size = 0u;
    if (out_truncated)
        *out_truncated = 0u;
    if (!url || !response || capacity < 128u || !out_size)
        return -1;
    if (!__atomic_compare_exchange_n(&g_fetch_busy, &expected, 1u, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return -2;

    g_fetch_output = response;
    g_fetch_capacity = capacity;
    g_fetch_size = 0u;
    g_fetch_truncated = 0u;
    g_fetch_cancelled = cancelled;
    g_fetch_deadline = knet_now_ms() + (uint64_t)(timeout_ms ? timeout_ms : 45000u);
    usbh_background_network_set(1u);
    terminal_print("net: request begin");
    terminal_flush_log();
    rc = knet_usb_request_url(url, capacity, method, body, body_size, content_type);
    was_cancelled = cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE);
    timed_out = !was_cancelled && g_fetch_deadline && knet_now_ms() >= g_fetch_deadline;
    if (rc != 0 || was_cancelled || timed_out)
    {
        usb_ethernet_status eth_status;
        usb_ethernet_get_status(&eth_status);
        terminal_print_inline("net: Ethernet failure diagnostics: ");
        terminal_print(eth_status.detail);
        memset(g_dns_cache, 0, sizeof(g_dns_cache));
        g_dns_cache_next = 0u;
        usb_ethernet_discard_pending_frames();
    }
    *out_size = g_fetch_size;
    if (out_truncated)
        *out_truncated = g_fetch_truncated;
    g_fetch_output = 0;
    g_fetch_capacity = 0u;
    g_fetch_size = 0u;
    g_fetch_cancelled = 0;
    g_fetch_deadline = 0u;
    usbh_background_network_set(0u);
    __atomic_store_n(&g_fetch_busy, 0u, __ATOMIC_RELEASE);
    terminal_flush_log();
    return was_cancelled ? -3 : (timed_out ? -4 : rc);
}

int knet_usb_fetch_url(const char *url, uint8_t *response, uint32_t capacity,
                       uint32_t *out_size, uint8_t *out_truncated,
                       volatile uint32_t *cancelled, uint32_t timeout_ms)
{
    return knet_usb_fetch_request(url, "GET", 0, 0u, 0, response, capacity,
                                  out_size, out_truncated, cancelled, timeout_ms);
}
