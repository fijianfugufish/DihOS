#include "usb/usb_ethernet.h"
#include "usb/usbh.h"
#include "terminal/terminal_api.h"
#include "kwrappers/string.h"

static usbh_dev_t g_usb_eth_dev;
static uint8_t g_usb_eth_online;
static uint8_t g_usb_eth_ax_ready;
static uint8_t g_usb_eth_ax_link_up;
static uint16_t g_usb_eth_ax_physr;
static uint8_t g_usb_eth_ax_link_sts;
static uint32_t g_usb_eth_last_tx_len;
static int g_usb_eth_last_tx_rc;
static uint32_t g_usb_eth_last_tx_got;
static uint32_t g_usb_eth_last_tx_fifo_before;
static uint32_t g_usb_eth_last_tx_fifo_after;
static uint32_t g_usb_eth_last_xhci_cc;
static uint32_t g_usb_eth_last_xhci_ep;
static uint32_t g_usb_eth_last_xhci_rem;
static uint32_t g_usb_eth_last_rx_got;
static uint32_t g_usb_eth_last_rx_hdr;
static uint32_t g_usb_eth_last_rx_reason;
static uint32_t g_usb_eth_last_rx_xhci_cc;
static uint32_t g_usb_eth_last_rx_xhci_ep;
static uint32_t g_usb_eth_last_rx_xhci_rem;
static uint8_t *g_usb_eth_tx_dma;
static uint8_t *g_usb_eth_rx_dma;
static uint32_t g_usb_eth_dma_cap;

#define USB_ETH_PENDING_COUNT 16u
#define USB_ETH_PENDING_CAP 2048u
static uint8_t g_usb_eth_pending[USB_ETH_PENDING_COUNT][USB_ETH_PENDING_CAP];
static uint16_t g_usb_eth_pending_len[USB_ETH_PENDING_COUNT];
static uint8_t g_usb_eth_pending_head;
static uint8_t g_usb_eth_pending_used;

#define USB_ETH_DMA_CAP 32768u

#define AX_ACCESS_MAC 0x01u
#define AX_ACCESS_PHY 0x02u
#define AX88179_PHY_ID 0x03u
#define PHYSICAL_LINK_STATUS 0x02u
#define AX_USB_HS 0x02u
#define AX_USB_SS 0x04u
#define AX_PHYPWR_RSTCTL 0x26u
#define AX_PHYPWR_RSTCTL_BZ 0x0010u
#define AX_PHYPWR_RSTCTL_IPRL 0x0020u
#define AX_CLK_SELECT 0x33u
#define AX_CLK_SELECT_BCS 0x01u
#define AX_CLK_SELECT_ACS 0x02u
#define AX_RX_BULKIN_QCTRL 0x2Eu
#define AX_PAUSE_WATERLVL_HIGH 0x54u
#define AX_PAUSE_WATERLVL_LOW 0x55u
#define AX_RXCOE_CTL 0x34u
#define AX_TXCOE_CTL 0x35u
#define AX_RXCOE_IP 0x01u
#define AX_RXCOE_TCP 0x02u
#define AX_RXCOE_UDP 0x04u
#define AX_RXCOE_TCPV6 0x20u
#define AX_RXCOE_UDPV6 0x40u
#define AX_TXCOE_IP 0x01u
#define AX_TXCOE_TCP 0x02u
#define AX_TXCOE_UDP 0x04u
#define AX_TXCOE_TCPV6 0x20u
#define AX_TXCOE_UDPV6 0x40u
#define AX_RX_CTL 0x0Bu
#define AX_RX_CTL_DROPCRCERR 0x0100u
#define AX_RX_CTL_IPE 0x0200u
#define AX_RX_CTL_START 0x0080u
#define AX_RX_CTL_AP 0x0020u
#define AX_RX_CTL_AMALL 0x0002u
#define AX_RX_CTL_AB 0x0008u
#define AX_MONITOR_MOD 0x24u
#define AX_MONITOR_MODE_RWMP 0x04u
#define AX_MONITOR_MODE_PMEPOL 0x20u
#define AX_MONITOR_MODE_PMETYPE 0x40u
#define AX_MEDIUM_STATUS_MODE 0x22u
#define AX_MEDIUM_GIGAMODE 0x01u
#define AX_MEDIUM_FULL_DUPLEX 0x02u
#define AX_MEDIUM_EN_125MHZ 0x08u
#define AX_MEDIUM_RXFLOW_CTRLEN 0x10u
#define AX_MEDIUM_TXFLOW_CTRLEN 0x20u
#define AX_MEDIUM_RECEIVE_EN 0x0100u
#define AX_MEDIUM_PS 0x0200u
#define GMII_PHY_PHYSR 0x11u
#define GMII_PHY_PAGE_SELECT 0x1Fu
#define GMII_PHY_PAGE_SELECT_PAGE0 0x0000u
#define GMII_PHY_PHYSR_SMASK 0xC000u
#define GMII_PHY_PHYSR_GIGA 0x8000u
#define GMII_PHY_PHYSR_100 0x4000u
#define GMII_PHY_PHYSR_FULL 0x2000u
#define GMII_PHY_PHYSR_LINK 0x0400u
#define AX_RXHDR_CRC_ERR 0x20000000u
#define AX_RXHDR_DROP_ERR 0x80000000u
#define AX_TX_FIFO_FULL_STATUS_CMD 0x81u
#define AX_TX_FIFO_FULL_STATUS_REG 0x8Cu

