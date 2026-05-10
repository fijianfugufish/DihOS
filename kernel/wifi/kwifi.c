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
    uint8_t eapol_start_retries;
    uint32_t mgmt_tx_seen;
    char ssid[KWIFI_CONNECT_SSID_MAX];
    char username[KWIFI_CONNECT_USER_MAX];
    char password[KWIFI_CONNECT_PASS_MAX];
    char auth_mode[24];
    char eap_phase_text[32];
    char peap_phase_text[40];
    char status[96];
} kwifi_connect_state;

static kwifi_connect_state g_kwifi_connect;

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

static void kwifi_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFu);
    p[1] = (uint8_t)(value & 0xFFu);
}

static void kwifi_write_eapol_eth_hdr(uint8_t *out)
{
    /* 802.1X PAE group address */
    out[0] = 0x01u;
    out[1] = 0x80u;
    out[2] = 0xC2u;
    out[3] = 0x00u;
    out[4] = 0x00u;
    out[5] = 0x03u;

    /* Matches current vdev MAC in pci_kernel_nic_probe.c */
    out[6] = 0x02u;
    out[7] = 0x11u;
    out[8] = 0x22u;
    out[9] = 0x33u;
    out[10] = 0x44u;
    out[11] = 0x55u;

    /* Ethertype: EAPOL */
    out[12] = 0x88u;
    out[13] = 0x8Eu;
}

static uint32_t kwifi_build_eapol_start(uint8_t *out, uint32_t cap)
{
    if (!out || cap < 18u)
        return 0u;

    kwifi_write_eapol_eth_hdr(out);

    /* EAPOL: version 2, type Start(1), body length 0 */
    out[14] = 2u;
    out[15] = 1u;
    out[16] = 0u;
    out[17] = 0u;
    return 18u;
}

