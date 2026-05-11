#include "wifi/kwifi.h"
#include "asm/asm.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"
#include "pci/pci_kernel_nic_probe.h"
#include "terminal/terminal_api.h"

#include <stdint.h>

#define KWIFI_CONNECT_SSID_MAX 64u
#define KWIFI_CONNECT_USER_MAX 96u
#define KWIFI_CONNECT_PASS_MAX 128u

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
    uint8_t beacon_miss_count;
    uint8_t reconnect_attempts;
    char ssid[KWIFI_CONNECT_SSID_MAX];
    char username[KWIFI_CONNECT_USER_MAX];
    char password[KWIFI_CONNECT_PASS_MAX];
    char auth_mode[24];
    char eap_phase_text[32];
    char peap_phase_text[40];
    char status[96];
} kwifi_connect_state;

static kwifi_connect_state g_kwifi_connect;
static const uint32_t g_kwifi_enterprise_fallback_channels_mhz[] = {
    5200u, 5180u, 5220u, 5240u, 5745u, 5680u};

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

static int kwifi_send_eapol_start(void)
{
    uint8_t frame[64];
    uint8_t src[6];
    uint32_t tx_count_before = 0u, tx_last_desc = 0u, tx_last_status = 0u;
    uint32_t tx_count_after = 0u, tx_last_desc_after = 0u, tx_last_status_after = 0u;
    uint32_t tx_status_for_log = 0xFFFFFFFFu;
    uint8_t sent = 0u;
    uint8_t completion_seen = 0u;
    static const uint8_t eapol_pae_group[6] = {0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x03u};

    if (g_kwifi_connect.eapol_start_sent)
        return 1;

    if (!pci_kernel_wifi_get_local_mac(src))
        return 0;

    for (uint32_t i = 0u; i < sizeof(frame); ++i)
        frame[i] = 0u;
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        frame[i] = eapol_pae_group[i];
        frame[6u + i] = src[i];
    }
    frame[12] = 0x88u;
    frame[13] = 0x8Eu;
    frame[14] = 0x01u; /* EAPOL v1 */
    frame[15] = 0x01u; /* EAPOL-Start */
    frame[16] = 0x00u; /* body len hi */
    frame[17] = 0x00u; /* body len lo */

    (void)pci_kernel_wifi_mgmt_tx_status(&tx_count_before, &tx_last_desc, &tx_last_status);
    if (pci_kernel_wifi_tx_l2_frame_mode(frame, 60u, 0u))
        sent = 1u;
    (void)pci_kernel_wifi_mgmt_tx_status(&tx_count_after, &tx_last_desc_after, &tx_last_status_after);
    completion_seen = (tx_count_after != tx_count_before) ? 1u : 0u;

    if (!completion_seen)
    {
        terminal_print("[K:WIFI] EAPOL-Start mgmt-tx completion not observed; retrying offchan path");
        if (pci_kernel_wifi_tx_l2_frame_mode(frame, 60u, 1u))
            sent = 1u;
        (void)pci_kernel_wifi_mgmt_tx_status(&tx_count_after, &tx_last_desc_after, &tx_last_status_after);
        completion_seen = (tx_count_after != tx_count_before) ? 1u : 0u;
    }

    if (!sent)
        return 0;

    if (completion_seen)
        tx_status_for_log = tx_last_status_after;

    g_kwifi_connect.eapol_start_sent = 1u;
    terminal_print("[K:WIFI] enterprise EAPOL-Start sent");
    terminal_print(" tx_count=");
    terminal_print_inline_hex64(tx_count_after);
    terminal_print(" tx_done=");
    terminal_print_inline(completion_seen ? "yes" : "no");
    terminal_print(" tx_status=");
    terminal_print_inline_hex64(tx_status_for_log);
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
    g_kwifi_connect.beacon_miss_count = 0u;
    g_kwifi_connect.reconnect_attempts = 0u;
    kwifi_copy_trunc(g_kwifi_connect.ssid, sizeof(g_kwifi_connect.ssid), ssid);
    kwifi_copy_trunc(g_kwifi_connect.username, sizeof(g_kwifi_connect.username), username ? username : "");
    kwifi_copy_trunc(g_kwifi_connect.password, sizeof(g_kwifi_connect.password), password);
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
         * - userspace/host supplicant runs PEAP/EAPOL and authorizes port
         */
        kwifi_set_eap_phase(1u, "assoc-staged");
        kwifi_set_peap_phase(1u, "supplicant-pending");
        kwifi_set_status("associated; controlled port unauthorized, waiting for PEAP supplicant");
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
    uint32_t auth_sent;

    if (!g_kwifi_connect.requested || !g_kwifi_connect.enterprise)
        return 0;

    g_kwifi_connect.supplicant_ready = ready ? 1u : 0u;
    if (!ready)
    {
        (void)pci_kernel_wifi_set_peer_authorize(0u);
        g_kwifi_connect.control_port_authorized = 0u;
        g_kwifi_connect.connected = 0u;
        g_kwifi_connect.eapol_start_sent = 0u;
        kwifi_set_eap_phase(2u, "controlled-port-unauthorized");
        kwifi_set_peap_phase(2u, "peap-supplicant-required");
        kwifi_set_status("supplicant marked not-ready; controlled port unauthorized");
        return 1;
    }

    auth_sent = pci_kernel_wifi_set_peer_authorize(1u) ? 1u : 0u;
    if (!auth_sent)
    {
        g_kwifi_connect.control_port_authorized = 0u;
        g_kwifi_connect.connected = 0u;
        kwifi_set_eap_phase(2u, "controlled-port-unauthorized");
        kwifi_set_peap_phase(2u, "peap-supplicant-required");
        kwifi_set_status("supplicant ready; waiting for firmware WMI credit to authorize controlled port");
        return 1;
    }

    g_kwifi_connect.control_port_authorized = 1u;
    key_done = pci_kernel_wifi_install_key_done();
    assoc_done = pci_kernel_wifi_peer_assoc_done();
    roam_reason = pci_kernel_wifi_roam_reason();
    g_kwifi_connect.connected = key_done ? 1u : 0u;
    (void)kwifi_send_eapol_start();
    kwifi_set_eap_phase(3u, "supplicant-running");
    kwifi_set_peap_phase(3u, "peap-running");
    if (key_done)
        kwifi_set_status("supplicant marked ready; firmware key install already complete");
    else if (assoc_done && roam_reason != 2u)
        kwifi_set_status("supplicant marked ready; peer assoc is stable, awaiting enterprise key install");
    else
        kwifi_set_status("supplicant marked ready; waiting for real enterprise key-install confirmation");
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
     * - supplicant handles PEAP/EAPOL state machine and eventually authorizes
     *   the port.
     */
    if (g_kwifi_connect.eap_phase == 1u)
    {
        g_kwifi_connect.control_port_authorized = 0u;
        kwifi_set_eap_phase(2u, "controlled-port-unauthorized");
        kwifi_set_peap_phase(2u, "peap-supplicant-required");
        kwifi_set_status(assoc_done
                             ? "peer assoc confirmed; waiting for PEAP/EAPOL controlled-port authorization"
                             : "waiting for firmware peer-assoc confirmation");
        return 1;
    }

    if (g_kwifi_connect.eap_phase == 2u)
    {
        if (!g_kwifi_connect.supplicant_ready)
        {
            if (roam_reason == 2u)
                kwifi_set_status("peer assoc confirmed but firmware reports beacon-miss; link not stable yet");
            else
                kwifi_set_status(assoc_done
                                     ? "peer assoc confirmed; waiting for PEAP supplicant readiness"
                                     : "association pending; waiting for firmware peer-assoc confirmation");
            return 0;
        }

        if (!pci_kernel_wifi_set_peer_authorize(1u))
        {
            g_kwifi_connect.control_port_authorized = 0u;
            g_kwifi_connect.connected = 0u;
            kwifi_set_status("supplicant ready; waiting for firmware WMI credit to authorize controlled port");
            return 0;
        }

        g_kwifi_connect.control_port_authorized = 1u;
        g_kwifi_connect.connected = key_done ? 1u : 0u;
        (void)kwifi_send_eapol_start();
        kwifi_set_eap_phase(3u, "supplicant-running");
        kwifi_set_peap_phase(3u, "peap-running");
        if (key_done)
            kwifi_set_status("PEAP/EAPOL path reports firmware key install complete");
        else if (assoc_done && roam_reason != 2u)
            kwifi_set_status("PEAP/EAPOL path reports stable peer assoc; enterprise key install still pending");
        else if (roam_reason == 2u)
            kwifi_set_status("supplicant running, but firmware reports beacon-miss; waiting for stable link/auth completion");
        else
            kwifi_set_status("PEAP/EAPOL host path running; waiting for firmware key-install completion");
        return 1;
    }

    if (g_kwifi_connect.eap_phase >= 3u)
    {
        if (key_done)
        {
            g_kwifi_connect.connected = 1u;
            kwifi_set_status("enterprise key install complete; link authorized");
            return 1;
        }
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