#define USB_ETH_RX_OK 0u
#define USB_ETH_RX_NO_USB 1u
#define USB_ETH_RX_NOT_READY 2u
#define USB_ETH_RX_SHORT 3u
#define USB_ETH_RX_BAD_TRAILER 4u
#define USB_ETH_RX_OVERLAP 5u
#define USB_ETH_RX_BAD_PACKET 6u
#define USB_ETH_RX_TOO_BIG 7u
#define USB_ETH_RX_NO_VALID 8u

extern volatile uint32_t g_xhci_last_cc;
extern volatile uint32_t g_xhci_last_ev_epid;
extern volatile uint32_t g_xhci_last_ev_len;

static void eth_copy(char *dst, uint32_t cap, const char *src)
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

static void eth_append(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0u;
    if (!dst || !cap || !src)
        return;
    while (dst[len] && len + 1u < cap)
        ++len;
    while (*src && len + 1u < cap)
        dst[len++] = *src++;
    dst[len] = 0;
}

static void eth_append_hex8(char *dst, uint32_t cap, uint8_t value)
{
    static const char *hex = "0123456789ABCDEF";
    char tmp[3];
    tmp[0] = hex[(value >> 4) & 0xFu];
    tmp[1] = hex[value & 0xFu];
    tmp[2] = 0;
    eth_append(dst, cap, tmp);
}

static void eth_append_hex16(char *dst, uint32_t cap, uint16_t value)
{
    eth_append_hex8(dst, cap, (uint8_t)(value >> 8));
    eth_append_hex8(dst, cap, (uint8_t)value);
}

static void eth_delay_ms(uint32_t ms)
{
    volatile uint32_t sink = 0u;
    while (ms--)
    {
        for (uint32_t i = 0u; i < 220000u; ++i)
            sink += i;
    }
    (void)sink;
}

static void eth_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void eth_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t eth_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int ax_write(uint8_t cmd, uint16_t value, uint16_t index, const void *data, uint16_t len)
{
    return usbh_control_xfer(&g_usb_eth_dev, 0x40u, cmd, value, index, (void *)data, len);
}

static int ax_read(uint8_t cmd, uint16_t value, uint16_t index, void *data, uint16_t len)
{
    return usbh_control_xfer(&g_usb_eth_dev, 0xC0u, cmd, value, index, data, len);
}

static int ax_write_u8(uint16_t reg, uint8_t value)
{
    uint8_t tmp = value;
    return ax_write(AX_ACCESS_MAC, reg, 1u, &tmp, 1u);
}

static int ax_write_u16(uint16_t reg, uint16_t value)
{
    uint8_t tmp[2];
    eth_put_le16(tmp, value);
    return ax_write(AX_ACCESS_MAC, reg, 2u, tmp, 2u);
}

static int ax_read_u8(uint16_t reg, uint8_t *out)
{
    uint8_t tmp = 0u;
    if (!out || ax_read(AX_ACCESS_MAC, reg, 1u, &tmp, 1u) != 0)
        return -1;
    *out = tmp;
    return 0;
}