static uint32_t kwifi_build_eap_response_identity(const char *identity, uint8_t identifier,
                                                  uint8_t *out, uint32_t cap)
{
    uint32_t id_len = kwifi_cstr_len_cap(identity, 240u);
    uint16_t eap_len = (uint16_t)(5u + id_len);
    uint16_t eapol_len = eap_len;

    if (!out || cap < (uint32_t)(18u + eap_len))
        return 0u;

    kwifi_write_eapol_eth_hdr(out);

    /* EAPOL header */
    out[14] = 2u;
    out[15] = 0u; /* EAP-Packet */
    kwifi_write_be16(out + 16u, eapol_len);

    /* EAP packet: Response(2), Identifier, Length, Type Identity(1), identity bytes */
    out[18u] = 2u;
    out[19u] = identifier;
    kwifi_write_be16(out + 20u, eap_len);
    out[22u] = 1u;
    for (uint32_t i = 0u; i < id_len; ++i)
        out[23u + i] = (uint8_t)identity[i];

    return 18u + (uint32_t)eap_len;
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

int kwifi_connect_request(const char *ssid, const char *username, const char *password)
{
    uint32_t ssid_len;
    uint32_t user_len;
    uint32_t pass_len;
    uint32_t found;

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
    g_kwifi_connect.eapol_start_retries = 0u;
    g_kwifi_connect.mgmt_tx_seen = 0u;
    kwifi_copy_trunc(g_kwifi_connect.ssid, sizeof(g_kwifi_connect.ssid), ssid);
    kwifi_copy_trunc(g_kwifi_connect.username, sizeof(g_kwifi_connect.username), username ? username : "");
    kwifi_copy_trunc(g_kwifi_connect.password, sizeof(g_kwifi_connect.password), password);
    kwifi_copy_trunc(g_kwifi_connect.auth_mode, sizeof(g_kwifi_connect.auth_mode),
                     g_kwifi_connect.enterprise ? "wpa2-enterprise" : "non-enterprise");
    kwifi_set_eap_phase(0u, "none");
    kwifi_set_peap_phase(0u, "none");

    if (!found)
    {
        kwifi_set_status("requested; target SSID not in latest scan yet");
        terminal_print("[K:WIFI] connect request saved; SSID not visible yet");
        return 1;
    }

    if (username && username[0])
    {
        uint32_t tx_count = 0u;
        uint32_t tx_desc = 0u;
        uint32_t tx_status = 0u;

        if (!pci_kernel_wifi_connect_ssid(ssid))
        {
            kwifi_set_status("enterprise connect failed before association");
            terminal_print("[K:WIFI] enterprise connect attempt failed before association");
            return 0;
        }

        if (pci_kernel_wifi_mgmt_tx_status(&tx_count, &tx_desc, &tx_status))
            g_kwifi_connect.mgmt_tx_seen = tx_count;
        kwifi_set_eap_phase(1u, "identity-ready");
        kwifi_set_peap_phase(1u, "profile-loaded");
        kwifi_set_status("enterprise connect staged; association sent, EAPOL pending");
        terminal_print("[K:WIFI] enterprise connect staged; association + EAP state initialized");
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

int kwifi_poll_connection(uint32_t rounds)
{
    uint8_t eapol[320];
    uint32_t tx_len;
    uint32_t tx_count = 0u;
    uint32_t tx_desc = 0u;
    uint32_t tx_status = 0u;
    (void)rounds;
    (void)tx_desc;

    if (!g_kwifi_connect.requested || !g_kwifi_connect.enterprise)
        return 0;

    /*
     * Stage 1: enterprise connect state machine scaffold.
     * Real EAPOL tx/rx and PEAP handshake are next-stage work.
     */
    if (g_kwifi_connect.eap_phase == 1u)
    {
        tx_len = kwifi_build_eapol_start(eapol, sizeof(eapol));
        if (!tx_len || !pci_kernel_wifi_tx_l2_frame(eapol, tx_len))
        {
            kwifi_set_eap_phase(2u, "eapol-start-failed");
            kwifi_set_peap_phase(2u, "eapol-start");
            kwifi_set_status("EAPOL-Start send failed: data-plane TX unavailable");
            return 0;
        }

        kwifi_set_eap_phase(2u, "eapol-start-pending");
        kwifi_set_peap_phase(2u, "eapol-start");
        kwifi_set_status("EAPOL-Start queued; waiting for tx completion");
        return 1;
    }

    if (g_kwifi_connect.eap_phase == 2u)
    {
        if (!pci_kernel_wifi_mgmt_tx_status(&tx_count, &tx_desc, &tx_status) ||
            tx_count == g_kwifi_connect.mgmt_tx_seen)
        {
            kwifi_set_status("waiting for EAPOL-Start tx completion");
            return 0;
        }

        g_kwifi_connect.mgmt_tx_seen = tx_count;
        if (tx_status != 0u)
        {
            if (g_kwifi_connect.eapol_start_retries == 0u)
            {
                g_kwifi_connect.eapol_start_retries = 1u;
                kwifi_set_eap_phase(1u, "eapol-start-retry");
                kwifi_set_status("EAPOL-Start tx error; retrying with alternate tx path");
                return 1;
            }
            kwifi_set_eap_phase(3u, "eapol-start-tx-error");
            kwifi_set_peap_phase(3u, "peap-tls-start");
            kwifi_set_status("EAPOL-Start tx completion returned error");
            return 0;
        }

        tx_len = kwifi_build_eap_response_identity(g_kwifi_connect.username, 1u, eapol, sizeof(eapol));
        if (!tx_len || !pci_kernel_wifi_tx_l2_frame(eapol, tx_len))
        {
            kwifi_set_eap_phase(3u, "identity-send-failed");
            kwifi_set_peap_phase(3u, "peap-tls-start");
            kwifi_set_status("EAP identity send failed: data-plane TX unavailable");
            return 0;
        }

        kwifi_set_eap_phase(3u, "identity-sent-pending");
        kwifi_set_peap_phase(3u, "peap-tls-start");
        kwifi_set_status("EAP identity sent; waiting for tx completion");
        return 1;
    }

    if (g_kwifi_connect.eap_phase == 3u && g_kwifi_connect.peap_phase == 3u)
    {
        if (!pci_kernel_wifi_mgmt_tx_status(&tx_count, &tx_desc, &tx_status) ||
            tx_count == g_kwifi_connect.mgmt_tx_seen)
        {
            kwifi_set_status("waiting for EAP identity tx completion");
            return 0;
        }

        g_kwifi_connect.mgmt_tx_seen = tx_count;
        if (tx_status != 0u)
        {
            kwifi_set_peap_phase(4u, "identity-tx-error");
            kwifi_set_status("EAP identity tx completion returned error");
            return 0;
        }

        kwifi_set_peap_phase(4u, "tls-tunnel-pending");
        kwifi_set_status("PEAP phase1 pending: TLS tunnel engine missing");
        return 1;
    }

    if (g_kwifi_connect.eap_phase == 3u && g_kwifi_connect.peap_phase == 4u)
    {
        kwifi_set_peap_phase(5u, "inner-mschapv2-pending");
        kwifi_set_status("PEAP phase2 pending: MSCHAPv2 engine missing");
        return 1;
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