static int ax_phy_read_u16(uint16_t reg, uint16_t *out)
{
    uint8_t tmp[2] = {0u, 0u};
    if (!out || ax_read(AX_ACCESS_PHY, AX88179_PHY_ID, reg, tmp, 2u) != 0)
        return -1;
    *out = (uint16_t)tmp[0] | ((uint16_t)tmp[1] << 8);
    return 0;
}

static int ax_phy_write_u16(uint16_t reg, uint16_t value)
{
    uint8_t tmp[2];
    eth_put_le16(tmp, value);
    return ax_write(AX_ACCESS_PHY, AX88179_PHY_ID, reg, tmp, 2u);
}

static int ax_read_tx_fifo_status(uint32_t *out)
{
    uint8_t tmp[4] = {0u, 0u, 0u, 0u};
    if (!out || ax_read(AX_TX_FIFO_FULL_STATUS_CMD, AX_TX_FIFO_FULL_STATUS_REG, 0u, tmp, 4u) != 0)
        return -1;
    *out = eth_get_le32(tmp);
    return 0;
}

static int ax88179_link_reset(void)
{
    static const uint8_t bulkin_ss[5] = {7u, 0x4Fu, 0u, 0x12u, 0xFFu};
    static const uint8_t bulkin_hs_giga[5] = {7u, 0x20u, 3u, 0x16u, 0xFFu};
    static const uint8_t bulkin_hs_100[5] = {7u, 0xAEu, 7u, 0x18u, 0xFFu};
    static const uint8_t bulkin_fs[5] = {7u, 0xCCu, 0x4Cu, 0x18u, 8u};
    const uint8_t *bulk = bulkin_fs;
    uint16_t rxctl = AX_RX_CTL_DROPCRCERR | AX_RX_CTL_IPE | AX_RX_CTL_START |
                     AX_RX_CTL_AP | AX_RX_CTL_AMALL | AX_RX_CTL_AB;
    uint16_t medium = AX_MEDIUM_RECEIVE_EN | AX_MEDIUM_TXFLOW_CTRLEN | AX_MEDIUM_RXFLOW_CTRLEN;

    g_usb_eth_ax_link_up = 0u;
    g_usb_eth_ax_physr = 0u;
    g_usb_eth_ax_link_sts = 0u;

    (void)ax_write_u16(AX_RX_CTL, 0u);
    (void)ax_write_u16(AX_RX_CTL, rxctl);

    for (uint32_t attempt = 0u; attempt < 12u; ++attempt)
    {
        (void)ax_phy_write_u16(GMII_PHY_PAGE_SELECT, GMII_PHY_PAGE_SELECT_PAGE0);
        if (ax_read_u8(PHYSICAL_LINK_STATUS, &g_usb_eth_ax_link_sts) != 0)
            terminal_warn("usbnet: AX88179 physical link status read failed");
        if (ax_phy_read_u16(GMII_PHY_PHYSR, &g_usb_eth_ax_physr) != 0)
            terminal_warn("usbnet: AX88179 PHY status read failed");
        if (g_usb_eth_ax_physr & GMII_PHY_PHYSR_LINK)
            break;
        eth_delay_ms(250u);
    }

    terminal_print_inline("usbnet: AX88179 link physr=");
    terminal_print_inline_hex32(g_usb_eth_ax_physr);
    terminal_print_inline(" usb=");
    terminal_print_inline_hex8(g_usb_eth_ax_link_sts);
    terminal_print("");

    if ((g_usb_eth_ax_physr & GMII_PHY_PHYSR_LINK) == 0u)
    {
        terminal_warn("usbnet: AX88179 cable link is down");
        return -1;
    }

    if ((g_usb_eth_ax_physr & GMII_PHY_PHYSR_SMASK) == GMII_PHY_PHYSR_GIGA)
    {
        medium |= AX_MEDIUM_GIGAMODE | AX_MEDIUM_EN_125MHZ;
        bulk = (g_usb_eth_ax_link_sts & AX_USB_SS) ? bulkin_ss :
               (g_usb_eth_ax_link_sts & AX_USB_HS) ? bulkin_hs_giga : bulkin_fs;
    }
    else if ((g_usb_eth_ax_physr & GMII_PHY_PHYSR_SMASK) == GMII_PHY_PHYSR_100)
    {
        medium |= AX_MEDIUM_PS;
        bulk = (g_usb_eth_ax_link_sts & (AX_USB_SS | AX_USB_HS)) ? bulkin_hs_100 : bulkin_fs;
    }

    if (g_usb_eth_ax_physr & GMII_PHY_PHYSR_FULL)
        medium |= AX_MEDIUM_FULL_DUPLEX;

    if (ax_write(AX_ACCESS_MAC, AX_RX_BULKIN_QCTRL, 5u, bulk, 5u) != 0)
        return -1;
    if (ax_write_u16(AX_MEDIUM_STATUS_MODE, medium) != 0)
        return -1;

    g_usb_eth_ax_link_up = 1u;
    terminal_success("usbnet: AX88179 cable link is up");
    return 0;
}

static int ax88179_basic_init(void)
{
    static const uint8_t bulkin_ss[5] = {7u, 0x4Fu, 0u, 0x12u, 0xFFu};
    uint8_t coe;
    uint16_t rxctl;
    uint16_t medium;

    if (g_usb_eth_dev.net_driver != USB_NET_DRIVER_ASIX_AX88179)
        return 0;

    g_usb_eth_ax_ready = 0u;
    g_usb_eth_ax_link_up = 0u;
    terminal_print("usbnet: AX88179 init");

    if (ax_write_u16(AX_PHYPWR_RSTCTL, 0u) != 0)
        return -1;
    if (ax_write_u16(AX_PHYPWR_RSTCTL, AX_PHYPWR_RSTCTL_IPRL) != 0)
        return -1;
    eth_delay_ms(250u);
    if (ax_write_u16(AX_PHYPWR_RSTCTL, AX_PHYPWR_RSTCTL_IPRL | AX_PHYPWR_RSTCTL_BZ) != 0)
        return -1;
    eth_delay_ms(500u);

    if (ax_write_u8(AX_CLK_SELECT, AX_CLK_SELECT_ACS | AX_CLK_SELECT_BCS) != 0)
        return -1;
    eth_delay_ms(200u);

    if (g_usb_eth_dev.net_mac_valid)
        (void)ax_write(AX_ACCESS_MAC, 0x10u, 6u, g_usb_eth_dev.net_mac, 6u);

    if (ax_write(AX_ACCESS_MAC, AX_RX_BULKIN_QCTRL, 5u, bulkin_ss, 5u) != 0)
        return -1;
    if (ax_write_u8(AX_PAUSE_WATERLVL_LOW, 0x34u) != 0)
        return -1;
    if (ax_write_u8(AX_PAUSE_WATERLVL_HIGH, 0x52u) != 0)
        return -1;

    coe = 0u;
    if (ax_write_u8(AX_RXCOE_CTL, coe) != 0)
        return -1;
    coe = 0u;
    if (ax_write_u8(AX_TXCOE_CTL, coe) != 0)
        return -1;

    rxctl = AX_RX_CTL_DROPCRCERR | AX_RX_CTL_IPE | AX_RX_CTL_START |
            AX_RX_CTL_AP | AX_RX_CTL_AMALL | AX_RX_CTL_AB;
    if (ax_write_u16(AX_RX_CTL, rxctl) != 0)
        return -1;

    if (ax_write_u8(AX_MONITOR_MOD, AX_MONITOR_MODE_PMETYPE | AX_MONITOR_MODE_PMEPOL | AX_MONITOR_MODE_RWMP) != 0)
        return -1;

    medium = AX_MEDIUM_RECEIVE_EN | AX_MEDIUM_TXFLOW_CTRLEN |
             AX_MEDIUM_RXFLOW_CTRLEN | AX_MEDIUM_FULL_DUPLEX | AX_MEDIUM_GIGAMODE;
    if (ax_write_u16(AX_MEDIUM_STATUS_MODE, medium) != 0)
        return -1;

    (void)ax88179_link_reset();

    g_usb_eth_ax_ready = 1u;
    terminal_success("usbnet: AX88179 RX/TX enabled");
    return 0;
}

static void eth_append_u32(char *dst, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t n = 0u;
    if (!value)
    {
        eth_append(dst, cap, "0");
        return;
    }
    while (value && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n)
    {
        char c[2];
        c[0] = tmp[--n];
        c[1] = 0;
        eth_append(dst, cap, c);
    }
}

static const char *eth_driver_name(const usbh_dev_t *dev)
{
    if (!dev)
        return "USB Ethernet";
    if (dev->net_driver == USB_NET_DRIVER_ASIX_AX88179)
        return "ASIX AX88179";
    if (dev->net_subclass == 0x06u)
        return "CDC ECM";
    if (dev->net_subclass == 0x0Du)
        return "CDC NCM";
    return "CDC Ethernet";
}

static int eth_dma_ready(void)
{
    if (g_usb_eth_tx_dma && g_usb_eth_rx_dma && g_usb_eth_dma_cap >= USB_ETH_DMA_CAP)
        return 0;
    g_usb_eth_tx_dma = (uint8_t *)usbh_alloc_dma(USB_ETH_DMA_CAP);
    g_usb_eth_rx_dma = (uint8_t *)usbh_alloc_dma(USB_ETH_DMA_CAP);
    if (!g_usb_eth_tx_dma || !g_usb_eth_rx_dma)
        return -1;
    g_usb_eth_dma_cap = USB_ETH_DMA_CAP;
    return 0;
}

int usb_ethernet_probe_multi(const uint64_t *xhci_mmio_hints,
                             uint32_t hint_count,
                             uint64_t acpi_rsdp_hint)
{
    if (g_usb_eth_online)
        return 0;
    g_usb_eth_ax_ready = 0u;

    if (usbh_init_any(xhci_mmio_hints, hint_count, acpi_rsdp_hint) != 0)
    {
        terminal_warn("usbnet: xhci init failed");
        terminal_flush_log();
        return -1;
    }

    if (usbh_enumerate_first_cdc_ethernet(&g_usb_eth_dev) != 0)
    {
        terminal_warn("usbnet: no CDC/ASIX Ethernet adapter found");
        terminal_flush_log();
        return -1;
    }

    if (eth_dma_ready() != 0)
    {
        terminal_warn("usbnet: DMA buffers unavailable");
        terminal_flush_log();
        return -1;
    }

    if (ax88179_basic_init() != 0)
    {
        terminal_warn("usbnet: AX88179 init failed");
        terminal_flush_log();
        return -1;
    }

    g_usb_eth_online = 1u;
    terminal_success("usbnet: USB Ethernet adapter online");
    terminal_flush_log();
    return 0;
}

uint32_t usb_ethernet_online(void)
{
    return g_usb_eth_online ? 1u : 0u;
}

int usb_ethernet_get_mac(uint8_t out_mac[6])
{
    if (!out_mac || !g_usb_eth_online || !g_usb_eth_dev.net_mac_valid)
        return -1;
    for (uint32_t i = 0u; i < 6u; ++i)
        out_mac[i] = g_usb_eth_dev.net_mac[i];
    return 0;
}

void usb_ethernet_get_status(usb_ethernet_status *out_status)
{
    if (!out_status)
        return;

    memset(out_status, 0, sizeof(*out_status));
    out_status->online = g_usb_eth_online;
    if (!g_usb_eth_online)
    {
        eth_copy(out_status->driver, sizeof(out_status->driver), "USB Ethernet");
        eth_copy(out_status->detail, sizeof(out_status->detail), "no CDC/ASIX adapter discovered");
        return;
    }

    out_status->port_id = g_usb_eth_dev.port_id;
    out_status->subclass = g_usb_eth_dev.net_subclass;
    out_status->protocol = g_usb_eth_dev.net_protocol;
    out_status->mtu = g_usb_eth_dev.net_mtu ? g_usb_eth_dev.net_mtu : 1500u;
    out_status->mac_valid = g_usb_eth_dev.net_mac_valid;
    for (uint32_t i = 0u; i < 6u; ++i)
        out_status->mac[i] = g_usb_eth_dev.net_mac[i];
    eth_copy(out_status->driver, sizeof(out_status->driver), eth_driver_name(&g_usb_eth_dev));

    if (g_usb_eth_dev.net_driver == USB_NET_DRIVER_ASIX_AX88179 && g_usb_eth_ax_ready && !g_usb_eth_ax_link_up)
        (void)ax88179_link_reset();

    eth_copy(out_status->detail, sizeof(out_status->detail), "vid:pid ");
    eth_append_hex16(out_status->detail, sizeof(out_status->detail), g_usb_eth_dev.net_vid);
    eth_append(out_status->detail, sizeof(out_status->detail), ":");
    eth_append_hex16(out_status->detail, sizeof(out_status->detail), g_usb_eth_dev.net_pid);
    eth_append(out_status->detail, sizeof(out_status->detail), " port ");
    eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_dev.port_id);
    eth_append(out_status->detail, sizeof(out_status->detail), " mtu ");
    eth_append_u32(out_status->detail, sizeof(out_status->detail), out_status->mtu);
    if (g_usb_eth_dev.net_driver == USB_NET_DRIVER_ASIX_AX88179)
        eth_append(out_status->detail, sizeof(out_status->detail), g_usb_eth_ax_ready ? " ax-ready" : " ax-init-pending");
    if (g_usb_eth_dev.net_driver == USB_NET_DRIVER_ASIX_AX88179)
    {
        eth_append(out_status->detail, sizeof(out_status->detail), g_usb_eth_ax_link_up ? " link-up" : " link-down");
        eth_append(out_status->detail, sizeof(out_status->detail), " physr ");
        eth_append_hex16(out_status->detail, sizeof(out_status->detail), g_usb_eth_ax_physr);
        eth_append(out_status->detail, sizeof(out_status->detail), " usb ");
        eth_append_hex8(out_status->detail, sizeof(out_status->detail), g_usb_eth_ax_link_sts);
        eth_append(out_status->detail, sizeof(out_status->detail), " tx ");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_tx_len);
        eth_append(out_status->detail, sizeof(out_status->detail), "/");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_tx_got);
        eth_append(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_tx_rc == 0 ? " tok" : " terr");
        eth_append(out_status->detail, sizeof(out_status->detail), " fifo ");
        eth_append_hex16(out_status->detail, sizeof(out_status->detail), (uint16_t)(g_usb_eth_last_tx_fifo_before >> 16));
        eth_append_hex16(out_status->detail, sizeof(out_status->detail), (uint16_t)g_usb_eth_last_tx_fifo_before);
        eth_append(out_status->detail, sizeof(out_status->detail), "/");
        eth_append_hex16(out_status->detail, sizeof(out_status->detail), (uint16_t)(g_usb_eth_last_tx_fifo_after >> 16));
        eth_append_hex16(out_status->detail, sizeof(out_status->detail), (uint16_t)g_usb_eth_last_tx_fifo_after);
        eth_append(out_status->detail, sizeof(out_status->detail), " x ");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_xhci_cc);
        eth_append(out_status->detail, sizeof(out_status->detail), ":");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_xhci_ep);
        eth_append(out_status->detail, sizeof(out_status->detail), ":");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_xhci_rem);
        eth_append(out_status->detail, sizeof(out_status->detail), " rx ");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_rx_got);
        eth_append(out_status->detail, sizeof(out_status->detail), " rr ");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_rx_reason);
        eth_append(out_status->detail, sizeof(out_status->detail), " rxh ");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_rx_xhci_cc);
        eth_append(out_status->detail, sizeof(out_status->detail), ":");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_rx_xhci_ep);
        eth_append(out_status->detail, sizeof(out_status->detail), ":");
        eth_append_u32(out_status->detail, sizeof(out_status->detail), g_usb_eth_last_rx_xhci_rem);
    }
    if (g_usb_eth_dev.net_mac_valid)
    {
        eth_append(out_status->detail, sizeof(out_status->detail), " mac ");
        for (uint32_t i = 0u; i < 6u; ++i)
        {
            if (i)
                eth_append(out_status->detail, sizeof(out_status->detail), ":");
            eth_append_hex8(out_status->detail, sizeof(out_status->detail), g_usb_eth_dev.net_mac[i]);
        }
    }
}

int usb_ethernet_send_frame(const void *frame, uint32_t len)
{
    uint32_t tx_len;
    uint32_t got = 0u;
    int rc;

    g_usb_eth_last_tx_rc = -1;
    g_usb_eth_last_tx_got = 0u;
    g_usb_eth_last_tx_fifo_before = 0u;
    g_usb_eth_last_tx_fifo_after = 0u;

    if (!g_usb_eth_online || !frame || len < 14u || len > 2048u)
        return -1;
    if (eth_dma_ready() != 0)
        return -1;
    if (g_usb_eth_dev.net_driver == USB_NET_DRIVER_ASIX_AX88179)
    {
        if (!g_usb_eth_ax_ready || len + 8u > g_usb_eth_dma_cap)
            return -1;
        if (!g_usb_eth_ax_link_up && ax88179_link_reset() != 0)
            return -1;
        eth_put_le32(g_usb_eth_tx_dma, len);
        eth_put_le32(g_usb_eth_tx_dma + 4u,
                     (g_usb_eth_dev.mps_bulk_out && ((len + 8u) % g_usb_eth_dev.mps_bulk_out) == 0u) ? 0x80008000u : 0u);
        memcpy(g_usb_eth_tx_dma + 8u, frame, len);
        tx_len = len + 8u;
        g_usb_eth_last_tx_len = tx_len;
        (void)ax_read_tx_fifo_status(&g_usb_eth_last_tx_fifo_before);
        rc = usbh_bulk_out_got(&g_usb_eth_dev, g_usb_eth_tx_dma, tx_len, &got);
        g_usb_eth_last_tx_rc = rc;
        g_usb_eth_last_tx_got = got;
        g_usb_eth_last_xhci_cc = g_xhci_last_cc;
        g_usb_eth_last_xhci_ep = g_xhci_last_ev_epid;
        g_usb_eth_last_xhci_rem = g_xhci_last_ev_len;
        (void)ax_read_tx_fifo_status(&g_usb_eth_last_tx_fifo_after);
        return rc;
    }
    memcpy(g_usb_eth_tx_dma, frame, len);
    g_usb_eth_last_tx_len = len;
    rc = usbh_bulk_out_got(&g_usb_eth_dev, g_usb_eth_tx_dma, len, &got);
    g_usb_eth_last_tx_rc = rc;
    g_usb_eth_last_tx_got = got;
    return rc;
}

int usb_ethernet_recv_frame(void *frame, uint32_t cap, uint32_t *out_len)
{
    uint32_t got = 0u;
    uint32_t rx_hdr;
    uint32_t pkt_cnt;
    uint32_t hdr_off;
    uint32_t data_off = 0u;
    if (out_len)
        *out_len = 0u;

    if (!g_usb_eth_online || !frame || cap < 64u)
        return -1;

    if (g_usb_eth_pending_used)
    {
        uint32_t pending_len = g_usb_eth_pending_len[g_usb_eth_pending_head];
        if (pending_len > cap)
        {
            g_usb_eth_last_rx_reason = USB_ETH_RX_TOO_BIG;
            return -1;
        }
        memcpy(frame, g_usb_eth_pending[g_usb_eth_pending_head], pending_len);
        g_usb_eth_pending_head = (uint8_t)((g_usb_eth_pending_head + 1u) % USB_ETH_PENDING_COUNT);
        --g_usb_eth_pending_used;
        if (out_len)
            *out_len = pending_len;
        return 0;
    }
    g_usb_eth_last_rx_got = 0u;
    g_usb_eth_last_rx_hdr = 0u;
    g_usb_eth_last_rx_reason = USB_ETH_RX_OK;
    g_usb_eth_last_rx_xhci_cc = 0u;
    g_usb_eth_last_rx_xhci_ep = 0u;
    g_usb_eth_last_rx_xhci_rem = 0u;
    if (eth_dma_ready() != 0)
        return -1;
    if (cap > g_usb_eth_dma_cap)
        cap = g_usb_eth_dma_cap;
    if (usbh_bulk_in_got_timeout(&g_usb_eth_dev, g_usb_eth_rx_dma, g_usb_eth_dma_cap,
                                 &got, 1000u) != 0)
    {
        g_usb_eth_last_rx_reason = USB_ETH_RX_NO_USB;
        g_usb_eth_last_rx_xhci_cc = g_xhci_last_cc;
        g_usb_eth_last_rx_xhci_ep = g_xhci_last_ev_epid;
        g_usb_eth_last_rx_xhci_rem = g_xhci_last_ev_len;
        return -1;
    }
    g_usb_eth_last_rx_got = got;
    g_usb_eth_last_rx_xhci_cc = g_xhci_last_cc;
    g_usb_eth_last_rx_xhci_ep = g_xhci_last_ev_epid;
    g_usb_eth_last_rx_xhci_rem = g_xhci_last_ev_len;
    if (g_usb_eth_dev.net_driver == USB_NET_DRIVER_ASIX_AX88179)
    {
        if (!g_usb_eth_ax_ready || got < 8u)
        {
            g_usb_eth_last_rx_reason = (!g_usb_eth_ax_ready) ? USB_ETH_RX_NOT_READY : USB_ETH_RX_SHORT;
            return -1;
        }
        rx_hdr = eth_get_le32(g_usb_eth_rx_dma + got - 4u);
        g_usb_eth_last_rx_hdr = rx_hdr;
        pkt_cnt = rx_hdr & 0xFFFFu;
        hdr_off = (rx_hdr >> 16) & 0xFFFFu;
        if (!pkt_cnt || hdr_off + pkt_cnt * 4u > got - 4u)
        {
            g_usb_eth_last_rx_reason = USB_ETH_RX_BAD_TRAILER;
            return -1;
        }
        for (uint32_t i = 0u; i < pkt_cnt; ++i)
        {
            uint32_t pkt_hdr = eth_get_le32(g_usb_eth_rx_dma + hdr_off + i * 4u);
            uint32_t pkt_len = (pkt_hdr >> 16) & 0x1FFFu;
            uint32_t pkt_pad = (pkt_len + 7u) & 0xFFF8u;
            if (!pkt_len)
                continue;
            if (data_off + pkt_pad > hdr_off)
            {
                g_usb_eth_last_rx_reason = USB_ETH_RX_OVERLAP;
                return -1;
            }
            if ((pkt_hdr & (AX_RXHDR_CRC_ERR | AX_RXHDR_DROP_ERR)) || pkt_len < 16u)
            {
                g_usb_eth_last_rx_reason = USB_ETH_RX_BAD_PACKET;
                data_off += pkt_pad;
                continue;
            }
            if (pkt_len - 2u > USB_ETH_PENDING_CAP)
            {
                g_usb_eth_last_rx_reason = USB_ETH_RX_TOO_BIG;
                data_off += pkt_pad;
                continue;
            }
            if (g_usb_eth_pending_used < USB_ETH_PENDING_COUNT)
            {
                uint8_t tail = (uint8_t)((g_usb_eth_pending_head + g_usb_eth_pending_used) % USB_ETH_PENDING_COUNT);
                g_usb_eth_pending_len[tail] = (uint16_t)(pkt_len - 2u);
                memcpy(g_usb_eth_pending[tail], g_usb_eth_rx_dma + data_off + 2u, pkt_len - 2u);
                ++g_usb_eth_pending_used;
            }
            data_off += pkt_pad;
        }
        if (g_usb_eth_pending_used)
        {
            uint32_t pending_len = g_usb_eth_pending_len[g_usb_eth_pending_head];
            if (pending_len > cap)
            {
                g_usb_eth_last_rx_reason = USB_ETH_RX_TOO_BIG;
                return -1;
            }
            memcpy(frame, g_usb_eth_pending[g_usb_eth_pending_head], pending_len);
            g_usb_eth_pending_head = (uint8_t)((g_usb_eth_pending_head + 1u) % USB_ETH_PENDING_COUNT);
            --g_usb_eth_pending_used;
            if (out_len)
                *out_len = pending_len;
            g_usb_eth_last_rx_reason = USB_ETH_RX_OK;
            return 0;
        }
        g_usb_eth_last_rx_reason = USB_ETH_RX_NO_VALID;
        return -1;
    }
    if (got > cap)
        got = cap;
    memcpy(frame, g_usb_eth_rx_dma, got);
    if (out_len)
        *out_len = got;
    return 0;
}

uint32_t usb_ethernet_pending_frames(void)
{
    return g_usb_eth_pending_used;
}
