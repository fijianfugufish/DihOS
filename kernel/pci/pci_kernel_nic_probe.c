#include "pci/pci_kernel_nic_probe.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "pci/pci_ecam_lookup.h"
#include "memory/mmio_map.h"
#include "memory/pmem.h"
#include "gpio/gpio.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"
#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} efi_mdesc_local;

static uint32_t g_net_hints = 0u;

void pci_kernel_set_net_hints(uint32_t hints)
{
    g_net_hints = hints;
}

static int ranges_overlap(uint64_t a_base, uint64_t a_size, uint64_t b_base, uint64_t b_size)
{
    uint64_t a_end = a_base + a_size;
    uint64_t b_end = b_base + b_size;
    if (!a_size || !b_size)
        return 0;
    if (a_end < a_base)
        a_end = ~0ull;
    if (b_end < b_base)
        b_end = ~0ull;
    return (a_base < b_end) && (b_base < a_end);
}

static int ecam_overlaps_efi_ram(const boot_info *bi, uint64_t base, uint64_t size)
{
    const uint32_t EFI_BOOT_SERVICES_CODE = 3u;
    const uint32_t EFI_BOOT_SERVICES_DATA = 4u;
    const uint32_t EFI_CONVENTIONAL_MEMORY = 7u;
    const uint64_t PAGE_SIZE = 4096ull;

    if (!bi || !bi->mmap || !bi->mmap_size)
        return 0;

    uint8_t *p = (uint8_t *)(uintptr_t)bi->mmap;
    uint8_t *end = p + bi->mmap_size;
    uint64_t dsz = bi->mmap_desc_size ? bi->mmap_desc_size : sizeof(efi_mdesc_local);

    for (; p + dsz <= end; p += dsz)
    {
        const efi_mdesc_local *d = (const efi_mdesc_local *)p;
        uint64_t dbase = d->PhysicalStart;
        uint64_t dsize = d->NumberOfPages * PAGE_SIZE;

        if (!(d->Type == EFI_BOOT_SERVICES_CODE ||
              d->Type == EFI_BOOT_SERVICES_DATA ||
              d->Type == EFI_CONVENTIONAL_MEMORY))
            continue;

        if (ranges_overlap(base, size, dbase, dsize))
            return 1;
    }

    return 0;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline uint8_t mmio_read8(uint64_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static inline uint16_t mmio_read16(uint64_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

static inline void mmio_write16(uint64_t addr, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)addr = value;
}

static inline void mmio_write8(uint64_t addr, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)addr = value;
}

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static int parse_pci_segment_from_dev_name(const char *dev_name, uint16_t *seg_out)
{
    uint32_t seg = 0;
    uint32_t i = 3u;
    uint32_t digits = 0u;

    if (!dev_name || !seg_out)
        return 0;

    if (!(dev_name[0] == 'P' && dev_name[1] == 'C' && dev_name[2] == 'I'))
        return 0;

    while (dev_name[i])
    {
        char c = dev_name[i++];
        if (c < '0' || c > '9')
            return 0;
        seg = (seg * 10u) + (uint32_t)(c - '0');
        if (seg > 0xFFFFu)
            return 0;
        digits++;
    }

    if (digits == 0u)
        return 0;

    *seg_out = (uint16_t)seg;
    return 1;
}

static int choose_preferred_segment_from_net_resources(uint16_t *seg_out)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();
    uint16_t fallback_seg = 0;
    int have_fallback = 0;

    if (!seg_out || !res || nres == 0u)
        return 0;

    for (uint32_t i = 0; i < nres; ++i)
    {
        uint16_t seg = 0;
        if (!parse_pci_segment_from_dev_name(res[i].dev_name, &seg))
            continue;

        if (!have_fallback)
        {
            fallback_seg = seg;
            have_fallback = 1;
        }

        if (res[i].kind == 1u && res[i].rtype == 0u)
        {
            *seg_out = seg;
            return 1;
        }
    }

    if (have_fallback)
    {
        *seg_out = fallback_seg;
        return 1;
    }

    return 0;
}

static int is_likely_network_class(uint8_t class_code)
{
    if (class_code == 0x02u) /* Network controller */
        return 1;
    if (class_code == 0x0Du) /* Wireless controller */
        return 1;
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}

static void short_delay(void)
{
    for (volatile uint32_t i = 0; i < 4000000u; ++i)
    {
    }
}

static void print_acpi_net_resource_windows(void)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();

    terminal_print("[K:PCI] ACPI net resource windows available: ");
    terminal_print_inline_hex64(nres);

    for (uint32_t i = 0; i < nres; ++i)
    {
        terminal_print("[K:PCI] platform-net dev=");
        terminal_print(res[i].dev_name);
        terminal_print(" hid=");
        terminal_print(res[i].hid_name);
        terminal_print(" min=");
        terminal_print_inline_hex64(res[i].min_addr);
        terminal_print(" max=");
        terminal_print_inline_hex64(res[i].max_addr);
        terminal_print(" len=");
        terminal_print_inline_hex64(res[i].span_len);
    }
}

#define QCOM_FC7800_SEGMENT 4u
#define QCOM_FC7800_BUS 1u
#define QCOM_FC7800_DEV 0u
#define QCOM_FC7800_FN 0u
#define QCOM_FC7800_VENDOR 0x17CBu
#define QCOM_FC7800_DEVICE 0x1107u
#define QCOM_FC7800_ROOTPORT_DEVICE 0x0111u
#define QWIFI_BAR0_PROBE_BYTES 0x200000ull
#define ATH12K_WINDOW_ENABLE_BIT 0x40000000u
#define ATH12K_WINDOW_REG_ADDRESS 0x310Cu
#define ATH12K_WINDOW_START 0x80000u
#define ATH12K_WINDOW_RANGE_MASK 0x7FFFFu
#define ATH12K_TCSR_SOC_HW_VERSION 0x01B00000u
#define ATH12K_TCSR_SOC_HW_VERSION_MAJOR_MASK 0x00000F00u
#define ATH12K_TCSR_SOC_HW_VERSION_MINOR_MASK 0x000000F0u
#define ATH12K_MHIREGLEN 0x00u
#define ATH12K_MHIVER 0x08u
#define ATH12K_MHICFG 0x10u
#define ATH12K_CHDBOFF 0x18u
#define ATH12K_ERDBOFF 0x20u
#define ATH12K_BHIOFF 0x28u
#define ATH12K_BHIEOFF 0x2Cu
#define ATH12K_DEBUGOFF 0x30u
#define ATH12K_MHICTRL 0x38u
#define ATH12K_MHISTATUS 0x48u
#define ATH12K_CCABAP_LOWER 0x58u
#define ATH12K_CCABAP_HIGHER 0x5Cu
#define ATH12K_ECABAP_LOWER 0x60u
#define ATH12K_ECABAP_HIGHER 0x64u
#define ATH12K_CRCBAP_LOWER 0x68u
#define ATH12K_CRCBAP_HIGHER 0x6Cu
#define ATH12K_CRDB_LOWER 0x70u
#define ATH12K_CRDB_HIGHER 0x74u
#define ATH12K_MHICTRLBASE_LOWER 0x80u
#define ATH12K_MHICTRLBASE_HIGHER 0x84u
#define ATH12K_MHICTRLLIMIT_LOWER 0x88u
#define ATH12K_MHICTRLLIMIT_HIGHER 0x8Cu
#define ATH12K_MHIDATABASE_LOWER 0x98u
#define ATH12K_MHIDATABASE_HIGHER 0x9Cu
#define ATH12K_MHIDATALIMIT_LOWER 0xA0u
#define ATH12K_MHIDATALIMIT_HIGHER 0xA4u
#define ATH12K_MHICFG_NHWER_MASK 0xFF000000u
#define ATH12K_MHICFG_NER_MASK 0x00FF0000u
#define ATH12K_MHICFG_NHWCH_MASK 0x0000FF00u
#define ATH12K_MHICFG_NCH_MASK 0x000000FFu
#define ATH12K_MHICTRL_MHISTATE_MASK 0x0000FF00u
#define ATH12K_MHICTRL_RESET_MASK 0x00000002u
#define ATH12K_MHISTATUS_MHISTATE_MASK 0x0000FF00u
#define ATH12K_MHISTATUS_SYSERR_MASK 0x00000004u
#define ATH12K_MHISTATUS_READY_MASK 0x00000001u
#define ATH12K_BHI_VERSION_MINOR 0x00u
#define ATH12K_BHI_VERSION_MAJOR 0x04u
#define ATH12K_BHI_IMGADDR_LOW 0x08u
#define ATH12K_BHI_IMGADDR_HIGH 0x0Cu
#define ATH12K_BHI_IMGSIZE 0x10u
#define ATH12K_BHI_IMGTXDB 0x18u
#define ATH12K_BHI_EXECENV 0x28u
#define ATH12K_BHI_STATUS 0x2Cu
#define ATH12K_BHI_ERRCODE 0x30u
#define ATH12K_BHI_ERRDBG1 0x34u
#define ATH12K_BHI_ERRDBG2 0x38u
#define ATH12K_BHI_ERRDBG3 0x3Cu
#define ATH12K_BHI_SERIALNU 0x40u
#define ATH12K_BHIE_MSMSOCID 0x00u
#define ATH12K_BHIE_TXVECADDR_LOW 0x2Cu
#define ATH12K_BHIE_TXVECADDR_HIGH 0x30u
#define ATH12K_BHIE_TXVECSIZE 0x34u
#define ATH12K_BHIE_TXVECDB 0x3Cu
#define ATH12K_BHIE_TXVECSTATUS 0x44u
#define ATH12K_BHIE_RXVECSTATUS 0x78u
#define ATH12K_PCIE_TXVECDB 0x360u
#define ATH12K_PCIE_TXVECSTATUS 0x368u
#define ATH12K_PCIE_RXVECDB 0x394u
#define ATH12K_PCIE_RXVECSTATUS 0x39Cu
#define QWIFI_MHI_MAX_CHAN 128u
#define QWIFI_MHI_EVENT_RINGS 2u
#define QWIFI_MHI_CMD_RINGS 1u
#define QWIFI_MHI_RING_BYTES 4096u
#define QWIFI_MHI_EVENT0_ELEMENTS 32u
#define QWIFI_MHI_EVENT1_ELEMENTS 256u
#define QWIFI_MHI_EVENT_ELEMENT_BYTES 16u
#define QWIFI_MHI_SBL_BYTES 0x80000u
#define QWIFI_BHIE_SEG_BYTES 0x80000u
#define QWIFI_BHIE_MAX_SEGMENTS 32u
#define QWIFI_MHI_TRE_BYTES 16u
#define QWIFI_MHI_IPCR_TX_CH 20u
#define QWIFI_MHI_IPCR_RX_CH 21u
#define QWIFI_MHI_IPCR_ELEMENTS 64u
#define QWIFI_MHI_IPCR_RX_PREPOST 32u
#define QWIFI_MHI_IPCR_RX_BUF_BYTES 4096u
#define QWIFI_MHI_IPCR_TX_BUF_BYTES 8192u
#define QWIFI_MHI_CMD_RING_ELEMENTS (QWIFI_MHI_RING_BYTES / QWIFI_MHI_TRE_BYTES)
#define QWIFI_MHI_ER_TYPE_VALID 1u
#define QWIFI_MHI_STATE_M0 2u
#define QWIFI_MHI_CH_STATE_ENABLED 1u
#define QWIFI_MHI_CH_TYPE_OUTBOUND 1u
#define QWIFI_MHI_CH_TYPE_INBOUND 2u
#define QWIFI_MHI_DB_BRST_DISABLE 2u
#define QWIFI_MHI_CMD_START_CHAN 18u
#define QWIFI_MHI_PKT_TYPE_TRANSFER 0x02u
#define QWIFI_MHI_PKT_TYPE_CMD_COMPLETION_EVENT 0x21u
#define QWIFI_MHI_PKT_TYPE_TX_EVENT 0x22u
#define QWIFI_MHI_EV_CC_SUCCESS 1u
#define QWIFI_MHI_EV_CC_EOT 2u
#define QWIFI_MHI_EV_CC_OVERFLOW 3u
#define QWIFI_MHI_EV_CC_EOB 4u
#define QWIFI_BHI_STATUS_SUCCESS 2u
#define QWIFI_BHI_STATUS_ERROR 3u
#define QWIFI_BHIE_STATUS_XFER_COMPL 2u
#define QWIFI_BHIE_STATUS_ERROR 3u
#define QWIFI_QRTR_PROTO_VER_1 1u
#define QWIFI_QRTR_TYPE_DATA 1u
#define QWIFI_QRTR_TYPE_HELLO 2u
#define QWIFI_QRTR_TYPE_BYE 3u
#define QWIFI_QRTR_TYPE_NEW_SERVER 4u
#define QWIFI_QRTR_TYPE_DEL_SERVER 5u
#define QWIFI_QRTR_TYPE_DEL_CLIENT 6u
#define QWIFI_QRTR_TYPE_RESUME_TX 7u
#define QWIFI_QRTR_TYPE_NEW_LOOKUP 10u
#define QWIFI_QRTR_TYPE_DEL_LOOKUP 11u
#define QWIFI_QRTR_TX_FLOW_LOW 5u
#define QWIFI_QRTR_TX_FLOW_HIGH 10u
#define QWIFI_QRTR_NODE_BCAST 0xFFFFFFFFu
#define QWIFI_QRTR_PORT_CTRL 0xFFFFFFFEu
#define QWIFI_QRTR_LOCAL_NODE 1u
#define QWIFI_QRTR_LOCAL_PORT 0x4000u
#define QWIFI_QRTR_WLANFW_SERVICE 0x45u
#define QWIFI_QRTR_WLANFW_INSTANCE_WCN7850 0x101u
#define QWIFI_QMI_REQUEST 0u
#define QWIFI_QMI_RESPONSE 2u
#define QWIFI_QMI_INDICATION 4u
#define QWIFI_QMI_WLANFW_IND_REGISTER_REQ 0x0020u
#define QWIFI_QMI_WLANFW_WLAN_MODE_REQ 0x0022u
#define QWIFI_QMI_WLANFW_WLAN_CFG_REQ 0x0023u
#define QWIFI_QMI_WLANFW_CAP_REQ 0x0024u
#define QWIFI_QMI_WLANFW_BDF_DOWNLOAD_REQ 0x0025u
#define QWIFI_QMI_WLANFW_WLAN_INI_REQ 0x002Fu
#define QWIFI_QMI_WLANFW_HOST_CAP_REQ 0x0034u
#define QWIFI_QMI_WLANFW_REQUEST_MEM_IND 0x0035u
#define QWIFI_QMI_WLANFW_RESPOND_MEM_REQ 0x0036u
#define QWIFI_QMI_WLANFW_FW_MEM_READY_IND 0x0037u
#define QWIFI_QMI_WLANFW_FW_READY_IND 0x0038u
#define QWIFI_QMI_WLANFW_M3_INFO_REQ 0x003Cu
#define QWIFI_QMI_WLANFW_PHY_CAP_REQ 0x0057u
#define QWIFI_QMI_TXN_PHY_CAP 1u
#define QWIFI_QMI_TXN_IND_REGISTER 2u
#define QWIFI_QMI_TXN_HOST_CAP 3u
#define QWIFI_QMI_TXN_RESPOND_MEM 4u
#define QWIFI_QMI_TXN_TARGET_CAP 5u
#define QWIFI_QMI_TXN_M3_INFO 6u
#define QWIFI_QMI_TXN_WLAN_INI 7u
#define QWIFI_QMI_TXN_WLAN_CFG 8u
#define QWIFI_QMI_TXN_WLAN_MODE 9u
#define QWIFI_QMI_TXN_BDF_DOWNLOAD 10u
#define QWIFI_QMI_WLANFW_CLIENT_ID 0x4B4E454Cu
#define QWIFI_QMI_NM_MODEM_SLEEP_CLOCK_INTERNAL 0x02u
#define QWIFI_QMI_NM_MODEM_HOST_CSTATE 0x04u
#define QWIFI_QMI_NM_MODEM_PCIE_GLOBAL_RESET 0x08u
#define QWIFI_QMI_WCN7850_MAX_MLO_PEER 32u
#define QWIFI_QMI_WCN7850_FEATURE_LIST ((1ull << 3) | (1ull << 4))
#define QWIFI_QMI_MEM_MAX_SEG 8u
#define QWIFI_QMI_BDF_CHUNK_BYTES 6144u
#define QWIFI_QMI_BDF_TYPE_BIN 0u
#define QWIFI_QMI_BDF_TYPE_ELF 1u
#define QWIFI_QMI_BDF_TYPE_REGDB 4u
#define QWIFI_QMI_WCN7850_BOARD_ID_DEFAULT 0xFFu
#define QWIFI_BOARD2_IE_BOARD 0u
#define QWIFI_BOARD2_IE_REGDB 1u
#define QWIFI_BOARD2_SUBIE_NAME 0u
#define QWIFI_BOARD2_SUBIE_DATA 1u
#define QWIFI_QMI_FW_MODE_NORMAL 0u
#define QWIFI_CE_PIPEDIR_NONE 0u
#define QWIFI_CE_PIPEDIR_IN 1u
#define QWIFI_CE_PIPEDIR_OUT 2u
#define QWIFI_CE_PIPEDIR_INOUT 3u
#define QWIFI_CE_PIPEDIR_INOUT_H2H 4u
#define QWIFI_CE_ATTR_DIS_INTR 8u
#define QWIFI_CE_COUNT 9u
#define QWIFI_CE_RING_ID_SRC_BASE 64u
#define QWIFI_CE_RING_ID_DST_BASE 81u
#define QWIFI_CE_RING_ID_DST_STATUS_BASE 100u
#define QWIFI_CE_REG_SRC_BASE 0x01B80000u
#define QWIFI_CE_REG_DST_BASE 0x01B81000u
#define QWIFI_CE_REG_STRIDE 0x2000u
#define QWIFI_CE_USE_SHADOW_REGS 0u
#define QWIFI_HAL_SHADOW_BASE_ADDR 0x000008FCu
#define QWIFI_HAL_SHADOW_REG(i) (QWIFI_HAL_SHADOW_BASE_ADDR + ((uint32_t)(i) * 4u))
#define QWIFI_CE_SHADOW_INVALID 0xFFFFFFFFu
#define QWIFI_QMI_WLAN_CFG_TLV_SHADOW_REG_V3 0x17u
#define QWIFI_CE_R0_BASE_LSB 0x000u
#define QWIFI_CE_R0_BASE_MSB 0x004u
#define QWIFI_CE_R0_RING_ID 0x008u
#define QWIFI_CE_R0_MISC 0x010u
#define QWIFI_CE_R0_HP_ADDR_LSB 0x014u
#define QWIFI_CE_R0_HP_ADDR_MSB 0x018u
#define QWIFI_CE_R0_TP_ADDR_LSB 0x01Cu
#define QWIFI_CE_R0_TP_ADDR_MSB 0x020u
#define QWIFI_CE_R0_INT_SETUP0 0x030u
#define QWIFI_CE_R0_INT_SETUP1 0x034u
#define QWIFI_CE_R0_STATUS_BASE_LSB 0x058u
#define QWIFI_CE_R0_DST_CTRL 0x0B0u
#define QWIFI_CE_R2_HP 0x400u
#define QWIFI_CE_R2_TP 0x404u
#define QWIFI_CE_R2_STATUS_HP 0x408u
#define QWIFI_CE_R2_STATUS_TP 0x40Cu
#define QWIFI_CE_SRC_DESC_DWORDS 4u
#define QWIFI_CE_DST_DESC_DWORDS 2u
#define QWIFI_CE_STATUS_DESC_DWORDS 4u
#define QWIFI_CE_MAX_RX_POST 512u
#define QWIFI_CE_RX_BUF_BYTES 2048u
#define QWIFI_CE_DESC_ADDR_HI_MASK 0xFFu
#define QWIFI_CE_MISC_MSI_RING_ID_DISABLE (1u << 0)
#define QWIFI_CE_MISC_MSI_LOOPCNT_DISABLE (1u << 1)
#define QWIFI_CE_MISC_SRNG_ENABLE (1u << 6)
#define QWIFI_HTC_MSG_READY_ID 1u
#define QWIFI_HTC_MSG_CONNECT_SERVICE_ID 2u
#define QWIFI_HTC_MSG_CONNECT_SERVICE_RESP_ID 3u
#define QWIFI_HTC_MSG_SETUP_COMPLETE_EX_ID 5u
#define QWIFI_HTC_SVC_WMI_DATA_BE 0x0101u
#define QWIFI_HTC_SVC_WMI_DATA_BK 0x0102u
#define QWIFI_HTC_SVC_WMI_DATA_VI 0x0103u
#define QWIFI_HTC_SVC_WMI_DATA_VO 0x0104u
#define QWIFI_HTC_SVC_WMI_CONTROL 0x0100u
#define QWIFI_HTC_SVC_RSVD_CTRL 0x0001u
#define QWIFI_HTC_SVC_HTT_DATA_MSG 0x0300u

typedef struct __attribute__((packed))
{
    uint32_t chcfg;
    uint32_t chtype;
    uint32_t erindex;
    uint64_t rbase;
    uint64_t rlen;
    uint64_t rp;
    uint64_t wp;
} qwifi_mhi_chan_ctxt;

typedef struct __attribute__((packed))
{
    uint32_t intmod;
    uint32_t ertype;
    uint32_t msivec;
    uint64_t rbase;
    uint64_t rlen;
    uint64_t rp;
    uint64_t wp;
} qwifi_mhi_event_ctxt;

typedef struct __attribute__((packed))
{
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint64_t rbase;
    uint64_t rlen;
    uint64_t rp;
    uint64_t wp;
} qwifi_mhi_cmd_ctxt;

typedef struct __attribute__((packed))
{
    uint64_t dma_addr;
    uint64_t size;
} qwifi_bhi_vec_entry;

typedef struct __attribute__((packed))
{
    uint64_t ptr;
    uint32_t dword0;
    uint32_t dword1;
} qwifi_mhi_ring_element;

typedef struct __attribute__((packed))
{
    uint32_t version;
    uint32_t type;
    uint32_t src_node_id;
    uint32_t src_port_id;
    uint32_t confirm_rx;
    uint32_t size;
    uint32_t dst_node_id;
    uint32_t dst_port_id;
} qwifi_qrtr_hdr_v1;

typedef struct __attribute__((packed))
{
    uint32_t cmd;
    uint32_t service;
    uint32_t instance;
    uint32_t node;
    uint32_t port;
} qwifi_qrtr_ctrl_pkt;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint16_t txn_id;
    uint16_t msg_id;
    uint16_t msg_len;
} qwifi_qmi_hdr;

typedef struct __attribute__((packed))
{
    uint32_t buffer_addr_low;
    uint32_t buffer_addr_info;
    uint32_t meta_info;
    uint32_t flags;
} qwifi_ce_src_desc;

typedef struct __attribute__((packed))
{
    uint32_t buffer_addr_low;
    uint32_t buffer_addr_info;
} qwifi_ce_dst_desc;

typedef struct __attribute__((packed))
{
    uint32_t flags;
    uint32_t toeplitz_hash0;
    uint32_t toeplitz_hash1;
    uint32_t meta_info;
} qwifi_ce_dst_status_desc;

typedef struct
{
    uint32_t src_entries;
    uint32_t dst_entries;
    uint32_t status_entries;
    uint32_t src_wp;
    uint32_t dst_wp;
    uint32_t dst_sw_index;
    uint32_t status_rp;
    uint32_t src_shadow_idx;
    uint32_t dst_shadow_idx;
    uint32_t status_shadow_idx;
    uint32_t buf_sz;
    void *src_ring;
    void *dst_ring;
    void *status_ring;
    void *rx_buf[QWIFI_CE_MAX_RX_POST];
    uint64_t rx_phys[QWIFI_CE_MAX_RX_POST];
} qwifi_ce_pipe;

static qwifi_mhi_chan_ctxt *g_qwifi_chan_ctxt;
static qwifi_mhi_event_ctxt *g_qwifi_event_ctxt;
static qwifi_mhi_cmd_ctxt *g_qwifi_cmd_ctxt;
static void *g_qwifi_event_ring[QWIFI_MHI_EVENT_RINGS];
static void *g_qwifi_cmd_ring;
static void *g_qwifi_ipcr_tx_ring;
static void *g_qwifi_ipcr_rx_ring;
static void *g_qwifi_ipcr_rx_bufs[QWIFI_MHI_IPCR_RX_PREPOST];
static uint64_t g_qwifi_ipcr_rx_buf_phys[QWIFI_MHI_IPCR_RX_PREPOST];
static qwifi_ce_pipe g_qwifi_ce[QWIFI_CE_COUNT];
static uint32_t *g_qwifi_ce_rdp;
static uint64_t g_qwifi_ce_rdp_phys;
static uint32_t g_qwifi_htc_ready_seen;
static uint32_t g_qwifi_htc_total_credits;
static uint32_t g_qwifi_htc_credit_size;
static uint32_t g_qwifi_ce_prepared;
static uint32_t g_qwifi_chan_count;
static uint32_t g_qwifi_cmd_wp_index;
static uint32_t g_qwifi_event_read_index[QWIFI_MHI_EVENT_RINGS];
static uint32_t g_qwifi_ipcr_tx_wp_index;
static uint32_t g_qwifi_ipcr_rx_wp_index;
static uint64_t g_qwifi_ipcr_bar0_base;
static uint32_t g_qwifi_ipcr_chdboff;
static uint32_t g_qwifi_qrtr_remote_node;
static uint32_t g_qwifi_qrtr_wlan_node;
static uint32_t g_qwifi_qrtr_wlan_port;
static uint32_t g_qwifi_qrtr_tx_pending;
static uint32_t g_qwifi_qmi_phy_cap_resp_seen;
static uint32_t g_qwifi_qmi_ind_register_resp_seen;
static uint32_t g_qwifi_qmi_host_cap_resp_seen;
static uint32_t g_qwifi_qmi_request_mem_ind_seen;
static uint32_t g_qwifi_qmi_respond_mem_resp_seen;
static uint32_t g_qwifi_qmi_fw_mem_ready_ind_seen;
static uint32_t g_qwifi_qmi_fw_ready_ind_seen;
static uint32_t g_qwifi_qmi_target_cap_resp_seen;
static uint32_t g_qwifi_qmi_bdf_download_resp_seen;
static uint32_t g_qwifi_qmi_m3_info_resp_seen;
static uint32_t g_qwifi_qmi_wlan_ini_resp_seen;
static uint32_t g_qwifi_qmi_wlan_cfg_resp_seen;
static uint32_t g_qwifi_qmi_wlan_mode_resp_seen;
static uint16_t g_qwifi_qmi_last_resp_msg;
static uint16_t g_qwifi_qmi_last_resp_txn;
static uint16_t g_qwifi_qmi_last_resp_result;
static uint16_t g_qwifi_qmi_last_resp_error;
static uint32_t g_qwifi_target_chip_id;
static uint32_t g_qwifi_target_chip_family;
static uint32_t g_qwifi_target_board_id;
static uint32_t g_qwifi_target_soc_id;
static uint32_t g_qwifi_mem_seg_count;
static uint32_t g_qwifi_mem_seg_size[QWIFI_QMI_MEM_MAX_SEG];
static uint32_t g_qwifi_mem_seg_type[QWIFI_QMI_MEM_MAX_SEG];
static uint64_t g_qwifi_mem_seg_phys[QWIFI_QMI_MEM_MAX_SEG];
static void *g_qwifi_mem_seg_virt[QWIFI_QMI_MEM_MAX_SEG];
static uint64_t g_qwifi_m3_phys;
static uint32_t g_qwifi_m3_size;
static void *g_qwifi_m3_virt;

static const uint32_t g_qwifi_ce_shadow_reg_v3[] = {
    QWIFI_CE_REG_SRC_BASE + 0u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_HP,
    QWIFI_CE_REG_DST_BASE + 1u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_HP,
    QWIFI_CE_REG_DST_BASE + 1u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_STATUS_TP,
    QWIFI_CE_REG_DST_BASE + 2u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_HP,
    QWIFI_CE_REG_DST_BASE + 2u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_STATUS_TP,
    QWIFI_CE_REG_SRC_BASE + 3u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_HP,
    QWIFI_CE_REG_SRC_BASE + 4u * QWIFI_CE_REG_STRIDE + QWIFI_CE_R2_HP,
};

static int pci_probe_config_read32(uint64_t addr, uint32_t *out_value)
{
    if (!addr || !out_value)
        return -1;

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return asm_aa64_try_read32(addr, out_value);
#else
    *out_value = mmio_read32(addr);
    return 0;
#endif
}

static int pci_probe_mmio_write32(uint64_t addr, uint32_t value)
{
    if (!addr)
        return -1;

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return asm_aa64_try_write32(addr, value);
#else
    mmio_write32(addr, value);
    return 0;
#endif
}

static uint64_t qwifi_bar0_base_from_config(uint32_t bar0, uint32_t bar1)
{
    if (bar0 & 0x1u)
        return 0u;
    if ((bar0 & 0x6u) == 0x4u)
        return (((uint64_t)bar1) << 32) | (uint64_t)(bar0 & ~0xFull);
    return (uint64_t)(bar0 & ~0xFull);
}

static void qwifi_zero(void *ptr, uint64_t bytes)
{
    uint8_t *p = (uint8_t *)ptr;

    if (!p)
        return;

    for (uint64_t i = 0; i < bytes; ++i)
        p[i] = 0u;
}

static uint64_t qwifi_pages_for(uint64_t bytes)
{
    return (bytes + 4095ull) >> 12;
}

static void qwifi_copy_to_buf(void *dst, const void *src, uint64_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (!d || !s)
        return;

    for (uint64_t i = 0; i < bytes; ++i)
        d[i] = s[i];
}

static uint32_t qwifi_cstr_len(const char *s)
{
    uint32_t len = 0;

    if (!s)
        return 0;

    while (s[len])
        len++;
    return len;
}

static int qwifi_bytes_match(const uint8_t *buf, uint32_t len, const char *needle)
{
    uint32_t needle_len = qwifi_cstr_len(needle);

    if (!buf || !needle || len < needle_len)
        return 0;

    for (uint32_t i = 0; i < needle_len; ++i)
    {
        if (buf[i] != (uint8_t)needle[i])
            return 0;
    }

    return 1;
}

static int qwifi_ascii_contains(const uint8_t *buf, uint32_t len, const char *needle)
{
    uint32_t needle_len = qwifi_cstr_len(needle);

    if (!buf || !needle || !needle_len || len < needle_len)
        return 0;

    for (uint32_t off = 0; off + needle_len <= len; ++off)
    {
        if (qwifi_bytes_match(buf + off, needle_len, needle))
            return 1;
    }

    return 0;
}

static uint32_t qwifi_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t qwifi_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void qwifi_write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void qwifi_write_le64(uint8_t *p, uint64_t value)
{
    for (uint32_t i = 0; i < 8u; ++i)
        p[i] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
}

static void qwifi_parse_request_mem_tlv(const uint8_t *value, uint16_t len)
{
    uint32_t count;
    uint32_t off = 1u;

    if (!value || len < 1u)
        return;

    count = value[0];
    if (count > QWIFI_QMI_MEM_MAX_SEG)
        count = QWIFI_QMI_MEM_MAX_SEG;

    g_qwifi_mem_seg_count = 0u;
    terminal_print("[K:QWIFI] QMI REQUEST_MEM seg_count=");
    terminal_print_inline_hex64(value[0]);
    terminal_print(" capped=");
    terminal_print_inline_hex64(count);
    terminal_flush_log();

    for (uint32_t i = 0; i < count && off + 9u <= len; ++i)
    {
        uint32_t size = qwifi_read_le32(value + off);
        uint32_t type = qwifi_read_le32(value + off + 4u);
        uint8_t mem_cfg_len = value[off + 8u];
        uint32_t skip = 9u + (uint32_t)mem_cfg_len * 13u;

        g_qwifi_mem_seg_size[i] = size;
        g_qwifi_mem_seg_type[i] = type;
        g_qwifi_mem_seg_count++;

        terminal_print("[K:QWIFI] QMI REQUEST_MEM seg=");
        terminal_print_inline_hex64(i);
        terminal_print(" size=");
        terminal_print_inline_hex64(size);
        terminal_print(" type=");
        terminal_print_inline_hex64(type);
        terminal_print(" cfg_len=");
        terminal_print_inline_hex64(mem_cfg_len);
        terminal_flush_log();

        if (off + skip > len)
            break;
        off += skip;
    }
}

static int qwifi_mhi_write32(uint64_t bar0_base, uint32_t off, uint32_t value)
{
    int rc = pci_probe_mmio_write32(bar0_base + off, value);
    if (rc != 0)
    {
        terminal_print("[K:QWIFI] MHI write failed off=");
        terminal_print_inline_hex64(off);
        terminal_print(" rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();
    }
    return rc;
}

static int qwifi_mhi_write64_pair(uint64_t bar0_base, uint32_t lower_off, uint32_t higher_off, uint64_t value)
{
    int rc;

    rc = qwifi_mhi_write32(bar0_base, higher_off, (uint32_t)(value >> 32));
    if (rc != 0)
        return rc;
    return qwifi_mhi_write32(bar0_base, lower_off, (uint32_t)(value & 0xFFFFFFFFu));
}

static uint32_t qwifi_mhi_transfer_dword1(void)
{
    return (QWIFI_MHI_PKT_TYPE_TRANSFER << 16) | (1u << 9);
}

static int qwifi_mhi_ring_channel_db(uint64_t bar0_base, uint32_t chdboff, uint32_t chid, uint64_t wp)
{
    uint32_t off;

    if (!chdboff || chdboff >= 0x1000u)
        return -1;

    off = chdboff + chid * 8u;
    return qwifi_mhi_write64_pair(bar0_base, off, off + 4u, wp);
}

static int qwifi_get_fw_blob(const boot_info *bi, uint32_t kind, uint64_t *base_phys, uint64_t *size_bytes)
{
    uint32_t count;

    if (!bi || !base_phys || !size_bytes)
        return 0;

    count = bi->wifi_fw_count;
    if (count > BOOTINFO_WIFI_FW_MAX)
        count = BOOTINFO_WIFI_FW_MAX;

    for (uint32_t i = 0; i < count; ++i)
    {
        if (bi->wifi_fw[i].kind != kind)
            continue;
        if (!bi->wifi_fw[i].base_phys || !bi->wifi_fw[i].size_bytes)
            continue;

        *base_phys = bi->wifi_fw[i].base_phys;
        *size_bytes = bi->wifi_fw[i].size_bytes;
        return 1;
    }

    return 0;
}

static void qwifi_copy_from_phys(void *dst, uint64_t src_phys, uint64_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)pmem_phys_to_virt(src_phys);

    if (!d || !s)
        return;

    for (uint64_t i = 0; i < bytes; ++i)
        d[i] = s[i];
}

static uint32_t qwifi_bhi_status_code(uint32_t status)
{
    return (status >> 30) & 0x3u;
}

static int qwifi_bhi_try_load_sbl(uint64_t bar0_base, uint32_t bhioff, const boot_info *bi)
{
    static int attempted = 0;
    uint64_t amss_phys = 0;
    uint64_t amss_size = 0;
    uint64_t sbl_size;
    void *sbl_buf;
    uint64_t sbl_phys;
    uint32_t status = 0;
    uint32_t status_code = 0;
    uint32_t execenv = 0;
    uint32_t errcode = 0;
    uint32_t errdbg1 = 0;
    uint32_t errdbg2 = 0;
    uint32_t errdbg3 = 0;
    uint32_t poll;
    int rc = 0;

    if (attempted)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: already attempted");
        terminal_flush_log();
        return 0;
    }
    attempted = 1;

    if (!bar0_base || !bhioff || bhioff >= 0x1000u)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: invalid BHI offset");
        terminal_flush_log();
        return 0;
    }

    if (!qwifi_get_fw_blob(bi, BOOTINFO_WIFI_FW_AMSS, &amss_phys, &amss_size))
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: AMSS firmware missing");
        terminal_flush_log();
        return 0;
    }

    sbl_size = amss_size;
    if (sbl_size > QWIFI_MHI_SBL_BYTES)
        sbl_size = QWIFI_MHI_SBL_BYTES;
    if (!sbl_size || sbl_size > 0xFFFFFFFFull)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: invalid AMSS size=");
        terminal_print_inline_hex64(amss_size);
        terminal_flush_log();
        return 0;
    }

    sbl_buf = pmem_alloc_pages_lowdma(qwifi_pages_for(sbl_size));
    if (!sbl_buf)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: low-DMA alloc failed bytes=");
        terminal_print_inline_hex64(sbl_size);
        terminal_flush_log();
        return 0;
    }

    qwifi_copy_from_phys(sbl_buf, amss_phys, sbl_size);
    asm_dma_clean_range(sbl_buf, sbl_size);
    sbl_phys = pmem_virt_to_phys(sbl_buf);

    terminal_print("[K:QWIFI] BHI SBL load begin fw_pa=");
    terminal_print_inline_hex64(amss_phys);
    terminal_print(" fw_size=");
    terminal_print_inline_hex64(amss_size);
    terminal_print(" sbl_pa=");
    terminal_print_inline_hex64(sbl_phys);
    terminal_print(" sbl_size=");
    terminal_print_inline_hex64(sbl_size);
    terminal_flush_log();

    rc |= qwifi_mhi_write32(bar0_base, bhioff + ATH12K_BHI_STATUS, 0u);
    rc |= qwifi_mhi_write64_pair(bar0_base,
                                 bhioff + ATH12K_BHI_IMGADDR_LOW,
                                 bhioff + ATH12K_BHI_IMGADDR_HIGH,
                                 sbl_phys);
    rc |= qwifi_mhi_write32(bar0_base, bhioff + ATH12K_BHI_IMGSIZE, (uint32_t)sbl_size);

    terminal_print("[K:QWIFI] BHI SBL programmed rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    asm_mmio_barrier();
    rc = qwifi_mhi_write32(bar0_base, bhioff + ATH12K_BHI_IMGTXDB, 1u);
    terminal_print("[K:QWIFI] BHI SBL doorbell rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;
    asm_mmio_barrier();

    for (poll = 0; poll < 4000000u; ++poll)
    {
        rc = pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_STATUS, &status);
        if (rc != 0)
            break;
        status_code = qwifi_bhi_status_code(status);
        if (status_code != 0u)
            break;
        asm_relax();
    }

    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_EXECENV, &execenv);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRCODE, &errcode);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRDBG1, &errdbg1);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRDBG2, &errdbg2);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRDBG3, &errdbg3);

    terminal_print("[K:QWIFI] BHI SBL result rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" status=");
    terminal_print_inline_hex64(status);
    terminal_print(" code=");
    terminal_print_inline_hex64(status_code);
    terminal_print(" execenv=");
    terminal_print_inline_hex64(execenv);
    terminal_print(" err=");
    terminal_print_inline_hex64(errcode);
    terminal_print(" dbg1=");
    terminal_print_inline_hex64(errdbg1);
    terminal_print(" dbg2=");
    terminal_print_inline_hex64(errdbg2);
    terminal_print(" dbg3=");
    terminal_print_inline_hex64(errdbg3);
    if (status_code == QWIFI_BHI_STATUS_SUCCESS)
        terminal_print(" [SBL accepted]");
    else if (status_code == QWIFI_BHI_STATUS_ERROR)
        terminal_print(" [SBL error]");
    else if (rc == 0)
        terminal_print(" [SBL timeout]");
    terminal_flush_log();

    return status_code == QWIFI_BHI_STATUS_SUCCESS;
}

static void qwifi_dump_pci_caps(uint64_t cfg, uint32_t cap_ptr_raw)
{
    uint32_t ptr = cap_ptr_raw & 0xFFu;
    uint32_t seen = 0;

    if (ptr == 0u)
    {
        terminal_print("[K:QWIFI] no PCI capability list");
        terminal_flush_log();
        return;
    }

    terminal_print("[K:QWIFI] PCI capability list start=");
    terminal_print_inline_hex64(ptr);
    terminal_flush_log();

    while (ptr >= 0x40u && ptr < 0x100u && seen < 16u)
    {
        uint32_t dword0 = 0;
        uint32_t dword1 = 0;
        uint32_t dword2 = 0;
        uint32_t cap_id;
        uint32_t next;
        int rc;

        rc = pci_probe_config_read32(cfg + ptr, &dword0);
        if (rc != 0)
        {
            terminal_print("[K:QWIFI] PCI cap read failed ptr=");
            terminal_print_inline_hex64(ptr);
            terminal_print(" rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            terminal_flush_log();
            return;
        }

        cap_id = dword0 & 0xFFu;
        next = (dword0 >> 8) & 0xFFu;

        terminal_print("[K:QWIFI] PCI cap ptr=");
        terminal_print_inline_hex64(ptr);
        terminal_print(" id=");
        terminal_print_inline_hex64(cap_id);
        terminal_print(" next=");
        terminal_print_inline_hex64(next);
        terminal_print(" d0=");
        terminal_print_inline_hex64(dword0);

        if (cap_id == 0x05u) /* MSI */
        {
            uint32_t msgctl = (dword0 >> 16) & 0xFFFFu;
            terminal_print(" MSI ctl=");
            terminal_print_inline_hex64(msgctl);
        }
        else if (cap_id == 0x10u) /* PCI Express */
        {
            (void)pci_probe_config_read32(cfg + ptr + 0x04u, &dword1);
            terminal_print(" PCIE cap=");
            terminal_print_inline_hex64((dword0 >> 16) & 0xFFFFu);
            terminal_print(" devcap=");
            terminal_print_inline_hex64(dword1);
        }
        else if (cap_id == 0x11u) /* MSI-X */
        {
            (void)pci_probe_config_read32(cfg + ptr + 0x04u, &dword1);
            (void)pci_probe_config_read32(cfg + ptr + 0x08u, &dword2);
            terminal_print(" MSIX ctl=");
            terminal_print_inline_hex64((dword0 >> 16) & 0xFFFFu);
            terminal_print(" table=");
            terminal_print_inline_hex64(dword1);
            terminal_print(" pba=");
            terminal_print_inline_hex64(dword2);
        }

        terminal_flush_log();

        if (next == 0u || next == ptr)
            break;
        ptr = next;
        seen++;
    }
}

static int qwifi_ath12k_read32(uint64_t bar0_base, uint32_t offset, uint32_t *out_value)
{
    uint32_t window;
    uint32_t readback = 0;
    uint64_t read_addr;
    int rc;

    if (!bar0_base || !out_value)
        return -1;

    if (offset < ATH12K_WINDOW_START)
        return pci_probe_config_read32(bar0_base + offset, out_value);

    window = (offset >> 19) & 0x3Fu;
    rc = pci_probe_mmio_write32(bar0_base + ATH12K_WINDOW_REG_ADDRESS,
                                ATH12K_WINDOW_ENABLE_BIT | window);
    terminal_print("[K:QWIFI] ath12k window select off=");
    terminal_print_inline_hex64(offset);
    terminal_print(" window=");
    terminal_print_inline_hex64(window);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return rc;

    (void)pci_probe_config_read32(bar0_base + ATH12K_WINDOW_REG_ADDRESS, &readback);
    terminal_print("[K:QWIFI] ath12k window readback=");
    terminal_print_inline_hex64(readback);
    terminal_flush_log();

    read_addr = bar0_base + ATH12K_WINDOW_START + (uint64_t)(offset & ATH12K_WINDOW_RANGE_MASK);
    return pci_probe_config_read32(read_addr, out_value);
}

static int qwifi_ath12k_window_addr(uint64_t bar0_base, uint32_t offset, uint64_t *addr_out)
{
    uint32_t window;
    int rc;

    if (!bar0_base || !addr_out)
        return -1;

    if (offset < ATH12K_WINDOW_START)
    {
        *addr_out = bar0_base + offset;
        return 0;
    }

    window = (offset >> 19) & 0x3Fu;
    rc = pci_probe_mmio_write32(bar0_base + ATH12K_WINDOW_REG_ADDRESS,
                                ATH12K_WINDOW_ENABLE_BIT | window);
    if (rc != 0)
        return rc;

    *addr_out = bar0_base + ATH12K_WINDOW_START + (uint64_t)(offset & ATH12K_WINDOW_RANGE_MASK);
    return 0;
}

static int qwifi_ath12k_read32_quiet(uint64_t bar0_base, uint32_t offset, uint32_t *out_value)
{
    uint64_t addr;
    int rc;

    if (!out_value)
        return -1;

    rc = qwifi_ath12k_window_addr(bar0_base, offset, &addr);
    if (rc != 0)
        return rc;

    return pci_probe_config_read32(addr, out_value);
}

static int qwifi_ath12k_write32_quiet(uint64_t bar0_base, uint32_t offset, uint32_t value)
{
    uint64_t addr;
    int rc;

    rc = qwifi_ath12k_window_addr(bar0_base, offset, &addr);
    if (rc != 0)
        return rc;

    return pci_probe_mmio_write32(addr, value);
}

static uint32_t qwifi_ce_shadow_src_idx(uint32_t ce)
{
#if !QWIFI_CE_USE_SHADOW_REGS
    (void)ce;
    return QWIFI_CE_SHADOW_INVALID;
#else
    switch (ce)
    {
    case 0u:
        return 0u;
    case 3u:
        return 5u;
    case 4u:
        return 6u;
    default:
        return QWIFI_CE_SHADOW_INVALID;
    }
#endif
}

static uint32_t qwifi_ce_shadow_dst_idx(uint32_t ce)
{
#if !QWIFI_CE_USE_SHADOW_REGS
    (void)ce;
    return QWIFI_CE_SHADOW_INVALID;
#else
    switch (ce)
    {
    case 1u:
        return 1u;
    case 2u:
        return 3u;
    default:
        return QWIFI_CE_SHADOW_INVALID;
    }
#endif
}

static uint32_t qwifi_ce_shadow_status_idx(uint32_t ce)
{
#if !QWIFI_CE_USE_SHADOW_REGS
    (void)ce;
    return QWIFI_CE_SHADOW_INVALID;
#else
    switch (ce)
    {
    case 1u:
        return 2u;
    case 2u:
        return 4u;
    default:
        return QWIFI_CE_SHADOW_INVALID;
    }
#endif
}

static int qwifi_ce_shadow_write(uint64_t bar0_base, uint32_t shadow_idx, uint32_t value)
{
#if !QWIFI_CE_USE_SHADOW_REGS
    (void)bar0_base;
    (void)shadow_idx;
    (void)value;
    return 0;
#else
    if (shadow_idx == QWIFI_CE_SHADOW_INVALID)
        return 0;

    return qwifi_ath12k_write32_quiet(bar0_base, QWIFI_HAL_SHADOW_REG(shadow_idx), value);
#endif
}

static void qwifi_mhi_program_minimal_contexts(uint64_t bar0_base, uint32_t mhicfg)
{
    static int programmed = 0;
    uint32_t nch = mhicfg & ATH12K_MHICFG_NCH_MASK;
    uint32_t chan_count = nch;
    uint64_t chan_bytes;
    uint64_t event_bytes = sizeof(qwifi_mhi_event_ctxt) * QWIFI_MHI_EVENT_RINGS;
    uint64_t cmd_bytes = sizeof(qwifi_mhi_cmd_ctxt) * QWIFI_MHI_CMD_RINGS;
    uint64_t chan_pa;
    uint64_t event_pa;
    uint64_t cmd_pa;
    uint64_t event_ring_pa[QWIFI_MHI_EVENT_RINGS];
    uint64_t cmd_ring_pa;
    uint32_t readback = 0;
    uint32_t new_mhicfg;
    int rc = 0;

    if (programmed)
    {
        terminal_print("[K:QWIFI] MHI minimal context reprogram after state change");
        terminal_flush_log();
    }

    if (chan_count == 0u || chan_count > QWIFI_MHI_MAX_CHAN)
        chan_count = QWIFI_MHI_MAX_CHAN;
    chan_bytes = sizeof(qwifi_mhi_chan_ctxt) * (uint64_t)chan_count;

    if (!g_qwifi_chan_ctxt)
    {
        g_qwifi_chan_ctxt = (qwifi_mhi_chan_ctxt *)pmem_alloc_pages_lowdma(qwifi_pages_for(chan_bytes));
        g_qwifi_event_ctxt = (qwifi_mhi_event_ctxt *)pmem_alloc_pages_lowdma(qwifi_pages_for(event_bytes));
        g_qwifi_cmd_ctxt = (qwifi_mhi_cmd_ctxt *)pmem_alloc_pages_lowdma(qwifi_pages_for(cmd_bytes));
        for (uint32_t i = 0; i < QWIFI_MHI_EVENT_RINGS; ++i)
            g_qwifi_event_ring[i] = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_RING_BYTES));
        g_qwifi_cmd_ring = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_RING_BYTES));
        g_qwifi_chan_count = chan_count;
    }

    if (!g_qwifi_chan_ctxt || !g_qwifi_event_ctxt || !g_qwifi_cmd_ctxt || !g_qwifi_cmd_ring)
    {
        terminal_print("[K:QWIFI] MHI minimal context alloc failed");
        terminal_flush_log();
        return;
    }
    for (uint32_t i = 0; i < QWIFI_MHI_EVENT_RINGS; ++i)
    {
        if (!g_qwifi_event_ring[i])
        {
            terminal_print("[K:QWIFI] MHI event ring alloc failed index=");
            terminal_print_inline_hex64(i);
            terminal_flush_log();
            return;
        }
    }

    if (g_qwifi_chan_count != 0u)
        chan_bytes = sizeof(qwifi_mhi_chan_ctxt) * (uint64_t)g_qwifi_chan_count;

    qwifi_zero(g_qwifi_chan_ctxt, chan_bytes);
    qwifi_zero(g_qwifi_event_ctxt, event_bytes);
    qwifi_zero(g_qwifi_cmd_ctxt, cmd_bytes);
    for (uint32_t i = 0; i < QWIFI_MHI_EVENT_RINGS; ++i)
    {
        qwifi_zero(g_qwifi_event_ring[i], QWIFI_MHI_RING_BYTES);
        g_qwifi_event_read_index[i] = 0u;
    }
    qwifi_zero(g_qwifi_cmd_ring, QWIFI_MHI_RING_BYTES);

    chan_pa = pmem_virt_to_phys(g_qwifi_chan_ctxt);
    event_pa = pmem_virt_to_phys(g_qwifi_event_ctxt);
    cmd_pa = pmem_virt_to_phys(g_qwifi_cmd_ctxt);
    for (uint32_t i = 0; i < QWIFI_MHI_EVENT_RINGS; ++i)
        event_ring_pa[i] = pmem_virt_to_phys(g_qwifi_event_ring[i]);
    cmd_ring_pa = pmem_virt_to_phys(g_qwifi_cmd_ring);

    for (uint32_t i = 0; i < QWIFI_MHI_EVENT_RINGS; ++i)
    {
        g_qwifi_event_ctxt[i].ertype = QWIFI_MHI_ER_TYPE_VALID;
        g_qwifi_event_ctxt[i].msivec = i + 1u;
        g_qwifi_event_ctxt[i].rbase = event_ring_pa[i];
        g_qwifi_event_ctxt[i].rlen = (i == 0u) ?
                                     (QWIFI_MHI_EVENT0_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES) :
                                     (QWIFI_MHI_EVENT1_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES);
        g_qwifi_event_ctxt[i].rp = event_ring_pa[i];
        g_qwifi_event_ctxt[i].wp = event_ring_pa[i] + g_qwifi_event_ctxt[i].rlen - QWIFI_MHI_EVENT_ELEMENT_BYTES;
    }

    g_qwifi_cmd_ctxt[0].rbase = cmd_ring_pa;
    g_qwifi_cmd_ctxt[0].rlen = QWIFI_MHI_RING_BYTES;
    g_qwifi_cmd_ctxt[0].rp = cmd_ring_pa;
    g_qwifi_cmd_ctxt[0].wp = cmd_ring_pa;

    asm_dma_clean_range(g_qwifi_chan_ctxt, chan_bytes);
    asm_dma_clean_range(g_qwifi_event_ctxt, event_bytes);
    asm_dma_clean_range(g_qwifi_cmd_ctxt, cmd_bytes);
    for (uint32_t i = 0; i < QWIFI_MHI_EVENT_RINGS; ++i)
        asm_dma_clean_range(g_qwifi_event_ring[i], QWIFI_MHI_RING_BYTES);
    asm_dma_clean_range(g_qwifi_cmd_ring, QWIFI_MHI_RING_BYTES);

    terminal_print("[K:QWIFI] MHI ctx alloc chan_pa=");
    terminal_print_inline_hex64(chan_pa);
    terminal_print(" event_pa=");
    terminal_print_inline_hex64(event_pa);
    terminal_print(" cmd_pa=");
    terminal_print_inline_hex64(cmd_pa);
    terminal_print(" ev0_pa=");
    terminal_print_inline_hex64(event_ring_pa[0]);
    terminal_print(" ev1_pa=");
    terminal_print_inline_hex64(event_ring_pa[1]);
    terminal_print(" cmdring_pa=");
    terminal_print_inline_hex64(cmd_ring_pa);
    terminal_flush_log();

    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_CCABAP_LOWER, ATH12K_CCABAP_HIGHER, chan_pa);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_ECABAP_LOWER, ATH12K_ECABAP_HIGHER, event_pa);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_CRCBAP_LOWER, ATH12K_CRCBAP_HIGHER, cmd_pa);

    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHICTRLBASE_LOWER, ATH12K_MHICTRLBASE_HIGHER, 0u);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHICTRLLIMIT_LOWER, ATH12K_MHICTRLLIMIT_HIGHER, 0xFFFFFFFFull);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHIDATABASE_LOWER, ATH12K_MHIDATABASE_HIGHER, 0u);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHIDATALIMIT_LOWER, ATH12K_MHIDATALIMIT_HIGHER, 0xFFFFFFFFull);

    new_mhicfg = (mhicfg & ~(ATH12K_MHICFG_NER_MASK | ATH12K_MHICFG_NHWER_MASK)) |
                 (QWIFI_MHI_EVENT_RINGS << 16);
    rc |= qwifi_mhi_write32(bar0_base, ATH12K_MHICFG, new_mhicfg);

    (void)pci_probe_config_read32(bar0_base + ATH12K_CCABAP_LOWER, &readback);
    terminal_print("[K:QWIFI] MHI ctx programmed rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" ccabap_lo=");
    terminal_print_inline_hex64(readback);
    (void)pci_probe_config_read32(bar0_base + ATH12K_MHICFG, &readback);
    terminal_print(" mhicfg=");
    terminal_print_inline_hex64(readback);
    terminal_flush_log();

    if (rc == 0)
        programmed = 1;
}

static void qwifi_mhi_dump_event_ring(uint32_t index, const char *tag)
{
    qwifi_mhi_ring_element *ring;
    uint64_t ring_bytes;
    uint32_t printed = 0;

    if (index >= QWIFI_MHI_EVENT_RINGS || !g_qwifi_event_ring[index])
        return;

    ring = (qwifi_mhi_ring_element *)g_qwifi_event_ring[index];
    ring_bytes = (index == 0u) ?
                 (QWIFI_MHI_EVENT0_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES) :
                 (QWIFI_MHI_EVENT1_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES);

    asm_dma_invalidate_range(g_qwifi_event_ring[index], ring_bytes);
    if (g_qwifi_event_ctxt)
        asm_dma_invalidate_range(&g_qwifi_event_ctxt[index], sizeof(g_qwifi_event_ctxt[index]));

    terminal_print("[K:QWIFI] MHI event ring dump ");
    terminal_print(tag ? tag : "?");
    terminal_print(" idx=");
    terminal_print_inline_hex64(index);
    terminal_print(" ctxt_rp=");
    terminal_print_inline_hex64(g_qwifi_event_ctxt ? g_qwifi_event_ctxt[index].rp : 0u);
    terminal_print(" ctxt_wp=");
    terminal_print_inline_hex64(g_qwifi_event_ctxt ? g_qwifi_event_ctxt[index].wp : 0u);
    terminal_flush_log();

    for (uint32_t i = 0; i < (uint32_t)(ring_bytes / QWIFI_MHI_TRE_BYTES) && printed < 12u; ++i)
    {
        uint64_t ptr = ring[i].ptr;
        uint32_t d0 = ring[i].dword0;
        uint32_t d1 = ring[i].dword1;
        uint32_t type = (d1 >> 16) & 0xFFu;
        uint32_t chan = (d1 >> 24) & 0xFFu;
        uint32_t code = (d0 >> 24) & 0xFFu;
        uint32_t len = d0 & 0xFFFFu;

        if (!ptr && !d0 && !d1)
            continue;

        terminal_print("[K:QWIFI] MHI ev i=");
        terminal_print_inline_hex64(i);
        terminal_print(" ptr=");
        terminal_print_inline_hex64(ptr);
        terminal_print(" d0=");
        terminal_print_inline_hex64(d0);
        terminal_print(" d1=");
        terminal_print_inline_hex64(d1);
        terminal_print(" type=");
        terminal_print_inline_hex64(type);
        terminal_print(" chan=");
        terminal_print_inline_hex64(chan);
        terminal_print(" code=");
        terminal_print_inline_hex64(code);
        terminal_print(" len=");
        terminal_print_inline_hex64(len);
        terminal_flush_log();
        printed++;
    }

    if (printed == 0u)
    {
        terminal_print("[K:QWIFI] MHI event ring empty idx=");
        terminal_print_inline_hex64(index);
        terminal_flush_log();
    }
}

static int qwifi_mhi_find_cmd_completion(uint64_t cmd_tre_phys, uint32_t *out_code)
{
    qwifi_mhi_ring_element *ring;
    uint64_t ring_bytes = QWIFI_MHI_EVENT0_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES;

    if (!g_qwifi_event_ring[0])
        return 0;

    ring = (qwifi_mhi_ring_element *)g_qwifi_event_ring[0];
    asm_dma_invalidate_range(g_qwifi_event_ring[0], ring_bytes);

    for (uint32_t i = 0; i < QWIFI_MHI_EVENT0_ELEMENTS; ++i)
    {
        uint32_t type = (ring[i].dword1 >> 16) & 0xFFu;
        if (type != QWIFI_MHI_PKT_TYPE_CMD_COMPLETION_EVENT)
            continue;
        if (ring[i].ptr != cmd_tre_phys)
            continue;

        if (out_code)
            *out_code = (ring[i].dword0 >> 24) & 0xFFu;
        return 1;
    }

    return 0;
}

static int qwifi_mhi_send_start_channel_cmd(uint64_t bar0_base, uint32_t chid)
{
    qwifi_mhi_ring_element *cmd_ring = (qwifi_mhi_ring_element *)g_qwifi_cmd_ring;
    uint64_t cmd_ring_pa;
    uint64_t cmd_tre_pa;
    uint64_t cmd_wp_pa;
    uint32_t cmd_index;
    uint32_t code = 0;
    uint32_t poll;
    int rc;

    if (!cmd_ring || !g_qwifi_cmd_ctxt)
        return 0;

    cmd_ring_pa = pmem_virt_to_phys(g_qwifi_cmd_ring);
    cmd_index = g_qwifi_cmd_wp_index % QWIFI_MHI_CMD_RING_ELEMENTS;
    cmd_tre_pa = cmd_ring_pa + (uint64_t)cmd_index * QWIFI_MHI_TRE_BYTES;

    cmd_ring[cmd_index].ptr = 0u;
    cmd_ring[cmd_index].dword0 = 0u;
    cmd_ring[cmd_index].dword1 = ((chid & 0xFFu) << 24) | (QWIFI_MHI_CMD_START_CHAN << 16);
    asm_dma_clean_range(&cmd_ring[cmd_index], sizeof(cmd_ring[cmd_index]));

    g_qwifi_cmd_wp_index = (g_qwifi_cmd_wp_index + 1u) % QWIFI_MHI_CMD_RING_ELEMENTS;
    cmd_wp_pa = cmd_ring_pa + (uint64_t)g_qwifi_cmd_wp_index * QWIFI_MHI_TRE_BYTES;
    g_qwifi_cmd_ctxt[0].wp = cmd_wp_pa;
    asm_dma_clean_range(g_qwifi_cmd_ctxt, sizeof(*g_qwifi_cmd_ctxt));

    rc = qwifi_mhi_write64_pair(bar0_base, ATH12K_CRDB_LOWER, ATH12K_CRDB_HIGHER, cmd_wp_pa);
    terminal_print("[K:QWIFI] MHI START_CHAN doorbell ch=");
    terminal_print_inline_hex64(chid);
    terminal_print(" idx=");
    terminal_print_inline_hex64(cmd_index);
    terminal_print(" wp=");
    terminal_print_inline_hex64(cmd_wp_pa);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    for (poll = 0; poll < 2000000u; ++poll)
    {
        if (qwifi_mhi_find_cmd_completion(cmd_tre_pa, &code))
            break;
        asm_relax();
    }

    terminal_print("[K:QWIFI] MHI START_CHAN result ch=");
    terminal_print_inline_hex64(chid);
    terminal_print(" poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" code=");
    terminal_print_inline_hex64(code);
    terminal_print(" found=");
    terminal_print_inline_hex64(poll < 2000000u ? 1u : 0u);
    terminal_flush_log();

    return poll < 2000000u && code == QWIFI_MHI_EV_CC_SUCCESS;
}

static uint32_t qwifi_mhi_find_rx_buf_index(uint64_t buf_pa)
{
    for (uint32_t i = 0; i < QWIFI_MHI_IPCR_RX_PREPOST; ++i)
    {
        if (g_qwifi_ipcr_rx_buf_phys[i] == buf_pa)
            return i;
    }

    return 0xFFFFFFFFu;
}

static uint32_t qwifi_mhi_find_rx_tre_index(uint64_t tre_pa)
{
    uint64_t ring_pa;
    uint64_t ring_bytes = QWIFI_MHI_IPCR_ELEMENTS * QWIFI_MHI_TRE_BYTES;
    uint64_t off;

    if (!g_qwifi_ipcr_rx_ring)
        return 0xFFFFFFFFu;

    ring_pa = pmem_virt_to_phys(g_qwifi_ipcr_rx_ring);
    if (tre_pa < ring_pa || tre_pa >= ring_pa + ring_bytes)
        return 0xFFFFFFFFu;

    off = tre_pa - ring_pa;
    if ((off % QWIFI_MHI_TRE_BYTES) != 0u)
        return 0xFFFFFFFFu;

    return (uint32_t)(off / QWIFI_MHI_TRE_BYTES);
}

static int qwifi_mhi_send_qrtr_resume_tx(uint64_t bar0_base,
                                         uint32_t chdboff,
                                         uint32_t remote_node,
                                         uint32_t remote_port,
                                         uint32_t local_node,
                                         uint32_t local_port);

static void qwifi_dump_ipcr_payload(const void *buf, uint64_t len, const char *tag)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint64_t dump_len = len;

    if (!p)
        return;

    if (dump_len > 96u)
        dump_len = 96u;

    terminal_print("[K:QWIFI] IPCR payload ");
    terminal_print(tag ? tag : "?");
    terminal_print(" len=");
    terminal_print_inline_hex64(len);
    terminal_print(" dump=");
    terminal_print_inline_hex64(dump_len);
    terminal_flush_log();

    for (uint64_t i = 0; i < dump_len; i += 16u)
    {
        terminal_print("[K:QWIFI] IPCR bytes +");
        terminal_print_inline_hex64(i);
        terminal_print(":");
        for (uint64_t j = 0; j < 16u && i + j < dump_len; ++j)
        {
            terminal_print(" ");
            terminal_print_inline_hex64(p[i + j]);
        }
        terminal_flush_log();
    }

    if (len >= sizeof(qwifi_qrtr_hdr_v1))
    {
        const qwifi_qrtr_hdr_v1 *hdr = (const qwifi_qrtr_hdr_v1 *)p;
        terminal_print("[K:QWIFI] QRTR hdr ver=");
        terminal_print_inline_hex64(hdr->version);
        terminal_print(" type=");
        terminal_print_inline_hex64(hdr->type);
        terminal_print(" src=");
        terminal_print_inline_hex64(hdr->src_node_id);
        terminal_print(":");
        terminal_print_inline_hex64(hdr->src_port_id);
        terminal_print(" dst=");
        terminal_print_inline_hex64(hdr->dst_node_id);
        terminal_print(":");
        terminal_print_inline_hex64(hdr->dst_port_id);
        terminal_print(" confirm=");
        terminal_print_inline_hex64(hdr->confirm_rx);
        terminal_print(" size=");
        terminal_print_inline_hex64(hdr->size);
        terminal_flush_log();

        if (hdr->type == QWIFI_QRTR_TYPE_DATA &&
            hdr->confirm_rx &&
            g_qwifi_ipcr_bar0_base &&
            g_qwifi_ipcr_chdboff)
        {
            terminal_print("[K:QWIFI] QRTR DATA confirm_rx; sending RESUME_TX");
            terminal_flush_log();
            (void)qwifi_mhi_send_qrtr_resume_tx(g_qwifi_ipcr_bar0_base,
                                                g_qwifi_ipcr_chdboff,
                                                hdr->src_node_id,
                                                hdr->src_port_id,
                                                hdr->dst_node_id,
                                                hdr->dst_port_id);
        }

        if (hdr->type != QWIFI_QRTR_TYPE_DATA &&
            hdr->size >= sizeof(qwifi_qrtr_ctrl_pkt) &&
            len >= sizeof(qwifi_qrtr_hdr_v1) + sizeof(qwifi_qrtr_ctrl_pkt))
        {
            const qwifi_qrtr_ctrl_pkt *ctrl = (const qwifi_qrtr_ctrl_pkt *)(p + sizeof(qwifi_qrtr_hdr_v1));
            if (hdr->src_node_id != 0u && hdr->src_node_id != QWIFI_QRTR_NODE_BCAST)
                g_qwifi_qrtr_remote_node = hdr->src_node_id;
            terminal_print("[K:QWIFI] QRTR ctrl cmd=");
            terminal_print_inline_hex64(ctrl->cmd);
            if (ctrl->cmd == QWIFI_QRTR_TYPE_NEW_SERVER ||
                ctrl->cmd == QWIFI_QRTR_TYPE_DEL_SERVER ||
                ctrl->cmd == QWIFI_QRTR_TYPE_NEW_LOOKUP ||
                ctrl->cmd == QWIFI_QRTR_TYPE_DEL_LOOKUP)
            {
                terminal_print(" svc=");
                terminal_print_inline_hex64(ctrl->service);
                terminal_print(" inst=");
                terminal_print_inline_hex64(ctrl->instance);
                terminal_print(" node=");
                terminal_print_inline_hex64(ctrl->node);
                terminal_print(" port=");
                terminal_print_inline_hex64(ctrl->port);
            }
            else if (ctrl->cmd == QWIFI_QRTR_TYPE_HELLO ||
                     ctrl->cmd == QWIFI_QRTR_TYPE_BYE)
            {
                terminal_print(" node=");
                terminal_print_inline_hex64(ctrl->node);
                terminal_print(" port=");
                terminal_print_inline_hex64(ctrl->port);
            }
            else if (ctrl->cmd == QWIFI_QRTR_TYPE_RESUME_TX)
            {
                terminal_print(" client=");
                terminal_print_inline_hex64(ctrl->service);
                terminal_print(":");
                terminal_print_inline_hex64(ctrl->instance);
                g_qwifi_qrtr_tx_pending = 0u;
            }
            terminal_flush_log();

            if (ctrl->cmd == QWIFI_QRTR_TYPE_NEW_SERVER &&
                ctrl->service == QWIFI_QRTR_WLANFW_SERVICE &&
                ctrl->instance == QWIFI_QRTR_WLANFW_INSTANCE_WCN7850)
            {
                g_qwifi_qrtr_wlan_node = ctrl->node ? ctrl->node : hdr->src_node_id;
                g_qwifi_qrtr_wlan_port = ctrl->port;
                terminal_print("[K:QWIFI] WLANFW QRTR service found node=");
                terminal_print_inline_hex64(g_qwifi_qrtr_wlan_node);
                terminal_print(" port=");
                terminal_print_inline_hex64(g_qwifi_qrtr_wlan_port);
                terminal_flush_log();
            }
        }

        if (hdr->type == QWIFI_QRTR_TYPE_DATA &&
            hdr->size >= sizeof(qwifi_qmi_hdr) &&
            len >= sizeof(qwifi_qrtr_hdr_v1) + sizeof(qwifi_qmi_hdr))
        {
            const qwifi_qmi_hdr *qmi = (const qwifi_qmi_hdr *)(p + sizeof(qwifi_qrtr_hdr_v1));
            uint64_t payload_off = sizeof(qwifi_qrtr_hdr_v1) + sizeof(qwifi_qmi_hdr);
            uint64_t payload_end = sizeof(qwifi_qrtr_hdr_v1) + (uint64_t)hdr->size;
            terminal_print("[K:QWIFI] QMI hdr type=");
            terminal_print_inline_hex64(qmi->type);
            terminal_print(" txn=");
            terminal_print_inline_hex64(qmi->txn_id);
            terminal_print(" msg=");
            terminal_print_inline_hex64(qmi->msg_id);
            terminal_print(" len=");
            terminal_print_inline_hex64(qmi->msg_len);
            terminal_flush_log();

            if (qmi->type == QWIFI_QMI_RESPONSE)
            {
                g_qwifi_qmi_last_resp_msg = qmi->msg_id;
                g_qwifi_qmi_last_resp_txn = qmi->txn_id;
                g_qwifi_qmi_last_resp_result = 0xFFFFu;
                g_qwifi_qmi_last_resp_error = 0xFFFFu;
            }

            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_PHY_CAP &&
                qmi->msg_id == QWIFI_QMI_WLANFW_PHY_CAP_REQ)
            {
                g_qwifi_qmi_phy_cap_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW PHY_CAP response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_IND_REGISTER &&
                qmi->msg_id == QWIFI_QMI_WLANFW_IND_REGISTER_REQ)
            {
                g_qwifi_qmi_ind_register_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW IND_REGISTER response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_HOST_CAP &&
                qmi->msg_id == QWIFI_QMI_WLANFW_HOST_CAP_REQ)
            {
                g_qwifi_qmi_host_cap_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW HOST_CAP response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_INDICATION &&
                qmi->msg_id == QWIFI_QMI_WLANFW_REQUEST_MEM_IND)
            {
                g_qwifi_qmi_request_mem_ind_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW REQUEST_MEM indication seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_RESPOND_MEM &&
                qmi->msg_id == QWIFI_QMI_WLANFW_RESPOND_MEM_REQ)
            {
                g_qwifi_qmi_respond_mem_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW RESPOND_MEM response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_INDICATION &&
                qmi->msg_id == QWIFI_QMI_WLANFW_FW_MEM_READY_IND)
            {
                g_qwifi_qmi_fw_mem_ready_ind_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW FW_MEM_READY indication seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_INDICATION &&
                qmi->msg_id == QWIFI_QMI_WLANFW_FW_READY_IND)
            {
                g_qwifi_qmi_fw_ready_ind_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW FW_READY indication seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_TARGET_CAP &&
                qmi->msg_id == QWIFI_QMI_WLANFW_CAP_REQ)
            {
                g_qwifi_qmi_target_cap_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW TARGET_CAP response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->msg_id == QWIFI_QMI_WLANFW_BDF_DOWNLOAD_REQ)
            {
                g_qwifi_qmi_bdf_download_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW BDF_DOWNLOAD response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_M3_INFO &&
                qmi->msg_id == QWIFI_QMI_WLANFW_M3_INFO_REQ)
            {
                g_qwifi_qmi_m3_info_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW M3_INFO response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_WLAN_INI &&
                qmi->msg_id == QWIFI_QMI_WLANFW_WLAN_INI_REQ)
            {
                g_qwifi_qmi_wlan_ini_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW WLAN_INI response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_WLAN_CFG &&
                qmi->msg_id == QWIFI_QMI_WLANFW_WLAN_CFG_REQ)
            {
                g_qwifi_qmi_wlan_cfg_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW WLAN_CFG response seen");
                terminal_flush_log();
            }
            if (qmi->type == QWIFI_QMI_RESPONSE &&
                qmi->txn_id == QWIFI_QMI_TXN_WLAN_MODE &&
                qmi->msg_id == QWIFI_QMI_WLANFW_WLAN_MODE_REQ)
            {
                g_qwifi_qmi_wlan_mode_resp_seen = 1u;
                terminal_print("[K:QWIFI] QMI WLANFW WLAN_MODE response seen");
                terminal_flush_log();
            }

            for (uint32_t tlv = 0; payload_off + 3u <= payload_end && payload_off + 3u <= len && tlv < 8u; ++tlv)
            {
                uint8_t tlv_type = p[payload_off];
                uint16_t tlv_len = (uint16_t)p[payload_off + 1u] | ((uint16_t)p[payload_off + 2u] << 8);
                uint64_t value_off = payload_off + 3u;
                terminal_print("[K:QWIFI] QMI TLV type=");
                terminal_print_inline_hex64(tlv_type);
                terminal_print(" len=");
                terminal_print_inline_hex64(tlv_len);
                if (value_off + tlv_len > payload_end || value_off + tlv_len > len)
                {
                    terminal_print(" [truncated]");
                    terminal_flush_log();
                    break;
                }
                if (tlv_len >= 1u)
                {
                    terminal_print(" v8=");
                    terminal_print_inline_hex64(p[value_off]);
                }
                if (tlv_len >= 2u)
                {
                    uint16_t v16 = qwifi_read_le16(p + value_off);
                    terminal_print(" v16=");
                    terminal_print_inline_hex64(v16);
                }
                if (tlv_len >= 4u)
                {
                    uint16_t a = qwifi_read_le16(p + value_off);
                    uint16_t b = qwifi_read_le16(p + value_off + 2u);
                    terminal_print(" v0=");
                    terminal_print_inline_hex64(a);
                    terminal_print(" v1=");
                    terminal_print_inline_hex64(b);
                }
                terminal_flush_log();
                if (qmi->type == QWIFI_QMI_INDICATION &&
                    qmi->msg_id == QWIFI_QMI_WLANFW_REQUEST_MEM_IND &&
                    tlv_type == 0x01u)
                    qwifi_parse_request_mem_tlv(p + value_off, tlv_len);
                if (qmi->type == QWIFI_QMI_RESPONSE &&
                    tlv_type == 0x02u &&
                    tlv_len >= 4u)
                {
                    g_qwifi_qmi_last_resp_result = qwifi_read_le16(p + value_off);
                    g_qwifi_qmi_last_resp_error = qwifi_read_le16(p + value_off + 2u);
                    terminal_print("[K:QWIFI] QMI resp result msg=");
                    terminal_print_inline_hex64(qmi->msg_id);
                    terminal_print(" txn=");
                    terminal_print_inline_hex64(qmi->txn_id);
                    terminal_print(" result=");
                    terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
                    terminal_print(" error=");
                    terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
                    terminal_flush_log();
                }
                if (qmi->type == QWIFI_QMI_RESPONSE &&
                    qmi->msg_id == QWIFI_QMI_WLANFW_CAP_REQ &&
                    tlv_type == 0x10u &&
                    tlv_len >= 8u)
                {
                    g_qwifi_target_chip_id = qwifi_read_le32(p + value_off);
                    g_qwifi_target_chip_family = qwifi_read_le32(p + value_off + 4u);
                    terminal_print("[K:QWIFI] TARGET_CAP chip_id=");
                    terminal_print_inline_hex64(g_qwifi_target_chip_id);
                    terminal_print(" family=");
                    terminal_print_inline_hex64(g_qwifi_target_chip_family);
                    terminal_flush_log();
                }
                if (qmi->type == QWIFI_QMI_RESPONSE &&
                    qmi->msg_id == QWIFI_QMI_WLANFW_CAP_REQ &&
                    tlv_type == 0x11u &&
                    tlv_len >= 4u)
                {
                    g_qwifi_target_board_id = qwifi_read_le32(p + value_off);
                    terminal_print("[K:QWIFI] TARGET_CAP board_id=");
                    terminal_print_inline_hex64(g_qwifi_target_board_id);
                    terminal_flush_log();
                }
                if (qmi->type == QWIFI_QMI_RESPONSE &&
                    qmi->msg_id == QWIFI_QMI_WLANFW_CAP_REQ &&
                    tlv_type == 0x12u &&
                    tlv_len >= 4u)
                {
                    g_qwifi_target_soc_id = qwifi_read_le32(p + value_off);
                    terminal_print("[K:QWIFI] TARGET_CAP soc_id=");
                    terminal_print_inline_hex64(g_qwifi_target_soc_id);
                    terminal_flush_log();
                }
                payload_off = value_off + tlv_len;
            }
        }
    }
}

static void qwifi_mhi_repost_ipcr_rx_buf(uint32_t rx_index);

static void qwifi_mhi_dump_ipcr_events(const char *tag)
{
    qwifi_mhi_ring_element *ring;
    uint64_t ring_bytes = QWIFI_MHI_EVENT1_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES;
    uint64_t ring_pa;
    uint32_t rp_index = 0;
    uint32_t idx;
    uint32_t printed = 0;
    int have_rp_index = 0;

    if (!g_qwifi_event_ring[1])
        return;

    ring = (qwifi_mhi_ring_element *)g_qwifi_event_ring[1];
    ring_pa = pmem_virt_to_phys(g_qwifi_event_ring[1]);
    asm_dma_invalidate_range(g_qwifi_event_ring[1], ring_bytes);
    if (g_qwifi_event_ctxt)
    {
        asm_dma_invalidate_range(&g_qwifi_event_ctxt[1], sizeof(g_qwifi_event_ctxt[1]));
        if (g_qwifi_event_ctxt[1].rp >= ring_pa &&
            g_qwifi_event_ctxt[1].rp < ring_pa + ring_bytes &&
            (((g_qwifi_event_ctxt[1].rp - ring_pa) % QWIFI_MHI_EVENT_ELEMENT_BYTES) == 0u))
        {
            rp_index = (uint32_t)((g_qwifi_event_ctxt[1].rp - ring_pa) / QWIFI_MHI_EVENT_ELEMENT_BYTES);
            have_rp_index = 1;
        }
    }

    terminal_print("[K:QWIFI] IPCR event scan ");
    terminal_print(tag ? tag : "?");
    terminal_print(" rp=");
    terminal_print_inline_hex64(g_qwifi_event_ctxt ? g_qwifi_event_ctxt[1].rp : 0u);
    terminal_print(" wp=");
    terminal_print_inline_hex64(g_qwifi_event_ctxt ? g_qwifi_event_ctxt[1].wp : 0u);
    terminal_print(" next=");
    terminal_print_inline_hex64(g_qwifi_event_read_index[1]);
    terminal_print(" rpidx=");
    terminal_print_inline_hex64(have_rp_index ? rp_index : 0xFFFFFFFFu);
    terminal_flush_log();

    if (!have_rp_index)
    {
        terminal_print("[K:QWIFI] IPCR event scan skipped: invalid RP");
        terminal_flush_log();
        return;
    }

    idx = g_qwifi_event_read_index[1] % QWIFI_MHI_EVENT1_ELEMENTS;
    for (uint32_t guard = 0; guard < QWIFI_MHI_EVENT1_ELEMENTS && idx != rp_index; ++guard)
    {
        uint64_t ptr = ring[idx].ptr;
        uint32_t d0 = ring[idx].dword0;
        uint32_t d1 = ring[idx].dword1;
        uint32_t type = (d1 >> 16) & 0xFFu;
        uint32_t chan = (d1 >> 24) & 0xFFu;
        uint32_t code = (d0 >> 24) & 0xFFu;
        uint32_t len = d0 & 0xFFFFu;

        if (!ptr && !d0 && !d1)
        {
            idx = (idx + 1u) % QWIFI_MHI_EVENT1_ELEMENTS;
            continue;
        }

        terminal_print("[K:QWIFI] IPCR ev i=");
        terminal_print_inline_hex64(idx);
        terminal_print(" ptr=");
        terminal_print_inline_hex64(ptr);
        terminal_print(" d0=");
        terminal_print_inline_hex64(d0);
        terminal_print(" d1=");
        terminal_print_inline_hex64(d1);
        terminal_print(" type=");
        terminal_print_inline_hex64(type);
        terminal_print(" chan=");
        terminal_print_inline_hex64(chan);
        terminal_print(" code=");
        terminal_print_inline_hex64(code);
        terminal_print(" len=");
        terminal_print_inline_hex64(len);
        terminal_flush_log();

        if (type == QWIFI_MHI_PKT_TYPE_TX_EVENT &&
            chan == QWIFI_MHI_IPCR_RX_CH &&
            (code == QWIFI_MHI_EV_CC_SUCCESS ||
             code == QWIFI_MHI_EV_CC_EOT ||
             code == QWIFI_MHI_EV_CC_EOB ||
             code == QWIFI_MHI_EV_CC_OVERFLOW))
        {
            uint32_t tre_index = qwifi_mhi_find_rx_tre_index(ptr);
            uint32_t rx_index = 0xFFFFFFFFu;
            if (tre_index != 0xFFFFFFFFu)
            {
                qwifi_mhi_ring_element *rx_ring = (qwifi_mhi_ring_element *)g_qwifi_ipcr_rx_ring;
                rx_index = qwifi_mhi_find_rx_buf_index(rx_ring[tre_index].ptr);
            }
            if (rx_index == 0xFFFFFFFFu)
                rx_index = qwifi_mhi_find_rx_buf_index(ptr);
            if (rx_index != 0xFFFFFFFFu && g_qwifi_ipcr_rx_bufs[rx_index])
            {
                asm_dma_invalidate_range(g_qwifi_ipcr_rx_bufs[rx_index], QWIFI_MHI_IPCR_RX_BUF_BYTES);
                qwifi_dump_ipcr_payload(g_qwifi_ipcr_rx_bufs[rx_index], len, "rx");
                qwifi_mhi_repost_ipcr_rx_buf(rx_index);
            }
            else
            {
                terminal_print("[K:QWIFI] IPCR RX payload lookup miss tre=");
                terminal_print_inline_hex64(tre_index);
                terminal_print(" ptr=");
                terminal_print_inline_hex64(ptr);
                terminal_flush_log();
            }
        }

        printed++;
        idx = (idx + 1u) % QWIFI_MHI_EVENT1_ELEMENTS;
    }

    g_qwifi_event_read_index[1] = rp_index;
    if (g_qwifi_event_ctxt)
    {
        uint64_t new_wp = (rp_index == 0u) ?
                          (ring_pa + ring_bytes - QWIFI_MHI_EVENT_ELEMENT_BYTES) :
                          (ring_pa + (uint64_t)(rp_index - 1u) * QWIFI_MHI_EVENT_ELEMENT_BYTES);
        g_qwifi_event_ctxt[1].wp = new_wp;
        asm_dma_clean_range(&g_qwifi_event_ctxt[1], sizeof(g_qwifi_event_ctxt[1]));
    }

    if (printed == 0u)
    {
        terminal_print("[K:QWIFI] IPCR event scan empty");
        terminal_flush_log();
    }
}

static int qwifi_mhi_ipcr_has_new_events(void)
{
    uint64_t ring_bytes = QWIFI_MHI_EVENT1_ELEMENTS * QWIFI_MHI_EVENT_ELEMENT_BYTES;
    uint64_t ring_pa;
    uint32_t rp_index;

    if (!g_qwifi_event_ring[1] || !g_qwifi_event_ctxt)
        return 0;

    ring_pa = pmem_virt_to_phys(g_qwifi_event_ring[1]);
    asm_dma_invalidate_range(&g_qwifi_event_ctxt[1], sizeof(g_qwifi_event_ctxt[1]));
    if (g_qwifi_event_ctxt[1].rp < ring_pa ||
        g_qwifi_event_ctxt[1].rp >= ring_pa + ring_bytes ||
        (((g_qwifi_event_ctxt[1].rp - ring_pa) % QWIFI_MHI_EVENT_ELEMENT_BYTES) != 0u))
        return 0;

    rp_index = (uint32_t)((g_qwifi_event_ctxt[1].rp - ring_pa) / QWIFI_MHI_EVENT_ELEMENT_BYTES);
    return rp_index != (g_qwifi_event_read_index[1] % QWIFI_MHI_EVENT1_ELEMENTS);
}

static void qwifi_mhi_wait_for_ipcr_events_until(const char *stage, uint32_t rounds, const uint32_t *done_flag)
{
    for (uint32_t round = 0; round < rounds && (!done_flag || !*done_flag); ++round)
    {
        short_delay();
        if (qwifi_mhi_ipcr_has_new_events())
        {
            terminal_print("[K:QWIFI] IPCR watch event round=");
            terminal_print_inline_hex64(round);
            terminal_flush_log();
            qwifi_mhi_dump_ipcr_events(stage);
        }
        else if ((round & 0x3Fu) == 0u)
        {
            terminal_print("[K:QWIFI] IPCR watch idle round=");
            terminal_print_inline_hex64(round);
            terminal_flush_log();
        }
    }
}

static void qwifi_mhi_wait_for_wlanfw_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qrtr_wlan_port);
}

static void qwifi_mhi_wait_for_qmi_phy_cap_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_phy_cap_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_ind_register_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_ind_register_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_host_cap_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_host_cap_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_request_mem_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_request_mem_ind_seen);
}

static void qwifi_mhi_wait_for_qmi_respond_mem_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_respond_mem_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_fw_mem_ready_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_fw_mem_ready_ind_seen);
}

static void qwifi_mhi_wait_for_qmi_fw_ready_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_fw_ready_ind_seen);
}

static void qwifi_mhi_wait_for_qmi_target_cap_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_target_cap_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_bdf_download_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_bdf_download_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_m3_info_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_m3_info_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_wlan_ini_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_wlan_ini_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_wlan_cfg_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_wlan_cfg_resp_seen);
}

static void qwifi_mhi_wait_for_qmi_wlan_mode_events(const char *stage, uint32_t rounds)
{
    qwifi_mhi_wait_for_ipcr_events_until(stage, rounds, &g_qwifi_qmi_wlan_mode_resp_seen);
}

static int qwifi_qmi_last_response_ok(uint16_t msg_id, uint16_t txn_id, int check_txn)
{
    if (g_qwifi_qmi_last_resp_msg != msg_id)
        return 0;
    if (check_txn && g_qwifi_qmi_last_resp_txn != txn_id)
        return 0;
    return g_qwifi_qmi_last_resp_result == 0u;
}

static void qwifi_mhi_repost_ipcr_rx_buf(uint32_t rx_index)
{
    qwifi_mhi_ring_element *rx_ring = (qwifi_mhi_ring_element *)g_qwifi_ipcr_rx_ring;
    void *buf;
    uint64_t buf_pa;
    uint64_t rx_ring_pa;
    uint64_t wp_pa;
    uint32_t idx;
    int rc;

    if (!rx_ring || !g_qwifi_chan_ctxt || rx_index >= QWIFI_MHI_IPCR_RX_PREPOST ||
        !g_qwifi_ipcr_bar0_base || !g_qwifi_ipcr_chdboff)
        return;

    buf = g_qwifi_ipcr_rx_bufs[rx_index];
    if (!buf)
        return;

    buf_pa = pmem_virt_to_phys(buf);
    qwifi_zero(buf, QWIFI_MHI_IPCR_RX_BUF_BYTES);
    asm_dma_invalidate_range(buf, QWIFI_MHI_IPCR_RX_BUF_BYTES);

    rx_ring_pa = pmem_virt_to_phys(g_qwifi_ipcr_rx_ring);
    idx = g_qwifi_ipcr_rx_wp_index % QWIFI_MHI_IPCR_ELEMENTS;
    rx_ring[idx].ptr = buf_pa;
    rx_ring[idx].dword0 = QWIFI_MHI_IPCR_RX_BUF_BYTES & 0xFFFFu;
    rx_ring[idx].dword1 = qwifi_mhi_transfer_dword1();
    asm_dma_clean_range(&rx_ring[idx], sizeof(rx_ring[idx]));

    g_qwifi_ipcr_rx_wp_index = (g_qwifi_ipcr_rx_wp_index + 1u) % QWIFI_MHI_IPCR_ELEMENTS;
    wp_pa = rx_ring_pa + (uint64_t)g_qwifi_ipcr_rx_wp_index * QWIFI_MHI_TRE_BYTES;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].wp = wp_pa;
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH], sizeof(g_qwifi_chan_ctxt[0]));

    rc = qwifi_mhi_ring_channel_db(g_qwifi_ipcr_bar0_base, g_qwifi_ipcr_chdboff, QWIFI_MHI_IPCR_RX_CH, wp_pa);
    if (rc != 0)
    {
        terminal_print("[K:QWIFI] IPCR RX repost failed idx=");
        terminal_print_inline_hex64(rx_index);
        terminal_print(" rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();
    }
}

static int qwifi_mhi_queue_ipcr_rx(uint64_t bar0_base, uint32_t chdboff)
{
    qwifi_mhi_ring_element *rx_ring = (qwifi_mhi_ring_element *)g_qwifi_ipcr_rx_ring;
    uint64_t rx_ring_pa;
    uint64_t wp_pa;
    uint32_t queued = 0;
    int rc;

    if (!rx_ring || !g_qwifi_chan_ctxt)
        return 0;

    rx_ring_pa = pmem_virt_to_phys(g_qwifi_ipcr_rx_ring);

    for (uint32_t i = 0; i < QWIFI_MHI_IPCR_RX_PREPOST; ++i)
    {
        uint32_t idx = g_qwifi_ipcr_rx_wp_index % QWIFI_MHI_IPCR_ELEMENTS;
        void *buf = g_qwifi_ipcr_rx_bufs[i];
        uint64_t buf_pa;

        if (!buf)
        {
            buf = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_IPCR_RX_BUF_BYTES));
            g_qwifi_ipcr_rx_bufs[i] = buf;
        }
        if (!buf)
            break;

        qwifi_zero(buf, QWIFI_MHI_IPCR_RX_BUF_BYTES);
        asm_dma_invalidate_range(buf, QWIFI_MHI_IPCR_RX_BUF_BYTES);
        buf_pa = pmem_virt_to_phys(buf);
        g_qwifi_ipcr_rx_buf_phys[i] = buf_pa;

        rx_ring[idx].ptr = buf_pa;
        rx_ring[idx].dword0 = QWIFI_MHI_IPCR_RX_BUF_BYTES & 0xFFFFu;
        rx_ring[idx].dword1 = qwifi_mhi_transfer_dword1();
        asm_dma_clean_range(&rx_ring[idx], sizeof(rx_ring[idx]));

        g_qwifi_ipcr_rx_wp_index = (g_qwifi_ipcr_rx_wp_index + 1u) % QWIFI_MHI_IPCR_ELEMENTS;
        queued++;
    }

    wp_pa = rx_ring_pa + (uint64_t)g_qwifi_ipcr_rx_wp_index * QWIFI_MHI_TRE_BYTES;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].wp = wp_pa;
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH], sizeof(g_qwifi_chan_ctxt[0]));

    rc = qwifi_mhi_ring_channel_db(bar0_base, chdboff, QWIFI_MHI_IPCR_RX_CH, wp_pa);
    terminal_print("[K:QWIFI] IPCR RX queued=");
    terminal_print_inline_hex64(queued);
    terminal_print(" wp=");
    terminal_print_inline_hex64(wp_pa);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();

    return queued != 0u && rc == 0;
}

static uint32_t qwifi_qrtr_dst_node(void)
{
    return g_qwifi_qrtr_remote_node ? g_qwifi_qrtr_remote_node : QWIFI_QRTR_NODE_BCAST;
}

static uint32_t qwifi_qrtr_next_confirm_rx(void)
{
    if (g_qwifi_qrtr_tx_pending < 0xFFFFFFFFu)
        g_qwifi_qrtr_tx_pending++;

    if (g_qwifi_qrtr_tx_pending == QWIFI_QRTR_TX_FLOW_LOW)
        return 1u;

    /*
     * Linux blocks at the high watermark until RESUME_TX arrives. We cannot
     * sleep this early in bringup, so keep marking packets as confirmable.
     */
    return g_qwifi_qrtr_tx_pending >= QWIFI_QRTR_TX_FLOW_HIGH ? 1u : 0u;
}

static uint32_t qwifi_build_qrtr_control_to(void *buf,
                                            uint32_t cap,
                                            uint32_t type,
                                            uint32_t service,
                                            uint32_t instance,
                                            uint32_t src_node,
                                            uint32_t dst_node)
{
    qwifi_qrtr_hdr_v1 hdr;
    qwifi_qrtr_ctrl_pkt ctrl;
    uint32_t off = 0;

    if (!buf || cap < sizeof(hdr) + sizeof(ctrl))
        return 0;

    qwifi_zero(&hdr, sizeof(hdr));
    qwifi_zero(&ctrl, sizeof(ctrl));

    hdr.version = QWIFI_QRTR_PROTO_VER_1;
    hdr.type = type;
    hdr.src_node_id = src_node;
    hdr.src_port_id = QWIFI_QRTR_PORT_CTRL;
    hdr.confirm_rx = 0u;
    hdr.size = sizeof(ctrl);
    hdr.dst_node_id = dst_node;
    hdr.dst_port_id = QWIFI_QRTR_PORT_CTRL;

    ctrl.cmd = type;
    ctrl.service = service;
    ctrl.instance = instance;

    qwifi_copy_to_buf(buf, &hdr, sizeof(hdr));
    off += sizeof(hdr);
    qwifi_copy_to_buf((uint8_t *)buf + off, &ctrl, sizeof(ctrl));
    off += sizeof(ctrl);
    return off;
}

static uint32_t qwifi_build_qrtr_resume_tx(void *buf,
                                           uint32_t cap,
                                           uint32_t remote_node,
                                           uint32_t remote_port,
                                           uint32_t local_node,
                                           uint32_t local_port)
{
    qwifi_qrtr_hdr_v1 hdr;
    qwifi_qrtr_ctrl_pkt ctrl;
    uint32_t off = 0;

    if (!buf || cap < sizeof(hdr) + sizeof(ctrl))
        return 0;

    qwifi_zero(&hdr, sizeof(hdr));
    qwifi_zero(&ctrl, sizeof(ctrl));

    hdr.version = QWIFI_QRTR_PROTO_VER_1;
    hdr.type = QWIFI_QRTR_TYPE_RESUME_TX;
    hdr.src_node_id = local_node;
    hdr.src_port_id = local_port;
    hdr.confirm_rx = 0u;
    hdr.size = sizeof(ctrl);
    hdr.dst_node_id = remote_node;
    hdr.dst_port_id = remote_port;

    ctrl.cmd = QWIFI_QRTR_TYPE_RESUME_TX;
    ctrl.service = local_node;
    ctrl.instance = local_port;

    qwifi_copy_to_buf(buf, &hdr, sizeof(hdr));
    off += sizeof(hdr);
    qwifi_copy_to_buf((uint8_t *)buf + off, &ctrl, sizeof(ctrl));
    off += sizeof(ctrl);
    return off;
}

static int qwifi_mhi_send_qrtr_control_to(uint64_t bar0_base,
                                          uint32_t chdboff,
                                          uint32_t type,
                                          uint32_t service,
                                          uint32_t instance,
                                          uint32_t src_node,
                                          uint32_t dst_node,
                                          const char *label)
{
    qwifi_mhi_ring_element *tx_ring = (qwifi_mhi_ring_element *)g_qwifi_ipcr_tx_ring;
    uint64_t tx_ring_pa;
    uint64_t wp_pa;
    void *packet;
    uint64_t packet_pa;
    uint32_t len;
    uint32_t idx;
    int rc;

    if (!tx_ring || !g_qwifi_chan_ctxt)
        return 0;

    packet = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_IPCR_TX_BUF_BYTES));
    if (!packet)
    {
        terminal_print("[K:QWIFI] QRTR control alloc failed");
        terminal_flush_log();
        return 0;
    }

    qwifi_zero(packet, QWIFI_MHI_IPCR_TX_BUF_BYTES);
    len = qwifi_build_qrtr_control_to(packet,
                                      QWIFI_MHI_IPCR_TX_BUF_BYTES,
                                      type,
                                      service,
                                      instance,
                                      src_node,
                                      dst_node);
    if (!len)
    {
        terminal_print("[K:QWIFI] QRTR control build failed");
        terminal_flush_log();
        return 0;
    }

    packet_pa = pmem_virt_to_phys(packet);
    asm_dma_clean_range(packet, len);

    tx_ring_pa = pmem_virt_to_phys(g_qwifi_ipcr_tx_ring);
    idx = g_qwifi_ipcr_tx_wp_index % QWIFI_MHI_IPCR_ELEMENTS;
    tx_ring[idx].ptr = packet_pa;
    tx_ring[idx].dword0 = len & 0xFFFFu;
    tx_ring[idx].dword1 = qwifi_mhi_transfer_dword1();
    asm_dma_clean_range(&tx_ring[idx], sizeof(tx_ring[idx]));

    g_qwifi_ipcr_tx_wp_index = (g_qwifi_ipcr_tx_wp_index + 1u) % QWIFI_MHI_IPCR_ELEMENTS;
    wp_pa = tx_ring_pa + (uint64_t)g_qwifi_ipcr_tx_wp_index * QWIFI_MHI_TRE_BYTES;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].wp = wp_pa;
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH], sizeof(g_qwifi_chan_ctxt[0]));

    terminal_print("[K:QWIFI] QRTR ");
    terminal_print(label ? label : "CTRL");
    terminal_print(" tx_pa=");
    terminal_print_inline_hex64(packet_pa);
    terminal_print(" len=");
    terminal_print_inline_hex64(len);
    terminal_print(" idx=");
    terminal_print_inline_hex64(idx);
    terminal_print(" type=");
    terminal_print_inline_hex64(type);
    terminal_print(" src=");
    terminal_print_inline_hex64(src_node);
    terminal_print(" dst=");
    terminal_print_inline_hex64(dst_node);
    terminal_print(" svc=");
    terminal_print_inline_hex64(service);
    terminal_print(" inst=");
    terminal_print_inline_hex64(instance);
    terminal_flush_log();

    rc = qwifi_mhi_ring_channel_db(bar0_base, chdboff, QWIFI_MHI_IPCR_TX_CH, wp_pa);
    terminal_print("[K:QWIFI] QRTR ");
    terminal_print(label ? label : "CTRL");
    terminal_print(" doorbell wp=");
    terminal_print_inline_hex64(wp_pa);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();

    return rc == 0;
}

static int qwifi_mhi_send_qrtr_resume_tx(uint64_t bar0_base,
                                         uint32_t chdboff,
                                         uint32_t remote_node,
                                         uint32_t remote_port,
                                         uint32_t local_node,
                                         uint32_t local_port)
{
    qwifi_mhi_ring_element *tx_ring = (qwifi_mhi_ring_element *)g_qwifi_ipcr_tx_ring;
    uint64_t tx_ring_pa;
    uint64_t wp_pa;
    void *packet;
    uint64_t packet_pa;
    uint32_t len;
    uint32_t idx;
    int rc;

    if (!tx_ring || !g_qwifi_chan_ctxt || !remote_node || !remote_port)
        return 0;

    packet = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_IPCR_TX_BUF_BYTES));
    if (!packet)
    {
        terminal_print("[K:QWIFI] QRTR RESUME_TX alloc failed");
        terminal_flush_log();
        return 0;
    }

    qwifi_zero(packet, QWIFI_MHI_IPCR_TX_BUF_BYTES);
    len = qwifi_build_qrtr_resume_tx(packet,
                                     QWIFI_MHI_IPCR_TX_BUF_BYTES,
                                     remote_node,
                                     remote_port,
                                     local_node,
                                     local_port);
    if (!len)
    {
        terminal_print("[K:QWIFI] QRTR RESUME_TX build failed");
        terminal_flush_log();
        return 0;
    }

    packet_pa = pmem_virt_to_phys(packet);
    asm_dma_clean_range(packet, len);

    tx_ring_pa = pmem_virt_to_phys(g_qwifi_ipcr_tx_ring);
    idx = g_qwifi_ipcr_tx_wp_index % QWIFI_MHI_IPCR_ELEMENTS;
    tx_ring[idx].ptr = packet_pa;
    tx_ring[idx].dword0 = len & 0xFFFFu;
    tx_ring[idx].dword1 = qwifi_mhi_transfer_dword1();
    asm_dma_clean_range(&tx_ring[idx], sizeof(tx_ring[idx]));

    g_qwifi_ipcr_tx_wp_index = (g_qwifi_ipcr_tx_wp_index + 1u) % QWIFI_MHI_IPCR_ELEMENTS;
    wp_pa = tx_ring_pa + (uint64_t)g_qwifi_ipcr_tx_wp_index * QWIFI_MHI_TRE_BYTES;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].wp = wp_pa;
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH], sizeof(g_qwifi_chan_ctxt[0]));

    terminal_print("[K:QWIFI] QRTR RESUME_TX tx_pa=");
    terminal_print_inline_hex64(packet_pa);
    terminal_print(" len=");
    terminal_print_inline_hex64(len);
    terminal_print(" idx=");
    terminal_print_inline_hex64(idx);
    terminal_print(" local=");
    terminal_print_inline_hex64(local_node);
    terminal_print(":");
    terminal_print_inline_hex64(local_port);
    terminal_print(" remote=");
    terminal_print_inline_hex64(remote_node);
    terminal_print(":");
    terminal_print_inline_hex64(remote_port);
    terminal_flush_log();

    rc = qwifi_mhi_ring_channel_db(bar0_base, chdboff, QWIFI_MHI_IPCR_TX_CH, wp_pa);
    terminal_print("[K:QWIFI] QRTR RESUME_TX doorbell wp=");
    terminal_print_inline_hex64(wp_pa);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();

    return rc == 0;
}

static uint32_t qwifi_build_qrtr_qmi_request(void *buf,
                                             uint32_t cap,
                                             uint16_t msg_id,
                                             uint16_t txn_id,
                                             const void *payload,
                                             uint16_t payload_len)
{
    qwifi_qrtr_hdr_v1 hdr;
    qwifi_qmi_hdr qmi;
    uint32_t off = 0;

    if (!buf || !g_qwifi_qrtr_wlan_node || !g_qwifi_qrtr_wlan_port ||
        cap < sizeof(hdr) + sizeof(qmi) + payload_len)
        return 0;

    qwifi_zero(&hdr, sizeof(hdr));
    qwifi_zero(&qmi, sizeof(qmi));

    hdr.version = QWIFI_QRTR_PROTO_VER_1;
    hdr.type = QWIFI_QRTR_TYPE_DATA;
    hdr.src_node_id = QWIFI_QRTR_LOCAL_NODE;
    hdr.src_port_id = QWIFI_QRTR_LOCAL_PORT;
    hdr.confirm_rx = qwifi_qrtr_next_confirm_rx();
    hdr.size = sizeof(qmi) + payload_len;
    hdr.dst_node_id = g_qwifi_qrtr_wlan_node;
    hdr.dst_port_id = g_qwifi_qrtr_wlan_port;

    qmi.type = QWIFI_QMI_REQUEST;
    qmi.txn_id = txn_id;
    qmi.msg_id = msg_id;
    qmi.msg_len = payload_len;

    qwifi_copy_to_buf(buf, &hdr, sizeof(hdr));
    off += sizeof(hdr);
    qwifi_copy_to_buf((uint8_t *)buf + off, &qmi, sizeof(qmi));
    off += sizeof(qmi);
    if (payload && payload_len)
    {
        qwifi_copy_to_buf((uint8_t *)buf + off, payload, payload_len);
        off += payload_len;
    }
    return off;
}

static uint32_t qwifi_qmi_put_tlv_u8(uint8_t *buf, uint32_t cap, uint32_t off, uint8_t type, uint8_t value)
{
    if (!buf || off + 4u > cap)
        return 0;
    buf[off++] = type;
    buf[off++] = 1u;
    buf[off++] = 0u;
    buf[off++] = value;
    return off;
}

static uint32_t qwifi_qmi_put_tlv_u16(uint8_t *buf, uint32_t cap, uint32_t off, uint8_t type, uint16_t value)
{
    if (!buf || off + 5u > cap)
        return 0;
    buf[off++] = type;
    buf[off++] = 2u;
    buf[off++] = 0u;
    buf[off++] = (uint8_t)(value & 0xFFu);
    buf[off++] = (uint8_t)((value >> 8) & 0xFFu);
    return off;
}

static uint32_t qwifi_qmi_put_tlv_u32(uint8_t *buf, uint32_t cap, uint32_t off, uint8_t type, uint32_t value)
{
    if (!buf || off + 7u > cap)
        return 0;
    buf[off++] = type;
    buf[off++] = 4u;
    buf[off++] = 0u;
    buf[off++] = (uint8_t)(value & 0xFFu);
    buf[off++] = (uint8_t)((value >> 8) & 0xFFu);
    buf[off++] = (uint8_t)((value >> 16) & 0xFFu);
    buf[off++] = (uint8_t)((value >> 24) & 0xFFu);
    return off;
}

static uint32_t qwifi_qmi_put_tlv_u64(uint8_t *buf, uint32_t cap, uint32_t off, uint8_t type, uint64_t value)
{
    if (!buf || off + 11u > cap)
        return 0;
    buf[off++] = type;
    buf[off++] = 8u;
    buf[off++] = 0u;
    for (uint32_t i = 0; i < 8u; ++i)
        buf[off++] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
    return off;
}

static uint32_t qwifi_qmi_put_tlv_bytes(uint8_t *buf,
                                        uint32_t cap,
                                        uint32_t off,
                                        uint8_t type,
                                        const uint8_t *value,
                                        uint16_t len)
{
    if (!buf || !value || off + 3u + len > cap)
        return 0;
    buf[off++] = type;
    buf[off++] = (uint8_t)(len & 0xFFu);
    buf[off++] = (uint8_t)((len >> 8) & 0xFFu);
    for (uint16_t i = 0; i < len; ++i)
        buf[off++] = value[i];
    return off;
}

static uint32_t qwifi_qmi_put_tlv_string(uint8_t *buf,
                                         uint32_t cap,
                                         uint32_t off,
                                         uint8_t type,
                                         const char *value)
{
    return qwifi_qmi_put_tlv_bytes(buf, cap, off, type, (const uint8_t *)value, (uint16_t)qwifi_cstr_len(value));
}

static uint32_t qwifi_qmi_put_tlv_data_u16_len(uint8_t *buf,
                                               uint32_t cap,
                                               uint32_t off,
                                               uint8_t type,
                                               const uint8_t *value,
                                               uint16_t len)
{
    if (!buf || (!value && len) || off + 5u + len > cap)
        return 0;

    buf[off++] = type;
    buf[off++] = (uint8_t)((len + 2u) & 0xFFu);
    buf[off++] = (uint8_t)(((len + 2u) >> 8) & 0xFFu);
    buf[off++] = (uint8_t)(len & 0xFFu);
    buf[off++] = (uint8_t)((len >> 8) & 0xFFu);
    for (uint16_t i = 0; i < len; ++i)
        buf[off++] = value[i];
    return off;
}

static uint32_t qwifi_qmi_put_tlv_records_u8_len(uint8_t *buf,
                                                 uint32_t cap,
                                                 uint32_t off,
                                                 uint8_t type,
                                                 const uint8_t *records,
                                                 uint8_t count,
                                                 uint8_t record_bytes)
{
    uint32_t data_len = (uint32_t)count * (uint32_t)record_bytes;
    uint32_t value_len = 1u + data_len;

    if (!buf || (!records && data_len) || off + 3u + value_len > cap)
        return 0;

    buf[off++] = type;
    buf[off++] = (uint8_t)(value_len & 0xFFu);
    buf[off++] = (uint8_t)((value_len >> 8) & 0xFFu);
    buf[off++] = count;
    for (uint32_t i = 0; i < data_len; ++i)
        buf[off++] = records[i];
    return off;
}

static uint16_t qwifi_build_ind_register_payload(uint8_t *buf, uint32_t cap)
{
    uint32_t off = 0;

    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x10u, 1u); /* fw_ready_enable */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x15u, QWIFI_QMI_WLANFW_CLIENT_ID);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x16u, 1u); /* request_mem_enable */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x17u, 1u); /* fw_mem_ready_enable */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x18u, 1u); /* fw_init_done_enable */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x1Bu, 1u); /* cal_done_enable */
    return (uint16_t)off;
}

static uint16_t qwifi_build_host_cap_payload(uint8_t *buf, uint32_t cap)
{
    uint8_t mlo_chip_info[18];
    uint32_t off = 0;

    qwifi_zero(mlo_chip_info, sizeof(mlo_chip_info));
    mlo_chip_info[0] = 0u; /* chip_id */
    mlo_chip_info[1] = 2u; /* num_local_links from PHY_CAP num_phy=2 */
    mlo_chip_info[2] = 0u; /* hw_link_id[0] */
    mlo_chip_info[3] = 1u; /* hw_link_id[1] */
    mlo_chip_info[4] = 1u; /* valid_mlo_link_id[0] */
    mlo_chip_info[5] = 1u; /* valid_mlo_link_id[1] */

    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x10u, 1u); /* num_clients */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x13u,
                               QWIFI_QMI_NM_MODEM_SLEEP_CLOCK_INTERNAL |
                                   QWIFI_QMI_NM_MODEM_HOST_CSTATE |
                                   QWIFI_QMI_NM_MODEM_PCIE_GLOBAL_RESET);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x14u, 1u); /* bdf_support */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x16u, 1u); /* m3_support */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x17u, 1u); /* m3_cache_support */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x1Au, 0u); /* cal_done */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x1Cu, 0u); /* mem_cfg_mode */
    if (!off)
        return 0;

    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x21u, 1u); /* mlo_capable */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u16(buf, cap, off, 0x22u, 0u); /* mlo_chip_id */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x23u, 0u); /* mlo_group_id */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u16(buf, cap, off, 0x24u, QWIFI_QMI_WCN7850_MAX_MLO_PEER);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x25u, 1u); /* mlo_num_chips */
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_bytes(buf, cap, off, 0x26u, mlo_chip_info, sizeof(mlo_chip_info));
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u64(buf, cap, off, 0x27u, QWIFI_QMI_WCN7850_FEATURE_LIST);
    return (uint16_t)off;
}

static int qwifi_alloc_requested_mem_segments(void)
{
    if (!g_qwifi_mem_seg_count || g_qwifi_mem_seg_count > QWIFI_QMI_MEM_MAX_SEG)
        return 0;

    for (uint32_t i = 0; i < g_qwifi_mem_seg_count; ++i)
    {
        uint32_t size = g_qwifi_mem_seg_size[i];
        void *buf;

        if (!size)
            return 0;

        if (!g_qwifi_mem_seg_virt[i])
        {
            buf = pmem_alloc_pages_lowdma(qwifi_pages_for(size));
            if (!buf)
            {
                terminal_print("[K:QWIFI] REQUEST_MEM alloc failed seg=");
                terminal_print_inline_hex64(i);
                terminal_print(" size=");
                terminal_print_inline_hex64(size);
                terminal_flush_log();
                return 0;
            }

            qwifi_zero(buf, qwifi_pages_for(size) << 12);
            asm_dma_clean_range(buf, qwifi_pages_for(size) << 12);
            g_qwifi_mem_seg_virt[i] = buf;
            g_qwifi_mem_seg_phys[i] = pmem_virt_to_phys(buf);
        }

        terminal_print("[K:QWIFI] REQUEST_MEM alloc seg=");
        terminal_print_inline_hex64(i);
        terminal_print(" pa=");
        terminal_print_inline_hex64(g_qwifi_mem_seg_phys[i]);
        terminal_print(" size=");
        terminal_print_inline_hex64(size);
        terminal_print(" type=");
        terminal_print_inline_hex64(g_qwifi_mem_seg_type[i]);
        terminal_flush_log();
    }

    return 1;
}

static uint16_t qwifi_build_respond_mem_payload(uint8_t *buf, uint32_t cap)
{
    uint32_t count = g_qwifi_mem_seg_count;
    uint32_t value_len = 1u + count * 17u;
    uint32_t off = 0;

    if (!buf || !count || count > QWIFI_QMI_MEM_MAX_SEG || cap < 3u + value_len)
        return 0;

    buf[off++] = 0x01u;
    buf[off++] = (uint8_t)(value_len & 0xFFu);
    buf[off++] = (uint8_t)((value_len >> 8) & 0xFFu);
    buf[off++] = (uint8_t)count;

    for (uint32_t i = 0; i < count; ++i)
    {
        qwifi_write_le64(buf + off, g_qwifi_mem_seg_phys[i]);
        off += 8u;
        qwifi_write_le32(buf + off, g_qwifi_mem_seg_size[i]);
        off += 4u;
        qwifi_write_le32(buf + off, g_qwifi_mem_seg_type[i]);
        off += 4u;
        buf[off++] = 0u; /* restore: fresh memory, not a restored target dump */
    }

    return (uint16_t)off;
}

static int qwifi_mhi_send_qmi_request(uint64_t bar0_base,
                                      uint32_t chdboff,
                                      uint16_t msg_id,
                                      uint16_t txn_id,
                                      const void *payload,
                                      uint16_t payload_len,
                                      const char *label);

typedef struct
{
    const uint8_t *data;
    uint32_t len;
    uint8_t bdf_type;
    uint32_t name_len;
    const char *selected_name;
    uint32_t rank;
} qwifi_board_match;

static uint32_t qwifi_align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

static int qwifi_board_name_matches_wcn7850_lenovo(const uint8_t *name, uint32_t name_len)
{
    return qwifi_ascii_contains(name, name_len, "bus=pci") &&
           qwifi_ascii_contains(name, name_len, "vendor=17cb") &&
           qwifi_ascii_contains(name, name_len, "device=1107") &&
           qwifi_ascii_contains(name, name_len, "subsystem-vendor=17aa") &&
           qwifi_ascii_contains(name, name_len, "subsystem-device=e0e9") &&
           qwifi_ascii_contains(name, name_len, "qmi-chip-id=2") &&
           qwifi_ascii_contains(name, name_len, "qmi-board-id=255");
}

static uint32_t qwifi_board_name_preferred_rank(const uint8_t *name, uint32_t name_len, const char **selected_name)
{
    if (selected_name)
        *selected_name = "fallback";

    if (qwifi_ascii_contains(name, name_len, "variant=LE_C590"))
    {
        if (selected_name)
            *selected_name = "variant=LE_C590";
        return 3u;
    }

    if (qwifi_ascii_contains(name, name_len, "variant=LES790"))
    {
        if (selected_name)
            *selected_name = "variant=LES790";
        return 2u;
    }

    if (qwifi_ascii_contains(name, name_len, "variant=LE_Altai"))
    {
        if (selected_name)
            *selected_name = "variant=LE_Altai";
        return 1u;
    }

    return 0u;
}

static int qwifi_board_name_matches_regdb(const uint8_t *name, uint32_t name_len)
{
    return (name_len == 7u && qwifi_bytes_match(name, name_len, "bus=pci")) ||
           qwifi_board_name_matches_wcn7850_lenovo(name, name_len);
}

static void qwifi_store_board_match(qwifi_board_match *match,
                                    const uint8_t *data,
                                    uint32_t data_len,
                                    uint8_t bdf_type,
                                    uint32_t name_len,
                                    const char *selected_name,
                                    uint32_t rank)
{
    match->data = data;
    match->len = data_len;
    match->bdf_type = bdf_type;
    match->name_len = name_len;
    match->selected_name = selected_name ? selected_name : "fallback";
    match->rank = rank;
}

static int qwifi_find_wcn7850_board2_ie(const boot_info *bi,
                                        uint32_t wanted_ie,
                                        qwifi_board_match *match)
{
    uint64_t board_phys = 0;
    uint64_t board_size64 = 0;
    const uint8_t *board;
    uint32_t board_size;
    uint32_t off;
    int have_fallback = 0;

    if (!match)
        return 0;

    match->data = 0;
    match->len = 0;
    match->bdf_type = QWIFI_QMI_BDF_TYPE_ELF;
    match->name_len = 0;
    match->selected_name = "none";
    match->rank = 0u;

    if (!qwifi_get_fw_blob(bi, BOOTINFO_WIFI_FW_BOARD, &board_phys, &board_size64) ||
        !board_phys || board_size64 < 0x30u || board_size64 > 0xFFFFFFFFull)
    {
        terminal_print("[K:QWIFI] BDF lookup skipped: board-2 firmware missing/invalid");
        terminal_flush_log();
        return 0;
    }

    board = (const uint8_t *)pmem_phys_to_virt(board_phys);
    board_size = (uint32_t)board_size64;
    if (!board || !qwifi_bytes_match(board, 16u, "QCA-ATH12K-BOARD"))
    {
        terminal_print("[K:QWIFI] BDF lookup skipped: board-2 magic mismatch");
        terminal_flush_log();
        return 0;
    }

    off = qwifi_align4(qwifi_cstr_len("QCA-ATH12K-BOARD") + 1u);
    while (off + 8u <= board_size)
    {
        uint32_t ie_id = qwifi_read_le32(board + off);
        uint32_t ie_len = qwifi_read_le32(board + off + 4u);
        uint32_t ie_data = off + 8u;
        uint32_t ie_end;
        uint32_t sub;
        const uint8_t *name = 0;
        uint32_t name_len = 0;

        if (ie_len > board_size - ie_data)
            break;

        ie_end = ie_data + ie_len;
        if (ie_id != wanted_ie)
        {
            off = ie_data + qwifi_align4(ie_len);
            continue;
        }

        sub = ie_data;
        while (sub + 8u <= ie_end)
        {
            uint32_t sub_id = qwifi_read_le32(board + sub);
            uint32_t sub_len = qwifi_read_le32(board + sub + 4u);
            uint32_t sub_data = sub + 8u;
            int name_matches = 0;

            if (sub_len > ie_end - sub_data)
                break;

            if (sub_id == QWIFI_BOARD2_SUBIE_NAME)
            {
                name = board + sub_data;
                name_len = sub_len;
            }
            else if (sub_id == QWIFI_BOARD2_SUBIE_DATA && name && name_len)
            {
                if (wanted_ie == QWIFI_BOARD2_IE_BOARD)
                    name_matches = qwifi_board_name_matches_wcn7850_lenovo(name, name_len);
                else if (wanted_ie == QWIFI_BOARD2_IE_REGDB)
                    name_matches = qwifi_board_name_matches_regdb(name, name_len);

                if (name_matches)
                {
                    const char *selected_name = "fallback";
                    uint8_t bdf_type;
                    uint32_t rank;

                    if (wanted_ie == QWIFI_BOARD2_IE_REGDB)
                    {
                        bdf_type = QWIFI_QMI_BDF_TYPE_REGDB;
                        rank = (name_len == 7u && qwifi_bytes_match(name, name_len, "bus=pci")) ? 1u : 2u;
                        selected_name = (rank == 1u) ? "regdb=bus=pci" : "regdb=device";
                    }
                    else
                    {
                        bdf_type = (uint8_t)((sub_len >= 4u && qwifi_bytes_match(board + sub_data, 4u, "\x7F""ELF")) ?
                                             QWIFI_QMI_BDF_TYPE_ELF :
                                             QWIFI_QMI_BDF_TYPE_BIN);
                        rank = qwifi_board_name_preferred_rank(name, name_len, &selected_name);
                    }

                    if (!have_fallback || rank >= match->rank)
                    {
                        qwifi_store_board_match(match,
                                                board + sub_data,
                                                sub_len,
                                                bdf_type,
                                                name_len,
                                                selected_name,
                                                rank);
                        have_fallback = 1;
                    }
                }
            }

            sub = sub_data + qwifi_align4(sub_len);
        }

        off = ie_data + qwifi_align4(ie_len);
    }

    if (have_fallback)
    {
        terminal_print("[K:QWIFI] board-2 match ");
        terminal_print(match->selected_name);
        terminal_print(" len=");
        terminal_print_inline_hex64(match->len);
        terminal_print(" type=");
        terminal_print_inline_hex64(match->bdf_type);
        terminal_print(" name_len=");
        terminal_print_inline_hex64(match->name_len);
        terminal_print(" rank=");
        terminal_print_inline_hex64(match->rank);
        terminal_flush_log();
        return 1;
    }

    terminal_print("[K:QWIFI] board-2 match missing ie=");
    terminal_print_inline_hex64(wanted_ie);
    terminal_flush_log();
    return 0;
}

static int qwifi_find_wcn7850_regdb_data(const boot_info *bi, qwifi_board_match *match)
{
    return qwifi_find_wcn7850_board2_ie(bi, QWIFI_BOARD2_IE_REGDB, match);
}

static int qwifi_find_wcn7850_board_data(const boot_info *bi, qwifi_board_match *match)
{
    return qwifi_find_wcn7850_board2_ie(bi, QWIFI_BOARD2_IE_BOARD, match);
}

static uint16_t qwifi_build_bdf_download_payload(uint8_t *buf,
                                                 uint32_t cap,
                                                 const uint8_t *data,
                                                 uint32_t remaining,
                                                 uint32_t seg_id,
                                                 uint16_t chunk_len,
                                                 uint8_t end,
                                                 uint8_t bdf_type)
{
    uint32_t off = 0;
    uint32_t file_id = g_qwifi_target_board_id ? g_qwifi_target_board_id : QWIFI_QMI_WCN7850_BOARD_ID_DEFAULT;

    if (!buf || (!data && chunk_len))
        return 0;

    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x01u, 1u);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x10u, file_id);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x11u, remaining);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x12u, seg_id);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_data_u16_len(buf, cap, off, 0x13u, data, chunk_len);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x14u, end);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x15u, bdf_type);
    return (uint16_t)off;
}

static int qwifi_send_bdf_match_download(uint64_t bar0_base,
                                         uint32_t chdboff,
                                         const qwifi_board_match *match,
                                         const char *label)
{
    uint32_t sent = 0;
    uint32_t seg_id = 0;

    if (!match || !match->data || !match->len)
        return 0;

    terminal_print("[K:QWIFI] ");
    terminal_print(label ? label : "BDF");
    terminal_print(" download begin total=");
    terminal_print_inline_hex64(match->len);
    terminal_print(" type=");
    terminal_print_inline_hex64(match->bdf_type);
    terminal_print(" board_id=");
    terminal_print_inline_hex64(g_qwifi_target_board_id ? g_qwifi_target_board_id : QWIFI_QMI_WCN7850_BOARD_ID_DEFAULT);
    terminal_flush_log();

    while (sent < match->len)
    {
        static uint8_t payload[QWIFI_QMI_BDF_CHUNK_BYTES + 64u];
        uint32_t remaining = match->len - sent;
        uint16_t chunk = (uint16_t)((remaining > QWIFI_QMI_BDF_CHUNK_BYTES) ? QWIFI_QMI_BDF_CHUNK_BYTES : remaining);
        uint16_t payload_len;
        uint16_t txn_id = (uint16_t)(QWIFI_QMI_TXN_BDF_DOWNLOAD + seg_id);
        uint8_t end = (chunk == remaining) ? 1u : 0u;

        qwifi_zero(payload, sizeof(payload));
        payload_len = qwifi_build_bdf_download_payload(payload,
                                                       sizeof(payload),
                                                       match->data + sent,
                                                       remaining,
                                                       seg_id,
                                                       chunk,
                                                       end,
                                                       match->bdf_type);
        if (!payload_len)
        {
            terminal_print("[K:QWIFI] ");
            terminal_print(label ? label : "BDF");
            terminal_print(" payload build failed seg=");
            terminal_print_inline_hex64(seg_id);
            terminal_flush_log();
            return 0;
        }

        terminal_print("[K:QWIFI] ");
        terminal_print(label ? label : "BDF");
        terminal_print(" chunk seg=");
        terminal_print_inline_hex64(seg_id);
        terminal_print(" off=");
        terminal_print_inline_hex64(sent);
        terminal_print(" chunk=");
        terminal_print_inline_hex64(chunk);
        terminal_print(" remaining=");
        terminal_print_inline_hex64(remaining);
        terminal_print(" end=");
        terminal_print_inline_hex64(end);
        terminal_print(" txn=");
        terminal_print_inline_hex64(txn_id);
        terminal_flush_log();

        g_qwifi_qmi_bdf_download_resp_seen = 0u;
        if (!qwifi_mhi_send_qmi_request(bar0_base,
                                        chdboff,
                                        QWIFI_QMI_WLANFW_BDF_DOWNLOAD_REQ,
                                        txn_id,
                                        payload,
                                        payload_len,
                                        label ? label : "WLANFW_BDF_DOWNLOAD_REQ"))
            return 0;

        qwifi_mhi_wait_for_qmi_bdf_download_events("after-qmi-bdf", 2048u);
        if (!g_qwifi_qmi_bdf_download_resp_seen ||
            !qwifi_qmi_last_response_ok(QWIFI_QMI_WLANFW_BDF_DOWNLOAD_REQ,
                                        txn_id,
                                        1))
        {
            terminal_print("[K:QWIFI] ");
            terminal_print(label ? label : "BDF");
            terminal_print(" chunk rejected/timeout seg=");
            terminal_print_inline_hex64(seg_id);
            terminal_print(" result=");
            terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
            terminal_print(" error=");
            terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
            terminal_flush_log();
            g_qwifi_qmi_bdf_download_resp_seen = 0u;
            return 0;
        }

        for (uint32_t pace = 0; pace < 4u; ++pace)
            short_delay();

        sent += chunk;
        seg_id++;
    }

    terminal_print("[K:QWIFI] ");
    terminal_print(label ? label : "BDF");
    terminal_print(" download complete chunks=");
    terminal_print_inline_hex64(seg_id);
    terminal_flush_log();
    return 1;
}

static int qwifi_send_regdb_download(uint64_t bar0_base, uint32_t chdboff, const boot_info *bi)
{
    qwifi_board_match match;

    if (!qwifi_find_wcn7850_regdb_data(bi, &match) || !match.data || !match.len)
    {
        terminal_print("[K:QWIFI] REGDB download skipped: no board-2 regdb match");
        terminal_flush_log();
        return 0;
    }

    return qwifi_send_bdf_match_download(bar0_base, chdboff, &match, "REGDB");
}

static int qwifi_send_bdf_download(uint64_t bar0_base, uint32_t chdboff, const boot_info *bi)
{
    qwifi_board_match match;

    if (!qwifi_find_wcn7850_board_data(bi, &match) || !match.data || !match.len)
        return 0;

    return qwifi_send_bdf_match_download(bar0_base, chdboff, &match, "BDF");
}

static int qwifi_prepare_m3_blob(const boot_info *bi, uint64_t *m3_phys, uint32_t *m3_size)
{
    uint64_t src_phys = 0;
    uint64_t src_size = 0;

    if (!m3_phys || !m3_size)
        return 0;

    if (g_qwifi_m3_phys && g_qwifi_m3_size)
    {
        *m3_phys = g_qwifi_m3_phys;
        *m3_size = g_qwifi_m3_size;
        return 1;
    }

    if (!qwifi_get_fw_blob(bi, BOOTINFO_WIFI_FW_M3, &src_phys, &src_size) ||
        !src_phys || !src_size || src_size > 0xFFFFFFFFull)
    {
        terminal_print("[K:QWIFI] M3 skipped: m3 firmware missing/invalid");
        terminal_flush_log();
        return 0;
    }

    g_qwifi_m3_virt = pmem_alloc_pages_lowdma(qwifi_pages_for(src_size));
    if (!g_qwifi_m3_virt)
    {
        terminal_print("[K:QWIFI] M3 alloc failed size=");
        terminal_print_inline_hex64(src_size);
        terminal_flush_log();
        return 0;
    }

    qwifi_zero(g_qwifi_m3_virt, qwifi_pages_for(src_size) << 12);
    qwifi_copy_from_phys(g_qwifi_m3_virt, src_phys, src_size);
    asm_dma_clean_range(g_qwifi_m3_virt, qwifi_pages_for(src_size) << 12);
    g_qwifi_m3_phys = pmem_virt_to_phys(g_qwifi_m3_virt);
    g_qwifi_m3_size = (uint32_t)src_size;

    terminal_print("[K:QWIFI] M3 prepared pa=");
    terminal_print_inline_hex64(g_qwifi_m3_phys);
    terminal_print(" size=");
    terminal_print_inline_hex64(g_qwifi_m3_size);
    terminal_flush_log();

    *m3_phys = g_qwifi_m3_phys;
    *m3_size = g_qwifi_m3_size;
    return 1;
}

static uint16_t qwifi_build_m3_info_payload(uint8_t *buf, uint32_t cap, uint64_t m3_phys, uint32_t m3_size)
{
    uint32_t off = 0;

    off = qwifi_qmi_put_tlv_u64(buf, cap, off, 0x01u, m3_phys);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x02u, m3_size);
    return (uint16_t)off;
}

static void qwifi_put_record_u32(uint8_t *buf, uint32_t *off, uint32_t value)
{
    qwifi_write_le32(buf + *off, value);
    *off += 4u;
}

static uint16_t qwifi_build_wlan_cfg_payload(uint8_t *buf, uint32_t cap)
{
    static const uint32_t ce_cfg[][5] = {
        {0u, QWIFI_CE_PIPEDIR_OUT, 32u, 2048u, 0u},
        {1u, QWIFI_CE_PIPEDIR_IN, 32u, 2048u, 0u},
        {2u, QWIFI_CE_PIPEDIR_IN, 32u, 2048u, 0u},
        {3u, QWIFI_CE_PIPEDIR_OUT, 32u, 2048u, 0u},
        {4u, QWIFI_CE_PIPEDIR_OUT, 256u, 256u, QWIFI_CE_ATTR_DIS_INTR},
        {5u, QWIFI_CE_PIPEDIR_IN, 32u, 2048u, 0u},
        {6u, QWIFI_CE_PIPEDIR_INOUT, 32u, 16384u, 0u},
        {7u, QWIFI_CE_PIPEDIR_INOUT_H2H, 0u, 0u, QWIFI_CE_ATTR_DIS_INTR},
        {8u, QWIFI_CE_PIPEDIR_INOUT, 32u, 16384u, 0u},
    };
    static const uint32_t svc_cfg[][3] = {
        {QWIFI_HTC_SVC_WMI_DATA_VO, QWIFI_CE_PIPEDIR_OUT, 3u},
        {QWIFI_HTC_SVC_WMI_DATA_VO, QWIFI_CE_PIPEDIR_IN, 2u},
        {QWIFI_HTC_SVC_WMI_DATA_BK, QWIFI_CE_PIPEDIR_OUT, 3u},
        {QWIFI_HTC_SVC_WMI_DATA_BK, QWIFI_CE_PIPEDIR_IN, 2u},
        {QWIFI_HTC_SVC_WMI_DATA_BE, QWIFI_CE_PIPEDIR_OUT, 3u},
        {QWIFI_HTC_SVC_WMI_DATA_BE, QWIFI_CE_PIPEDIR_IN, 2u},
        {QWIFI_HTC_SVC_WMI_DATA_VI, QWIFI_CE_PIPEDIR_OUT, 3u},
        {QWIFI_HTC_SVC_WMI_DATA_VI, QWIFI_CE_PIPEDIR_IN, 2u},
        {QWIFI_HTC_SVC_WMI_CONTROL, QWIFI_CE_PIPEDIR_OUT, 3u},
        {QWIFI_HTC_SVC_WMI_CONTROL, QWIFI_CE_PIPEDIR_IN, 2u},
        {QWIFI_HTC_SVC_RSVD_CTRL, QWIFI_CE_PIPEDIR_OUT, 0u},
        {QWIFI_HTC_SVC_RSVD_CTRL, QWIFI_CE_PIPEDIR_IN, 2u},
        {QWIFI_HTC_SVC_HTT_DATA_MSG, QWIFI_CE_PIPEDIR_OUT, 4u},
        {QWIFI_HTC_SVC_HTT_DATA_MSG, QWIFI_CE_PIPEDIR_IN, 1u},
        {0u, 0u, 0u},
    };
    uint8_t ce_records[sizeof(ce_cfg)];
    uint8_t svc_records[sizeof(svc_cfg)];
#if QWIFI_CE_USE_SHADOW_REGS
    uint8_t shadow_records[sizeof(g_qwifi_ce_shadow_reg_v3)];
#endif
    uint32_t rec_off = 0;
    uint32_t off = 0;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(ce_cfg) / sizeof(ce_cfg[0])); ++i)
    {
        for (uint32_t j = 0; j < 5u; ++j)
            qwifi_put_record_u32(ce_records, &rec_off, ce_cfg[i][j]);
    }

    rec_off = 0;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(svc_cfg) / sizeof(svc_cfg[0])); ++i)
    {
        for (uint32_t j = 0; j < 3u; ++j)
            qwifi_put_record_u32(svc_records, &rec_off, svc_cfg[i][j]);
    }

#if QWIFI_CE_USE_SHADOW_REGS
    rec_off = 0;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_qwifi_ce_shadow_reg_v3) / sizeof(g_qwifi_ce_shadow_reg_v3[0])); ++i)
        qwifi_put_record_u32(shadow_records, &rec_off, g_qwifi_ce_shadow_reg_v3[i]);
#endif

    off = qwifi_qmi_put_tlv_string(buf, cap, off, 0x10u, "WIN");
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_records_u8_len(buf,
                                           cap,
                                           off,
                                           0x11u,
                                           ce_records,
                                           (uint8_t)(sizeof(ce_cfg) / sizeof(ce_cfg[0])),
                                           20u);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_records_u8_len(buf,
                                           cap,
                                           off,
                                           0x12u,
                                           svc_records,
                                           (uint8_t)(sizeof(svc_cfg) / sizeof(svc_cfg[0])),
                                           12u);
#if QWIFI_CE_USE_SHADOW_REGS
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_records_u8_len(buf,
                                           cap,
                                           off,
                                           QWIFI_QMI_WLAN_CFG_TLV_SHADOW_REG_V3,
                                           shadow_records,
                                           (uint8_t)(sizeof(g_qwifi_ce_shadow_reg_v3) / sizeof(g_qwifi_ce_shadow_reg_v3[0])),
                                           4u);
#endif
    return (uint16_t)off;
}

static uint16_t qwifi_build_wlan_ini_payload(uint8_t *buf, uint32_t cap)
{
    return (uint16_t)qwifi_qmi_put_tlv_u8(buf, cap, 0u, 0x10u, 1u);
}

static uint16_t qwifi_build_wlan_mode_payload(uint8_t *buf, uint32_t cap)
{
    uint32_t off = 0;

    off = qwifi_qmi_put_tlv_u32(buf, cap, off, 0x01u, QWIFI_QMI_FW_MODE_NORMAL);
    if (!off)
        return 0;
    off = qwifi_qmi_put_tlv_u8(buf, cap, off, 0x10u, 0u); /* hw_debug */
    return (uint16_t)off;
}

static int qwifi_mhi_send_qmi_request(uint64_t bar0_base,
                                      uint32_t chdboff,
                                      uint16_t msg_id,
                                      uint16_t txn_id,
                                      const void *payload,
                                      uint16_t payload_len,
                                      const char *label)
{
    qwifi_mhi_ring_element *tx_ring = (qwifi_mhi_ring_element *)g_qwifi_ipcr_tx_ring;
    uint64_t tx_ring_pa;
    uint64_t wp_pa;
    void *packet;
    uint64_t packet_pa;
    uint32_t len;
    uint32_t tx_len;
    uint32_t idx;
    int rc;

    if (!tx_ring || !g_qwifi_chan_ctxt)
        return 0;

    packet = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_IPCR_TX_BUF_BYTES));
    if (!packet)
    {
        terminal_print("[K:QWIFI] QMI alloc failed");
        terminal_flush_log();
        return 0;
    }

    qwifi_zero(packet, QWIFI_MHI_IPCR_TX_BUF_BYTES);
    len = qwifi_build_qrtr_qmi_request(packet,
                                       QWIFI_MHI_IPCR_TX_BUF_BYTES,
                                       msg_id,
                                       txn_id,
                                       payload,
                                       payload_len);
    if (!len)
    {
        terminal_print("[K:QWIFI] QMI build failed");
        terminal_flush_log();
        return 0;
    }
    tx_len = (len + 3u) & ~3u;

    packet_pa = pmem_virt_to_phys(packet);
    asm_dma_clean_range(packet, tx_len);

    tx_ring_pa = pmem_virt_to_phys(g_qwifi_ipcr_tx_ring);
    idx = g_qwifi_ipcr_tx_wp_index % QWIFI_MHI_IPCR_ELEMENTS;
    tx_ring[idx].ptr = packet_pa;
    tx_ring[idx].dword0 = tx_len & 0xFFFFu;
    tx_ring[idx].dword1 = qwifi_mhi_transfer_dword1();
    asm_dma_clean_range(&tx_ring[idx], sizeof(tx_ring[idx]));

    g_qwifi_ipcr_tx_wp_index = (g_qwifi_ipcr_tx_wp_index + 1u) % QWIFI_MHI_IPCR_ELEMENTS;
    wp_pa = tx_ring_pa + (uint64_t)g_qwifi_ipcr_tx_wp_index * QWIFI_MHI_TRE_BYTES;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].wp = wp_pa;
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH], sizeof(g_qwifi_chan_ctxt[0]));

    terminal_print("[K:QWIFI] QMI ");
    terminal_print(label ? label : "REQ");
    terminal_print(" msg=");
    terminal_print_inline_hex64(msg_id);
    terminal_print(" txn=");
    terminal_print_inline_hex64(txn_id);
    terminal_print(" dst=");
    terminal_print_inline_hex64(g_qwifi_qrtr_wlan_node);
    terminal_print(":");
    terminal_print_inline_hex64(g_qwifi_qrtr_wlan_port);
    terminal_print(" tx_pa=");
    terminal_print_inline_hex64(packet_pa);
    terminal_print(" len=");
    terminal_print_inline_hex64(len);
    terminal_print(" tx_len=");
    terminal_print_inline_hex64(tx_len);
    terminal_print(" pending=");
    terminal_print_inline_hex64(g_qwifi_qrtr_tx_pending);
    terminal_print(" confirm=");
    terminal_print_inline_hex64(((const qwifi_qrtr_hdr_v1 *)packet)->confirm_rx);
    terminal_print(" idx=");
    terminal_print_inline_hex64(idx);
    terminal_flush_log();

    rc = qwifi_mhi_ring_channel_db(bar0_base, chdboff, QWIFI_MHI_IPCR_TX_CH, wp_pa);
    terminal_print("[K:QWIFI] QMI ");
    terminal_print(label ? label : "REQ");
    terminal_print(" doorbell wp=");
    terminal_print_inline_hex64(wp_pa);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();

    return rc == 0;
}

static void *qwifi_alloc_zero_lowdma(uint64_t bytes)
{
    void *ptr = pmem_alloc_pages_lowdma(qwifi_pages_for(bytes));

    if (ptr)
        qwifi_zero(ptr, qwifi_pages_for(bytes) << 12);

    return ptr;
}

static uint32_t qwifi_ce_ring_dwords(uint32_t entries, uint32_t entry_dwords)
{
    return entries * entry_dwords;
}

static uint32_t qwifi_ce_reg_src(uint32_t ce)
{
    return QWIFI_CE_REG_SRC_BASE + ce * QWIFI_CE_REG_STRIDE;
}

static uint32_t qwifi_ce_reg_dst(uint32_t ce)
{
    return QWIFI_CE_REG_DST_BASE + ce * QWIFI_CE_REG_STRIDE;
}

static uint32_t qwifi_ce_ring_base_msb(uint64_t ring_pa, uint32_t ring_dwords)
{
    return (uint32_t)((ring_pa >> 32) & 0xFFu) | ((ring_dwords & 0xFFFFu) << 8);
}

static uint32_t qwifi_ce_desc_addr_info(uint64_t phys)
{
    return (uint32_t)((phys >> 32) & QWIFI_CE_DESC_ADDR_HI_MASK);
}

static int qwifi_ce_program_src_like_ring(uint64_t bar0_base,
                                          uint32_t r0_base,
                                          uint32_t r2_base,
                                          uint32_t ring_id,
                                          uint32_t shadow_idx,
                                          void *ring,
                                          uint32_t entries,
                                          uint32_t entry_dwords)
{
    uint64_t ring_pa;
    uint64_t tp_pa;
    uint32_t ring_dwords;
    int rc = 0;

    if (!ring || !entries || !entry_dwords || !g_qwifi_ce_rdp || !g_qwifi_ce_rdp_phys)
        return 0;

    ring_pa = pmem_virt_to_phys(ring);
    tp_pa = g_qwifi_ce_rdp_phys + (uint64_t)ring_id * 4u;
    ring_dwords = qwifi_ce_ring_dwords(entries, entry_dwords);

    g_qwifi_ce_rdp[ring_id] = 0u;
    asm_dma_clean_range(&g_qwifi_ce_rdp[ring_id], sizeof(g_qwifi_ce_rdp[ring_id]));

    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_BASE_LSB, (uint32_t)ring_pa);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_BASE_MSB,
                                     qwifi_ce_ring_base_msb(ring_pa, ring_dwords));
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_RING_ID, entry_dwords);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_INT_SETUP0, 0u);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_INT_SETUP1, 0u);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_TP_ADDR_LSB, (uint32_t)tp_pa);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_TP_ADDR_MSB, (uint32_t)(tp_pa >> 32));
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r2_base, 0u);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r2_base + (QWIFI_CE_R2_TP - QWIFI_CE_R2_HP), 0u);
    rc |= qwifi_ce_shadow_write(bar0_base, shadow_idx, 0u);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_MISC,
                                     QWIFI_CE_MISC_MSI_LOOPCNT_DISABLE |
                                     QWIFI_CE_MISC_SRNG_ENABLE);

    return rc == 0;
}

static int qwifi_ce_program_status_ring(uint64_t bar0_base,
                                        uint32_t ce,
                                        uint32_t shadow_idx,
                                        void *ring,
                                        uint32_t entries)
{
    uint32_t dst_base = qwifi_ce_reg_dst(ce);
    uint32_t r0_base = dst_base + QWIFI_CE_R0_STATUS_BASE_LSB;
    uint32_t r2_base = dst_base + QWIFI_CE_R2_STATUS_HP;
    uint32_t ring_id = QWIFI_CE_RING_ID_DST_STATUS_BASE + ce;
    uint64_t ring_pa;
    uint64_t hp_pa;
    uint32_t ring_dwords;
    int rc = 0;

    if (!ring || !entries || !g_qwifi_ce_rdp || !g_qwifi_ce_rdp_phys)
        return 0;

    ring_pa = pmem_virt_to_phys(ring);
    hp_pa = g_qwifi_ce_rdp_phys + (uint64_t)ring_id * 4u;
    ring_dwords = qwifi_ce_ring_dwords(entries, QWIFI_CE_STATUS_DESC_DWORDS);

    g_qwifi_ce_rdp[ring_id] = 0u;
    asm_dma_clean_range(&g_qwifi_ce_rdp[ring_id], sizeof(g_qwifi_ce_rdp[ring_id]));

    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_BASE_LSB, (uint32_t)ring_pa);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_BASE_MSB,
                                     qwifi_ce_ring_base_msb(ring_pa, ring_dwords));
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_RING_ID,
                                     (ring_id << 8) | QWIFI_CE_STATUS_DESC_DWORDS);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_HP_ADDR_LSB, (uint32_t)hp_pa);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_HP_ADDR_MSB, (uint32_t)(hp_pa >> 32));
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r2_base, 0u);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r2_base + (QWIFI_CE_R2_STATUS_TP - QWIFI_CE_R2_STATUS_HP), 0u);
    rc |= qwifi_ce_shadow_write(bar0_base, shadow_idx, 0u);
    rc |= qwifi_ath12k_write32_quiet(bar0_base, r0_base + QWIFI_CE_R0_MISC,
                                     QWIFI_CE_MISC_SRNG_ENABLE);

    return rc == 0;
}

static int qwifi_ce_alloc_pipe_rings(uint32_t ce, uint32_t src_entries, uint32_t dst_entries, uint32_t buf_sz)
{
    qwifi_ce_pipe *pipe;
    uint64_t bytes;

    if (ce >= QWIFI_CE_COUNT)
        return 0;

    pipe = &g_qwifi_ce[ce];
    pipe->src_entries = src_entries;
    pipe->dst_entries = dst_entries;
    pipe->status_entries = dst_entries;
    pipe->buf_sz = buf_sz ? buf_sz : QWIFI_CE_RX_BUF_BYTES;
    pipe->src_shadow_idx = qwifi_ce_shadow_src_idx(ce);
    pipe->dst_shadow_idx = qwifi_ce_shadow_dst_idx(ce);
    pipe->status_shadow_idx = qwifi_ce_shadow_status_idx(ce);

    if (src_entries && !pipe->src_ring)
    {
        bytes = (uint64_t)src_entries * sizeof(qwifi_ce_src_desc);
        pipe->src_ring = qwifi_alloc_zero_lowdma(bytes);
        if (!pipe->src_ring)
            return 0;
        asm_dma_clean_range(pipe->src_ring, bytes);
    }

    if (dst_entries && !pipe->dst_ring)
    {
        bytes = (uint64_t)dst_entries * sizeof(qwifi_ce_dst_desc);
        pipe->dst_ring = qwifi_alloc_zero_lowdma(bytes);
        if (!pipe->dst_ring)
            return 0;
        asm_dma_clean_range(pipe->dst_ring, bytes);

        bytes = (uint64_t)dst_entries * sizeof(qwifi_ce_dst_status_desc);
        pipe->status_ring = qwifi_alloc_zero_lowdma(bytes);
        if (!pipe->status_ring)
            return 0;
        asm_dma_clean_range(pipe->status_ring, bytes);
    }

    return 1;
}

static int qwifi_ce_prepost_rx(uint64_t bar0_base, uint32_t ce, uint32_t post_count)
{
    qwifi_ce_pipe *pipe;
    qwifi_ce_dst_desc *dst;
    uint32_t dst_base;
    uint32_t post;
    uint32_t ring_dwords;

    if (ce >= QWIFI_CE_COUNT)
        return 0;

    pipe = &g_qwifi_ce[ce];
    if (!pipe->dst_ring || !pipe->dst_entries)
        return 1;

    dst = (qwifi_ce_dst_desc *)pipe->dst_ring;
    dst_base = qwifi_ce_reg_dst(ce);
    ring_dwords = qwifi_ce_ring_dwords(pipe->dst_entries, QWIFI_CE_DST_DESC_DWORDS);
    post = post_count;
    if (post > QWIFI_CE_MAX_RX_POST)
        post = QWIFI_CE_MAX_RX_POST;
    if (post > pipe->dst_entries)
        post = pipe->dst_entries;

    for (uint32_t i = 0; i < post; ++i)
    {
        uint32_t idx = pipe->dst_wp / QWIFI_CE_DST_DESC_DWORDS;
        void *buf = pipe->rx_buf[i];
        uint64_t phys;

        if (!buf)
        {
            buf = qwifi_alloc_zero_lowdma(pipe->buf_sz);
            pipe->rx_buf[i] = buf;
        }
        if (!buf)
            return 0;

        qwifi_zero(buf, pipe->buf_sz);
        asm_dma_invalidate_range(buf, pipe->buf_sz);
        phys = pmem_virt_to_phys(buf);
        pipe->rx_phys[i] = phys;

        dst[idx].buffer_addr_low = (uint32_t)phys;
        dst[idx].buffer_addr_info = qwifi_ce_desc_addr_info(phys);
        asm_dma_clean_range(&dst[idx], sizeof(dst[idx]));

        pipe->dst_wp += QWIFI_CE_DST_DESC_DWORDS;
        if (pipe->dst_wp >= ring_dwords)
            pipe->dst_wp = 0u;
    }

    (void)qwifi_ath12k_write32_quiet(bar0_base, dst_base + QWIFI_CE_R2_HP, pipe->dst_wp);
    (void)qwifi_ce_shadow_write(bar0_base, pipe->dst_shadow_idx, pipe->dst_wp);
    return 1;
}

static int qwifi_ce_init_pipe(uint64_t bar0_base,
                              uint32_t ce,
                              uint32_t src_entries,
                              uint32_t dst_entries,
                              uint32_t buf_sz,
                              uint32_t rx_post)
{
    qwifi_ce_pipe *pipe;
    uint32_t src_base;
    uint32_t dst_base;
    int ok = 1;

    if (!qwifi_ce_alloc_pipe_rings(ce, src_entries, dst_entries, buf_sz))
        return 0;

    pipe = &g_qwifi_ce[ce];
    src_base = qwifi_ce_reg_src(ce);
    dst_base = qwifi_ce_reg_dst(ce);

    if (pipe->src_ring)
        ok &= qwifi_ce_program_src_like_ring(bar0_base,
                                             src_base,
                                             src_base + QWIFI_CE_R2_HP,
                                             QWIFI_CE_RING_ID_SRC_BASE + ce,
                                             pipe->src_shadow_idx,
                                             pipe->src_ring,
                                             pipe->src_entries,
                                             QWIFI_CE_SRC_DESC_DWORDS);

    if (pipe->dst_ring)
    {
        ok &= qwifi_ce_program_src_like_ring(bar0_base,
                                             dst_base,
                                             dst_base + QWIFI_CE_R2_HP,
                                             QWIFI_CE_RING_ID_DST_BASE + ce,
                                             pipe->dst_shadow_idx,
                                             pipe->dst_ring,
                                             pipe->dst_entries,
                                             QWIFI_CE_DST_DESC_DWORDS);
        ok &= qwifi_ce_program_status_ring(bar0_base, ce, pipe->status_shadow_idx, pipe->status_ring, pipe->status_entries);
        ok &= qwifi_ath12k_write32_quiet(bar0_base, dst_base + QWIFI_CE_R0_DST_CTRL, pipe->buf_sz) == 0;
        ok &= qwifi_ce_prepost_rx(bar0_base, ce, rx_post);
    }

    terminal_print("[K:QWIFI] CE pipe init ce=");
    terminal_print_inline_hex64(ce);
    terminal_print(" src=");
    terminal_print_inline_hex64(src_entries);
    terminal_print(" dst=");
    terminal_print_inline_hex64(dst_entries);
    terminal_print(" post=");
    terminal_print_inline_hex64(rx_post);
    terminal_print(" ok=");
    terminal_print_inline_hex64(ok ? 1u : 0u);
    terminal_flush_log();

    return ok;
}

static void qwifi_ce_parse_htc_frame(uint32_t ce, const uint8_t *buf, uint32_t len)
{
    uint32_t htc_info;
    uint32_t ctrl_info;
    uint32_t eid;
    uint32_t flags;
    uint32_t payload_len;

    if (!buf || len < 8u)
        return;

    htc_info = qwifi_read_le32(buf);
    ctrl_info = qwifi_read_le32(buf + 4u);
    eid = htc_info & 0xFFu;
    flags = (htc_info >> 8) & 0xFFu;
    payload_len = (htc_info >> 16) & 0xFFFFu;

    terminal_print("[K:QWIFI] HTC rx ce=");
    terminal_print_inline_hex64(ce);
    terminal_print(" eid=");
    terminal_print_inline_hex64(eid);
    terminal_print(" flags=");
    terminal_print_inline_hex64(flags);
    terminal_print(" payload=");
    terminal_print_inline_hex64(payload_len);
    terminal_print(" ctrl=");
    terminal_print_inline_hex64(ctrl_info);
    terminal_flush_log();

    if (eid == 0u && len >= 16u && payload_len >= 8u)
    {
        uint32_t msg_svc = qwifi_read_le32(buf + 8u);
        uint32_t flags_len = qwifi_read_le32(buf + 12u);
        uint32_t msg_id = msg_svc & 0xFFFFu;

        terminal_print("[K:QWIFI] HTC ctrl msg=");
        terminal_print_inline_hex64(msg_id);
        terminal_print(" word0=");
        terminal_print_inline_hex64(msg_svc);
        terminal_print(" word1=");
        terminal_print_inline_hex64(flags_len);
        terminal_flush_log();

        if (msg_id == QWIFI_HTC_MSG_READY_ID)
        {
            g_qwifi_htc_ready_seen = 1u;
            g_qwifi_htc_total_credits = (msg_svc >> 16) & 0xFFFFu;
            g_qwifi_htc_credit_size = flags_len & 0xFFFFu;
            terminal_print("[K:QWIFI] HTC READY credits=");
            terminal_print_inline_hex64(g_qwifi_htc_total_credits);
            terminal_print(" credit_size=");
            terminal_print_inline_hex64(g_qwifi_htc_credit_size);
            terminal_print(" max_ep=");
            terminal_print_inline_hex64((flags_len >> 16) & 0xFFu);
            terminal_flush_log();
        }
    }
}

static void qwifi_ce_poll_rx_pipe(uint64_t bar0_base, uint32_t ce)
{
    qwifi_ce_pipe *pipe;
    qwifi_ce_dst_status_desc *status;
    uint32_t ring_dwords;
    uint32_t hp = 0;
    uint32_t hp_mem = 0;
    uint32_t ring_id;
    uint32_t dst_base;

    if (ce >= QWIFI_CE_COUNT)
        return;

    pipe = &g_qwifi_ce[ce];
    if (!pipe->status_ring || !pipe->status_entries)
        return;

    ring_id = QWIFI_CE_RING_ID_DST_STATUS_BASE + ce;
    dst_base = qwifi_ce_reg_dst(ce);
    ring_dwords = qwifi_ce_ring_dwords(pipe->status_entries, QWIFI_CE_STATUS_DESC_DWORDS);
    status = (qwifi_ce_dst_status_desc *)pipe->status_ring;

    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R2_STATUS_HP, &hp);
    if (g_qwifi_ce_rdp)
    {
        asm_dma_invalidate_range(&g_qwifi_ce_rdp[ring_id], sizeof(g_qwifi_ce_rdp[ring_id]));
        hp_mem = g_qwifi_ce_rdp[ring_id];
        if (hp_mem != pipe->status_rp)
            hp = hp_mem;
    }

    for (uint32_t guard = 0; guard < pipe->status_entries && pipe->status_rp != hp; ++guard)
    {
        uint32_t status_idx = pipe->status_rp / QWIFI_CE_STATUS_DESC_DWORDS;
        uint32_t rx_idx = pipe->dst_sw_index % QWIFI_CE_MAX_RX_POST;
        uint32_t flags;
        uint32_t meta;
        uint32_t len;

        asm_dma_invalidate_range(&status[status_idx], sizeof(status[status_idx]));
        flags = status[status_idx].flags;
        meta = status[status_idx].meta_info;
        len = (flags >> 16) & 0xFFFFu;

        terminal_print("[K:QWIFI] CE RX done ce=");
        terminal_print_inline_hex64(ce);
        terminal_print(" st=");
        terminal_print_inline_hex64(status_idx);
        terminal_print(" rx=");
        terminal_print_inline_hex64(rx_idx);
        terminal_print(" len=");
        terminal_print_inline_hex64(len);
        terminal_print(" flags=");
        terminal_print_inline_hex64(flags);
        terminal_print(" meta=");
        terminal_print_inline_hex64(meta);
        terminal_flush_log();

        if (rx_idx < QWIFI_CE_MAX_RX_POST && pipe->rx_buf[rx_idx] && len <= pipe->buf_sz)
        {
            asm_dma_invalidate_range(pipe->rx_buf[rx_idx], pipe->buf_sz);
            qwifi_ce_parse_htc_frame(ce, (const uint8_t *)pipe->rx_buf[rx_idx], len);
        }

        pipe->dst_sw_index++;
        pipe->status_rp += QWIFI_CE_STATUS_DESC_DWORDS;
        if (pipe->status_rp >= ring_dwords)
            pipe->status_rp = 0u;
    }

    (void)qwifi_ath12k_write32_quiet(bar0_base, dst_base + QWIFI_CE_R2_STATUS_TP, pipe->status_rp);
    (void)qwifi_ce_shadow_write(bar0_base, pipe->status_shadow_idx, pipe->status_rp);
}

static void qwifi_ce_dump_rx_state(uint64_t bar0_base, uint32_t ce)
{
    qwifi_ce_pipe *pipe;
    qwifi_ce_dst_desc *dst;
    uint32_t dst_base;
    uint32_t hp = 0;
    uint32_t tp = 0;
    uint32_t st_hp = 0;
    uint32_t st_hp_mem = 0;
    uint32_t dst_shadow = 0;
    uint32_t status_shadow = 0;
    uint32_t r0_base_lsb = 0;
    uint32_t r0_base_msb = 0;
    uint32_t r0_ring_id = 0;
    uint32_t r0_misc = 0;
    uint32_t dst_ctrl = 0;
    uint32_t dst0_lo = 0;
    uint32_t dst0_info = 0;
    uint32_t ring_id;

    if (ce >= QWIFI_CE_COUNT)
        return;

    pipe = &g_qwifi_ce[ce];
    if (!pipe->dst_ring)
        return;

    ring_id = QWIFI_CE_RING_ID_DST_STATUS_BASE + ce;
    dst_base = qwifi_ce_reg_dst(ce);
    dst = (qwifi_ce_dst_desc *)pipe->dst_ring;
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R2_HP, &hp);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R2_TP, &tp);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R2_STATUS_HP, &st_hp);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R0_BASE_LSB, &r0_base_lsb);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R0_BASE_MSB, &r0_base_msb);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R0_RING_ID, &r0_ring_id);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R0_MISC, &r0_misc);
    (void)qwifi_ath12k_read32_quiet(bar0_base, dst_base + QWIFI_CE_R0_DST_CTRL, &dst_ctrl);
    if (dst)
    {
        dst0_lo = dst[0].buffer_addr_low;
        dst0_info = dst[0].buffer_addr_info;
    }
    if (g_qwifi_ce_rdp)
    {
        asm_dma_invalidate_range(&g_qwifi_ce_rdp[ring_id], sizeof(g_qwifi_ce_rdp[ring_id]));
        st_hp_mem = g_qwifi_ce_rdp[ring_id];
    }
    if (pipe->dst_shadow_idx != QWIFI_CE_SHADOW_INVALID)
        (void)qwifi_ath12k_read32_quiet(bar0_base, QWIFI_HAL_SHADOW_REG(pipe->dst_shadow_idx), &dst_shadow);
    if (pipe->status_shadow_idx != QWIFI_CE_SHADOW_INVALID)
        (void)qwifi_ath12k_read32_quiet(bar0_base, QWIFI_HAL_SHADOW_REG(pipe->status_shadow_idx), &status_shadow);

    terminal_print("[K:QWIFI] CE rx state ce=");
    terminal_print_inline_hex64(ce);
    terminal_print(" hp=");
    terminal_print_inline_hex64(hp);
    terminal_print(" dst_wp=");
    terminal_print_inline_hex64(pipe->dst_wp);
    terminal_print(" tp=");
    terminal_print_inline_hex64(tp);
    terminal_print(" dst_shadow=");
    terminal_print_inline_hex64(dst_shadow);
    terminal_print(" st_hp=");
    terminal_print_inline_hex64(st_hp);
    terminal_print(" st_hp_mem=");
    terminal_print_inline_hex64(st_hp_mem);
    terminal_print(" st_rp=");
    terminal_print_inline_hex64(pipe->status_rp);
    terminal_print(" st_shadow=");
    terminal_print_inline_hex64(status_shadow);
    terminal_flush_log();

    terminal_print("[K:QWIFI] CE rx regs ce=");
    terminal_print_inline_hex64(ce);
    terminal_print(" base_lsb=");
    terminal_print_inline_hex64(r0_base_lsb);
    terminal_print(" base_msb=");
    terminal_print_inline_hex64(r0_base_msb);
    terminal_print(" ring_id=");
    terminal_print_inline_hex64(r0_ring_id);
    terminal_print(" misc=");
    terminal_print_inline_hex64(r0_misc);
    terminal_print(" dst_ctrl=");
    terminal_print_inline_hex64(dst_ctrl);
    terminal_print(" d0lo=");
    terminal_print_inline_hex64(dst0_lo);
    terminal_print(" d0info=");
    terminal_print_inline_hex64(dst0_info);
    terminal_flush_log();
}

static int qwifi_ce_prepare_htc(uint64_t bar0_base)
{
    static int attempted = 0;
    int ok = 1;

    if (attempted)
        return g_qwifi_ce_prepared ? 1 : 0;
    attempted = 1;

    if (!g_qwifi_ce_rdp)
    {
        g_qwifi_ce_rdp = (uint32_t *)qwifi_alloc_zero_lowdma(1024u);
        if (!g_qwifi_ce_rdp)
        {
            terminal_print("[K:QWIFI] CE RDP alloc failed");
            terminal_flush_log();
            return 0;
        }
        g_qwifi_ce_rdp_phys = pmem_virt_to_phys(g_qwifi_ce_rdp);
        asm_dma_clean_range(g_qwifi_ce_rdp, 1024u);
    }

    terminal_print("[K:QWIFI] CE/HTC prepare begin rdp=");
    terminal_print_inline_hex64(g_qwifi_ce_rdp_phys);
    terminal_flush_log();

    ok &= qwifi_ce_init_pipe(bar0_base, 0u, 16u, 0u, 0u, 0u);
    ok &= qwifi_ce_init_pipe(bar0_base, 1u, 0u, 512u, QWIFI_CE_RX_BUF_BYTES, 510u);
    ok &= qwifi_ce_init_pipe(bar0_base, 2u, 0u, 64u, QWIFI_CE_RX_BUF_BYTES, 62u);
    ok &= qwifi_ce_init_pipe(bar0_base, 3u, 32u, 0u, 0u, 0u);
    ok &= qwifi_ce_init_pipe(bar0_base, 4u, 2048u, 0u, 0u, 0u);

    if (!ok)
    {
        terminal_print("[K:QWIFI] CE/HTC prepare failed: CE init failed");
        terminal_flush_log();
        return 0;
    }

    g_qwifi_ce_prepared = 1u;
    terminal_print("[K:QWIFI] CE/HTC prepared before WLAN mode");
    terminal_flush_log();
    return 1;
}

static int qwifi_ce_wait_htc_ready(uint64_t bar0_base)
{
    if (!qwifi_ce_prepare_htc(bar0_base))
        return 0;

    if (g_qwifi_htc_ready_seen)
        return 1;

    for (uint32_t round = 0; round < 2048u && !g_qwifi_htc_ready_seen; ++round)
    {
        qwifi_ce_poll_rx_pipe(bar0_base, 1u);
        qwifi_ce_poll_rx_pipe(bar0_base, 2u);
        short_delay();
        if ((round & 0x3FFu) == 0u)
        {
            terminal_print("[K:QWIFI] HTC READY wait round=");
            terminal_print_inline_hex64(round);
            terminal_flush_log();
        }
    }

    terminal_print("[K:QWIFI] HTC READY wait done seen=");
    terminal_print_inline_hex64(g_qwifi_htc_ready_seen ? 1u : 0u);
    terminal_flush_log();
    if (!g_qwifi_htc_ready_seen)
    {
        qwifi_ce_dump_rx_state(bar0_base, 1u);
        qwifi_ce_dump_rx_state(bar0_base, 2u);
    }
    return g_qwifi_htc_ready_seen ? 1 : 0;
}

static int qwifi_mhi_send_qrtr_control(uint64_t bar0_base,
                                       uint32_t chdboff,
                                       uint32_t type,
                                       uint32_t service,
                                       uint32_t instance,
                                       const char *label)
{
    return qwifi_mhi_send_qrtr_control_to(bar0_base,
                                          chdboff,
                                          type,
                                          service,
                                          instance,
                                          QWIFI_QRTR_LOCAL_NODE,
                                          qwifi_qrtr_dst_node(),
                                          label);
}

static int qwifi_mhi_send_qrtr_hello(uint64_t bar0_base, uint32_t chdboff)
{
    return qwifi_mhi_send_qrtr_control(bar0_base,
                                       chdboff,
                                       QWIFI_QRTR_TYPE_HELLO,
                                       0u,
                                       0u,
                                       "HELLO");
}

static int qwifi_mhi_send_qrtr_lookup(uint64_t bar0_base, uint32_t chdboff)
{
    return qwifi_mhi_send_qrtr_control(bar0_base,
                                       chdboff,
                                       QWIFI_QRTR_TYPE_NEW_LOOKUP,
                                       QWIFI_QRTR_WLANFW_SERVICE,
                                       QWIFI_QRTR_WLANFW_INSTANCE_WCN7850,
                                       "NEW_LOOKUP");
}

static void qwifi_mhi_probe_qrtr_wlanfw(uint64_t bar0_base, uint32_t chdboff, const boot_info *bi)
{
    if (!qwifi_mhi_queue_ipcr_rx(bar0_base, chdboff))
    {
        terminal_print("[K:QWIFI] QRTR probe skipped: RX queue failed");
        terminal_flush_log();
        return;
    }

    qwifi_mhi_dump_ipcr_events("initial");

    if (g_qwifi_qrtr_remote_node && qwifi_mhi_send_qrtr_hello(bar0_base, chdboff))
        qwifi_mhi_wait_for_wlanfw_events("after-hello", 128u);

    if (!g_qwifi_qrtr_wlan_port && !qwifi_mhi_send_qrtr_lookup(bar0_base, chdboff))
    {
        terminal_print("[K:QWIFI] QRTR probe skipped: lookup TX failed");
        terminal_flush_log();
        return;
    }

    if (!g_qwifi_qrtr_wlan_port)
        qwifi_mhi_wait_for_wlanfw_events("after-lookup", 256u);

    terminal_print("[K:QWIFI] QRTR WLANFW wait done found=");
    terminal_print_inline_hex64(g_qwifi_qrtr_wlan_port ? 1u : 0u);
    terminal_flush_log();

    if (g_qwifi_qrtr_wlan_port &&
        qwifi_mhi_send_qmi_request(bar0_base,
                                   chdboff,
                                   QWIFI_QMI_WLANFW_PHY_CAP_REQ,
                                   QWIFI_QMI_TXN_PHY_CAP,
                                   0,
                                   0,
                                   "WLANFW_PHY_CAP_REQ"))
    {
        qwifi_mhi_wait_for_qmi_phy_cap_events("after-qmi-phy-cap", 256u);
        terminal_print("[K:QWIFI] QMI WLANFW PHY_CAP done seen=");
        terminal_print_inline_hex64(g_qwifi_qmi_phy_cap_resp_seen ? 1u : 0u);
        terminal_flush_log();
    }

    if (g_qwifi_qmi_phy_cap_resp_seen)
    {
        uint8_t ind_payload[64];
        uint16_t ind_len;

        qwifi_zero(ind_payload, sizeof(ind_payload));
        ind_len = qwifi_build_ind_register_payload(ind_payload, sizeof(ind_payload));
        terminal_print("[K:QWIFI] QMI IND_REGISTER payload len=");
        terminal_print_inline_hex64(ind_len);
        terminal_flush_log();

        if (ind_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_IND_REGISTER_REQ,
                                       QWIFI_QMI_TXN_IND_REGISTER,
                                       ind_payload,
                                       ind_len,
                                       "WLANFW_IND_REGISTER_REQ"))
        {
            qwifi_mhi_wait_for_qmi_ind_register_events("after-qmi-ind-register", 256u);
            terminal_print("[K:QWIFI] QMI WLANFW IND_REGISTER done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_ind_register_resp_seen ? 1u : 0u);
            terminal_flush_log();
        }
    }

    if (g_qwifi_qmi_ind_register_resp_seen)
    {
        uint8_t host_payload[128];
        uint16_t host_len;

        qwifi_zero(host_payload, sizeof(host_payload));
        host_len = qwifi_build_host_cap_payload(host_payload, sizeof(host_payload));
        terminal_print("[K:QWIFI] QMI HOST_CAP payload len=");
        terminal_print_inline_hex64(host_len);
        terminal_flush_log();

        if (host_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_HOST_CAP_REQ,
                                       QWIFI_QMI_TXN_HOST_CAP,
                                       host_payload,
                                       host_len,
                                       "WLANFW_HOST_CAP_REQ"))
        {
            qwifi_mhi_wait_for_qmi_host_cap_events("after-qmi-host-cap", 256u);
            terminal_print("[K:QWIFI] QMI WLANFW HOST_CAP done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_host_cap_resp_seen ? 1u : 0u);
            terminal_flush_log();

            if (g_qwifi_qmi_host_cap_resp_seen)
            {
                qwifi_mhi_wait_for_qmi_request_mem_events("after-qmi-host-cap-mem", 512u);
                terminal_print("[K:QWIFI] QMI WLANFW REQUEST_MEM done seen=");
                terminal_print_inline_hex64(g_qwifi_qmi_request_mem_ind_seen ? 1u : 0u);
                terminal_flush_log();
            }
        }
    }

    if (g_qwifi_qmi_request_mem_ind_seen)
    {
        uint8_t mem_payload[192];
        uint16_t mem_len = 0;

        if (qwifi_alloc_requested_mem_segments())
        {
            qwifi_zero(mem_payload, sizeof(mem_payload));
            mem_len = qwifi_build_respond_mem_payload(mem_payload, sizeof(mem_payload));
        }

        terminal_print("[K:QWIFI] QMI RESPOND_MEM payload len=");
        terminal_print_inline_hex64(mem_len);
        terminal_flush_log();

        if (mem_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_RESPOND_MEM_REQ,
                                       QWIFI_QMI_TXN_RESPOND_MEM,
                                       mem_payload,
                                       mem_len,
                                       "WLANFW_RESPOND_MEM_REQ"))
        {
            qwifi_mhi_wait_for_qmi_respond_mem_events("after-qmi-respond-mem", 256u);
            terminal_print("[K:QWIFI] QMI WLANFW RESPOND_MEM done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_respond_mem_resp_seen ? 1u : 0u);
            terminal_flush_log();

            if (g_qwifi_qmi_respond_mem_resp_seen)
            {
                qwifi_mhi_wait_for_qmi_fw_mem_ready_events("after-qmi-respond-mem-ready", 512u);
                terminal_print("[K:QWIFI] QMI WLANFW FW_MEM_READY done seen=");
                terminal_print_inline_hex64(g_qwifi_qmi_fw_mem_ready_ind_seen ? 1u : 0u);
                terminal_flush_log();
            }
        }
    }

    if (g_qwifi_qmi_fw_mem_ready_ind_seen)
    {
        g_qwifi_qmi_target_cap_resp_seen = 0u;
        if (qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_CAP_REQ,
                                       QWIFI_QMI_TXN_TARGET_CAP,
                                       0,
                                       0,
                                       "WLANFW_TARGET_CAP_REQ"))
        {
            qwifi_mhi_wait_for_qmi_target_cap_events("after-qmi-target-cap", 512u);
            terminal_print("[K:QWIFI] QMI WLANFW TARGET_CAP done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_target_cap_resp_seen ? 1u : 0u);
            terminal_flush_log();
            if (!qwifi_qmi_last_response_ok(QWIFI_QMI_WLANFW_CAP_REQ, QWIFI_QMI_TXN_TARGET_CAP, 1))
            {
                terminal_print("[K:QWIFI] TARGET_CAP rejected result=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
                terminal_print(" error=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
                terminal_flush_log();
                g_qwifi_qmi_target_cap_resp_seen = 0u;
            }
        }
    }

    if (g_qwifi_qmi_target_cap_resp_seen)
    {
        if (!qwifi_send_regdb_download(bar0_base, chdboff, bi))
        {
            terminal_print("[K:QWIFI] QMI WLANFW REGDB download failed/stopped");
            terminal_flush_log();
            g_qwifi_qmi_bdf_download_resp_seen = 0u;
        }
        else if (!qwifi_send_bdf_download(bar0_base, chdboff, bi))
        {
            terminal_print("[K:QWIFI] QMI WLANFW BDF download failed/stopped");
            terminal_flush_log();
            g_qwifi_qmi_bdf_download_resp_seen = 0u;
        }
    }

    if (g_qwifi_qmi_target_cap_resp_seen && g_qwifi_qmi_bdf_download_resp_seen)
    {
        uint8_t m3_payload[32];
        uint64_t m3_phys = 0;
        uint32_t m3_size = 0;
        uint16_t m3_len = 0;

        if (qwifi_prepare_m3_blob(bi, &m3_phys, &m3_size))
        {
            qwifi_zero(m3_payload, sizeof(m3_payload));
            m3_len = qwifi_build_m3_info_payload(m3_payload, sizeof(m3_payload), m3_phys, m3_size);
        }

        terminal_print("[K:QWIFI] QMI M3_INFO payload len=");
        terminal_print_inline_hex64(m3_len);
        terminal_flush_log();

        g_qwifi_qmi_m3_info_resp_seen = 0u;
        if (m3_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_M3_INFO_REQ,
                                       QWIFI_QMI_TXN_M3_INFO,
                                       m3_payload,
                                       m3_len,
                                       "WLANFW_M3_INFO_REQ"))
        {
            qwifi_mhi_wait_for_qmi_m3_info_events("after-qmi-m3-info", 512u);
            terminal_print("[K:QWIFI] QMI WLANFW M3_INFO done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_m3_info_resp_seen ? 1u : 0u);
            terminal_flush_log();
            if (!qwifi_qmi_last_response_ok(QWIFI_QMI_WLANFW_M3_INFO_REQ, QWIFI_QMI_TXN_M3_INFO, 1))
            {
                terminal_print("[K:QWIFI] M3_INFO rejected result=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
                terminal_print(" error=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
                terminal_flush_log();
                g_qwifi_qmi_m3_info_resp_seen = 0u;
            }
        }
    }

    if (g_qwifi_qmi_m3_info_resp_seen)
    {
        qwifi_mhi_wait_for_qmi_fw_ready_events("after-qmi-m3-fw-ready", 1024u);
        terminal_print("[K:QWIFI] QMI WLANFW FW_READY done seen=");
        terminal_print_inline_hex64(g_qwifi_qmi_fw_ready_ind_seen ? 1u : 0u);
        terminal_flush_log();
    }

    if (g_qwifi_qmi_fw_ready_ind_seen)
    {
        uint8_t ini_payload[16];
        uint16_t ini_len;

        qwifi_zero(ini_payload, sizeof(ini_payload));
        ini_len = qwifi_build_wlan_ini_payload(ini_payload, sizeof(ini_payload));
        terminal_print("[K:QWIFI] QMI WLAN_INI payload len=");
        terminal_print_inline_hex64(ini_len);
        terminal_flush_log();

        g_qwifi_qmi_wlan_ini_resp_seen = 0u;
        if (ini_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_WLAN_INI_REQ,
                                       QWIFI_QMI_TXN_WLAN_INI,
                                       ini_payload,
                                       ini_len,
                                       "WLANFW_WLAN_INI_REQ"))
        {
            qwifi_mhi_wait_for_qmi_wlan_ini_events("after-qmi-wlan-ini", 512u);
            terminal_print("[K:QWIFI] QMI WLANFW WLAN_INI done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_wlan_ini_resp_seen ? 1u : 0u);
            terminal_flush_log();
            if (!qwifi_qmi_last_response_ok(QWIFI_QMI_WLANFW_WLAN_INI_REQ, QWIFI_QMI_TXN_WLAN_INI, 1))
            {
                terminal_print("[K:QWIFI] WLAN_INI rejected result=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
                terminal_print(" error=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
                terminal_flush_log();
                g_qwifi_qmi_wlan_ini_resp_seen = 0u;
            }
        }
    }

    if (g_qwifi_qmi_wlan_ini_resp_seen)
    {
        uint8_t cfg_payload[512];
        uint16_t cfg_len;

        qwifi_zero(cfg_payload, sizeof(cfg_payload));
        cfg_len = qwifi_build_wlan_cfg_payload(cfg_payload, sizeof(cfg_payload));
        terminal_print("[K:QWIFI] QMI WLAN_CFG payload len=");
        terminal_print_inline_hex64(cfg_len);
        terminal_flush_log();

        g_qwifi_qmi_wlan_cfg_resp_seen = 0u;
        if (cfg_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_WLAN_CFG_REQ,
                                       QWIFI_QMI_TXN_WLAN_CFG,
                                       cfg_payload,
                                       cfg_len,
                                       "WLANFW_WLAN_CFG_REQ"))
        {
            qwifi_mhi_wait_for_qmi_wlan_cfg_events("after-qmi-wlan-cfg", 512u);
            terminal_print("[K:QWIFI] QMI WLANFW WLAN_CFG done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_wlan_cfg_resp_seen ? 1u : 0u);
            terminal_flush_log();
            if (!qwifi_qmi_last_response_ok(QWIFI_QMI_WLANFW_WLAN_CFG_REQ, QWIFI_QMI_TXN_WLAN_CFG, 1))
            {
                terminal_print("[K:QWIFI] WLAN_CFG rejected result=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
                terminal_print(" error=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
                terminal_flush_log();
                g_qwifi_qmi_wlan_cfg_resp_seen = 0u;
            }
        }
    }

    if (g_qwifi_qmi_wlan_cfg_resp_seen)
    {
        uint8_t mode_payload[32];
        uint16_t mode_len;

        qwifi_zero(mode_payload, sizeof(mode_payload));
        mode_len = qwifi_build_wlan_mode_payload(mode_payload, sizeof(mode_payload));
        terminal_print("[K:QWIFI] QMI WLAN_MODE payload len=");
        terminal_print_inline_hex64(mode_len);
        terminal_flush_log();

        g_qwifi_qmi_wlan_mode_resp_seen = 0u;
        if (mode_len &&
            qwifi_mhi_send_qmi_request(bar0_base,
                                       chdboff,
                                       QWIFI_QMI_WLANFW_WLAN_MODE_REQ,
                                       QWIFI_QMI_TXN_WLAN_MODE,
                                       mode_payload,
                                       mode_len,
                                       "WLANFW_WLAN_MODE_REQ"))
        {
            qwifi_mhi_wait_for_qmi_wlan_mode_events("after-qmi-wlan-mode", 512u);
            terminal_print("[K:QWIFI] QMI WLANFW WLAN_MODE done seen=");
            terminal_print_inline_hex64(g_qwifi_qmi_wlan_mode_resp_seen ? 1u : 0u);
            terminal_flush_log();
            if (!qwifi_qmi_last_response_ok(QWIFI_QMI_WLANFW_WLAN_MODE_REQ, QWIFI_QMI_TXN_WLAN_MODE, 1))
            {
                terminal_print("[K:QWIFI] WLAN_MODE rejected result=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_result);
                terminal_print(" error=");
                terminal_print_inline_hex64(g_qwifi_qmi_last_resp_error);
                terminal_flush_log();
                g_qwifi_qmi_wlan_mode_resp_seen = 0u;
            }
        }
    }

    if (g_qwifi_qmi_wlan_mode_resp_seen)
    {
        terminal_print("[K:QWIFI] QMI complete; waiting for CE/HTC control path");
        terminal_flush_log();
        if (qwifi_ce_wait_htc_ready(bar0_base))
        {
            terminal_print("[K:QWIFI] HTC control path online; WMI connect is next");
            terminal_flush_log();
        }
    }
}

static int qwifi_mhi_start_ipcr_channels(uint64_t bar0_base, uint32_t chdboff, const boot_info *bi)
{
    static int attempted = 0;
    uint64_t tx_pa;
    uint64_t rx_pa;
    uint64_t ring_bytes = QWIFI_MHI_IPCR_ELEMENTS * QWIFI_MHI_TRE_BYTES;
    int tx_ok;
    int rx_ok;

    if (attempted)
        return 0;
    attempted = 1;
    g_qwifi_ipcr_bar0_base = bar0_base;
    g_qwifi_ipcr_chdboff = chdboff;

    if (!g_qwifi_chan_ctxt || g_qwifi_chan_count <= QWIFI_MHI_IPCR_RX_CH)
    {
        terminal_print("[K:QWIFI] IPCR start skipped: channel context unavailable");
        terminal_flush_log();
        return 0;
    }

    if (!g_qwifi_ipcr_tx_ring)
        g_qwifi_ipcr_tx_ring = pmem_alloc_pages_lowdma(qwifi_pages_for(ring_bytes));
    if (!g_qwifi_ipcr_rx_ring)
        g_qwifi_ipcr_rx_ring = pmem_alloc_pages_lowdma(qwifi_pages_for(ring_bytes));
    if (!g_qwifi_ipcr_tx_ring || !g_qwifi_ipcr_rx_ring)
    {
        terminal_print("[K:QWIFI] IPCR ring alloc failed");
        terminal_flush_log();
        return 0;
    }

    qwifi_zero(g_qwifi_ipcr_tx_ring, ring_bytes);
    qwifi_zero(g_qwifi_ipcr_rx_ring, ring_bytes);
    tx_pa = pmem_virt_to_phys(g_qwifi_ipcr_tx_ring);
    rx_pa = pmem_virt_to_phys(g_qwifi_ipcr_rx_ring);

    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].chcfg =
        QWIFI_MHI_CH_STATE_ENABLED | (QWIFI_MHI_DB_BRST_DISABLE << 8);
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].chtype = QWIFI_MHI_CH_TYPE_OUTBOUND;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].erindex = 1u;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].rbase = tx_pa;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].rlen = ring_bytes;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].rp = tx_pa;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH].wp = tx_pa;

    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].chcfg =
        QWIFI_MHI_CH_STATE_ENABLED | (QWIFI_MHI_DB_BRST_DISABLE << 8);
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].chtype = QWIFI_MHI_CH_TYPE_INBOUND;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].erindex = 1u;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].rbase = rx_pa;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].rlen = ring_bytes;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].rp = rx_pa;
    g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH].wp = rx_pa;

    asm_dma_clean_range(g_qwifi_ipcr_tx_ring, ring_bytes);
    asm_dma_clean_range(g_qwifi_ipcr_rx_ring, ring_bytes);
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_TX_CH], sizeof(g_qwifi_chan_ctxt[0]));
    asm_dma_clean_range(&g_qwifi_chan_ctxt[QWIFI_MHI_IPCR_RX_CH], sizeof(g_qwifi_chan_ctxt[0]));

    terminal_print("[K:QWIFI] IPCR rings tx_pa=");
    terminal_print_inline_hex64(tx_pa);
    terminal_print(" rx_pa=");
    terminal_print_inline_hex64(rx_pa);
    terminal_print(" bytes=");
    terminal_print_inline_hex64(ring_bytes);
    terminal_flush_log();

    qwifi_mhi_dump_event_ring(0u, "before-start-ipcr");
    tx_ok = qwifi_mhi_send_start_channel_cmd(bar0_base, QWIFI_MHI_IPCR_TX_CH);
    rx_ok = qwifi_mhi_send_start_channel_cmd(bar0_base, QWIFI_MHI_IPCR_RX_CH);
    qwifi_mhi_dump_event_ring(0u, "after-start-ipcr");

    terminal_print("[K:QWIFI] IPCR start result tx=");
    terminal_print_inline_hex64(tx_ok ? 1u : 0u);
    terminal_print(" rx=");
    terminal_print_inline_hex64(rx_ok ? 1u : 0u);
    terminal_flush_log();

    if (tx_ok && rx_ok)
        qwifi_mhi_probe_qrtr_wlanfw(bar0_base, chdboff, bi);

    return tx_ok && rx_ok;
}

static int qwifi_mhi_wait_ready_and_enter_m0(uint64_t bar0_base,
                                             uint32_t bhioff,
                                             uint32_t mhicfg_hint)
{
    uint32_t mhictrl = 0;
    uint32_t mhistatus = 0;
    uint32_t mhicfg = 0;
    uint32_t execenv = 0;
    uint32_t poll;
    uint32_t ready = 0;
    uint32_t reset = 1;
    int rc = 0;

    for (poll = 0; poll < 4000000u; ++poll)
    {
        (void)pci_probe_config_read32(bar0_base + ATH12K_MHICTRL, &mhictrl);
        (void)pci_probe_config_read32(bar0_base + ATH12K_MHISTATUS, &mhistatus);
        if (bhioff != 0u && bhioff < 0x1000u)
            (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_EXECENV, &execenv);

        reset = (mhictrl & ATH12K_MHICTRL_RESET_MASK) ? 1u : 0u;
        ready = (mhistatus & ATH12K_MHISTATUS_READY_MASK) ? 1u : 0u;
        if (!reset && ready)
            break;
        asm_relax();
    }

    (void)pci_probe_config_read32(bar0_base + ATH12K_MHICFG, &mhicfg);
    terminal_print("[K:QWIFI] post-SBL READY poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" reset=");
    terminal_print_inline_hex64(reset);
    terminal_print(" ready=");
    terminal_print_inline_hex64(ready);
    terminal_print(" mhictrl=");
    terminal_print_inline_hex64(mhictrl);
    terminal_print(" mhistatus=");
    terminal_print_inline_hex64(mhistatus);
    terminal_print(" mhicfg=");
    terminal_print_inline_hex64(mhicfg);
    terminal_print(" execenv=");
    terminal_print_inline_hex64(execenv);
    terminal_flush_log();

    if (reset || !ready)
        return 0;

    qwifi_mhi_program_minimal_contexts(bar0_base, mhicfg ? mhicfg : mhicfg_hint);

    rc = qwifi_mhi_write32(bar0_base, ATH12K_MHICTRL, QWIFI_MHI_STATE_M0 << 8);
    asm_mmio_barrier();
    (void)pci_probe_config_read32(bar0_base + ATH12K_MHICTRL, &mhictrl);
    (void)pci_probe_config_read32(bar0_base + ATH12K_MHISTATUS, &mhistatus);

    terminal_print("[K:QWIFI] MHI enter M0 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" mhictrl=");
    terminal_print_inline_hex64(mhictrl);
    terminal_print(" mhistatus=");
    terminal_print_inline_hex64(mhistatus);
    terminal_flush_log();

    return rc == 0;
}

static int qwifi_bhie_try_load_amss(uint64_t bar0_base, uint32_t bhioff, uint32_t bhieoff, const boot_info *bi)
{
    static int attempted = 0;
    uint64_t amss_phys = 0;
    uint64_t amss_size = 0;
    uint64_t copied = 0;
    uint32_t seg_count;
    uint64_t vec_bytes;
    qwifi_bhi_vec_entry *vec;
    uint64_t vec_phys;
    uint32_t status = 0;
    uint32_t status_code = 0;
    uint32_t execenv = 0;
    uint32_t mhistatus = 0;
    uint32_t poll;
    const uint32_t sequence = 2u;
    int rc = 0;

    if (attempted)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: already attempted");
        terminal_flush_log();
        return 0;
    }
    attempted = 1;

    if (!bar0_base || !bhieoff || bhieoff >= 0x1000u)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: invalid BHIE offset");
        terminal_flush_log();
        return 0;
    }

    if (!qwifi_get_fw_blob(bi, BOOTINFO_WIFI_FW_AMSS, &amss_phys, &amss_size))
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: AMSS firmware missing");
        terminal_flush_log();
        return 0;
    }

    seg_count = (uint32_t)((amss_size + QWIFI_BHIE_SEG_BYTES - 1u) / QWIFI_BHIE_SEG_BYTES);
    if (!seg_count || seg_count > QWIFI_BHIE_MAX_SEGMENTS)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: segment count=");
        terminal_print_inline_hex64(seg_count);
        terminal_print(" fw_size=");
        terminal_print_inline_hex64(amss_size);
        terminal_flush_log();
        return 0;
    }

    vec_bytes = sizeof(qwifi_bhi_vec_entry) * (uint64_t)seg_count;
    vec = (qwifi_bhi_vec_entry *)pmem_alloc_pages_lowdma(qwifi_pages_for(vec_bytes));
    if (!vec)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: vec alloc failed bytes=");
        terminal_print_inline_hex64(vec_bytes);
        terminal_flush_log();
        return 0;
    }
    qwifi_zero(vec, vec_bytes);

    for (uint32_t i = 0; i < seg_count; ++i)
    {
        uint64_t chunk = amss_size - copied;
        void *seg_buf;
        uint64_t seg_phys;

        if (chunk > QWIFI_BHIE_SEG_BYTES)
            chunk = QWIFI_BHIE_SEG_BYTES;

        seg_buf = pmem_alloc_pages_lowdma(qwifi_pages_for(chunk));
        if (!seg_buf)
        {
            terminal_print("[K:QWIFI] BHIE AMSS segment alloc failed i=");
            terminal_print_inline_hex64(i);
            terminal_print(" bytes=");
            terminal_print_inline_hex64(chunk);
            terminal_flush_log();
            return 0;
        }

        qwifi_copy_from_phys(seg_buf, amss_phys + copied, chunk);
        asm_dma_clean_range(seg_buf, chunk);
        seg_phys = pmem_virt_to_phys(seg_buf);
        vec[i].dma_addr = seg_phys;
        vec[i].size = chunk;
        copied += chunk;
    }

    asm_dma_clean_range(vec, vec_bytes);
    vec_phys = pmem_virt_to_phys(vec);

    terminal_print("[K:QWIFI] BHIE AMSS load begin fw_pa=");
    terminal_print_inline_hex64(amss_phys);
    terminal_print(" fw_size=");
    terminal_print_inline_hex64(amss_size);
    terminal_print(" segments=");
    terminal_print_inline_hex64(seg_count);
    terminal_print(" vec_pa=");
    terminal_print_inline_hex64(vec_phys);
    terminal_print(" vec_bytes=");
    terminal_print_inline_hex64(vec_bytes);
    terminal_flush_log();

    rc |= qwifi_mhi_write64_pair(bar0_base,
                                 bhieoff + ATH12K_BHIE_TXVECADDR_LOW,
                                 bhieoff + ATH12K_BHIE_TXVECADDR_HIGH,
                                 vec_phys);
    rc |= qwifi_mhi_write32(bar0_base, bhieoff + ATH12K_BHIE_TXVECSIZE, (uint32_t)vec_bytes);
    terminal_print("[K:QWIFI] BHIE AMSS programmed rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    asm_mmio_barrier();
    rc = qwifi_mhi_write32(bar0_base, bhieoff + ATH12K_BHIE_TXVECDB, sequence);
    terminal_print("[K:QWIFI] BHIE AMSS doorbell rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" seq=");
    terminal_print_inline_hex64(sequence);
    terminal_flush_log();
    if (rc != 0)
        return 0;
    asm_mmio_barrier();

    for (poll = 0; poll < 8000000u; ++poll)
    {
        rc = pci_probe_config_read32(bar0_base + bhieoff + ATH12K_BHIE_TXVECSTATUS, &status);
        if (rc != 0)
            break;
        status_code = qwifi_bhi_status_code(status);
        if (status_code != 0u)
            break;
        asm_relax();
    }

    terminal_print("[K:QWIFI] BHIE AMSS result rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" status=");
    terminal_print_inline_hex64(status);
    terminal_print(" code=");
    terminal_print_inline_hex64(status_code);
    terminal_print(" seq_seen=");
    terminal_print_inline_hex64(status & 0x3FFFFFFFu);
    if (status_code == QWIFI_BHIE_STATUS_XFER_COMPL)
        terminal_print(" [AMSS accepted]");
    else if (status_code == QWIFI_BHIE_STATUS_ERROR)
        terminal_print(" [AMSS error]");
    else if (rc == 0)
        terminal_print(" [AMSS timeout]");
    terminal_flush_log();

    if (bhioff != 0u && bhioff < 0x1000u)
    {
        for (poll = 0; poll < 4000000u; ++poll)
        {
            (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_EXECENV, &execenv);
            if (execenv > 1u)
                break;
            asm_relax();
        }
    }

    (void)pci_probe_config_read32(bar0_base + ATH12K_MHISTATUS, &mhistatus);
    (void)pci_probe_config_read32(bar0_base + bhieoff + ATH12K_BHIE_TXVECSTATUS, &status);
    terminal_print("[K:QWIFI] post-AMSS mhistatus=");
    terminal_print_inline_hex64(mhistatus);
    terminal_print(" bhie_status=");
    terminal_print_inline_hex64(status);
    terminal_print(" execenv=");
    terminal_print_inline_hex64(execenv);
    terminal_flush_log();

    return status_code == QWIFI_BHIE_STATUS_XFER_COMPL;
}

static void qwifi_dump_ath12k_probe_regs(uint64_t bar0_base, const boot_info *bi)
{
    static const struct
    {
        const char *name;
        uint32_t off;
    } mhi_regs[] = {
        {"MHIREGLEN", ATH12K_MHIREGLEN},
        {"MHIVER", ATH12K_MHIVER},
        {"MHICFG", ATH12K_MHICFG},
        {"CHDBOFF", ATH12K_CHDBOFF},
        {"ERDBOFF", ATH12K_ERDBOFF},
        {"BHIOFF", ATH12K_BHIOFF},
        {"BHIEOFF", ATH12K_BHIEOFF},
        {"DEBUGOFF", ATH12K_DEBUGOFF},
        {"MHICTRL", ATH12K_MHICTRL},
        {"MHISTATUS", ATH12K_MHISTATUS},
        {"PCIE_TXVECDB", ATH12K_PCIE_TXVECDB},
        {"PCIE_TXVECSTATUS", ATH12K_PCIE_TXVECSTATUS},
        {"PCIE_RXVECDB", ATH12K_PCIE_RXVECDB},
        {"PCIE_RXVECSTATUS", ATH12K_PCIE_RXVECSTATUS},
    };
    static const struct
    {
        const char *name;
        uint32_t rel;
    } bhi_regs[] = {
        {"BHI_VERSION_MINOR", ATH12K_BHI_VERSION_MINOR},
        {"BHI_VERSION_MAJOR", ATH12K_BHI_VERSION_MAJOR},
        {"BHI_EXECENV", ATH12K_BHI_EXECENV},
        {"BHI_STATUS", ATH12K_BHI_STATUS},
        {"BHI_ERRCODE", ATH12K_BHI_ERRCODE},
        {"BHI_ERRDBG1", ATH12K_BHI_ERRDBG1},
        {"BHI_ERRDBG2", ATH12K_BHI_ERRDBG2},
        {"BHI_ERRDBG3", ATH12K_BHI_ERRDBG3},
        {"BHI_SERIALNU", ATH12K_BHI_SERIALNU},
    };
    static const struct
    {
        const char *name;
        uint32_t rel;
    } bhie_regs[] = {
        {"BHIE_MSMSOCID", ATH12K_BHIE_MSMSOCID},
        {"BHIE_TXVECSTATUS", ATH12K_BHIE_TXVECSTATUS},
        {"BHIE_RXVECSTATUS", ATH12K_BHIE_RXVECSTATUS},
    };
    uint32_t mhicfg = 0;
    uint32_t mhictrl = 0;
    uint32_t mhistatus = 0;
    uint32_t chdboff = 0;
    uint32_t bhioff = 0;
    uint32_t bhieoff = 0;
    uint32_t bhi_status = 0;
    uint32_t value = 0;
    int sbl_ok = 0;
    int rc;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(mhi_regs) / sizeof(mhi_regs[0])); ++i)
    {
        rc = pci_probe_config_read32(bar0_base + mhi_regs[i].off, &value);
        terminal_print("[K:QWIFI] MHI reg ");
        terminal_print(mhi_regs[i].name);
        terminal_print(" off=");
        terminal_print_inline_hex64(mhi_regs[i].off);
        terminal_print(" rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_print(" value=");
        terminal_print_inline_hex64(value);
        terminal_flush_log();
        if (rc != 0)
            break;

        if (mhi_regs[i].off == ATH12K_MHICFG)
            mhicfg = value;
        else if (mhi_regs[i].off == ATH12K_MHICTRL)
            mhictrl = value;
        else if (mhi_regs[i].off == ATH12K_MHISTATUS)
            mhistatus = value;
        else if (mhi_regs[i].off == ATH12K_CHDBOFF)
            chdboff = value;
        else if (mhi_regs[i].off == ATH12K_BHIOFF)
            bhioff = value;
        else if (mhi_regs[i].off == ATH12K_BHIEOFF)
            bhieoff = value;
    }

    terminal_print("[K:QWIFI] MHI decode nch=");
    terminal_print_inline_hex64(mhicfg & ATH12K_MHICFG_NCH_MASK);
    terminal_print(" nhwch=");
    terminal_print_inline_hex64((mhicfg & ATH12K_MHICFG_NHWCH_MASK) >> 8);
    terminal_print(" ner=");
    terminal_print_inline_hex64((mhicfg & ATH12K_MHICFG_NER_MASK) >> 16);
    terminal_print(" nhwer=");
    terminal_print_inline_hex64((mhicfg & ATH12K_MHICFG_NHWER_MASK) >> 24);
    terminal_print(" ctrl_state=");
    terminal_print_inline_hex64((mhictrl & ATH12K_MHICTRL_MHISTATE_MASK) >> 8);
    terminal_print(" ctrl_reset=");
    terminal_print_inline_hex64((mhictrl & ATH12K_MHICTRL_RESET_MASK) ? 1u : 0u);
    terminal_print(" status_state=");
    terminal_print_inline_hex64((mhistatus & ATH12K_MHISTATUS_MHISTATE_MASK) >> 8);
    terminal_print(" ready=");
    terminal_print_inline_hex64((mhistatus & ATH12K_MHISTATUS_READY_MASK) ? 1u : 0u);
    terminal_print(" syserr=");
    terminal_print_inline_hex64((mhistatus & ATH12K_MHISTATUS_SYSERR_MASK) ? 1u : 0u);
    terminal_flush_log();

    qwifi_mhi_program_minimal_contexts(bar0_base, mhicfg);

    if (bhioff != 0u && bhioff < 0x1000u)
    {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(bhi_regs) / sizeof(bhi_regs[0])); ++i)
        {
            rc = pci_probe_config_read32(bar0_base + bhioff + bhi_regs[i].rel, &value);
            terminal_print("[K:QWIFI] BHI reg ");
            terminal_print(bhi_regs[i].name);
            terminal_print(" off=");
            terminal_print_inline_hex64(bhioff + bhi_regs[i].rel);
            terminal_print(" rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            terminal_print(" value=");
            terminal_print_inline_hex64(value);
            terminal_flush_log();
            if (rc != 0)
                break;
            if (bhi_regs[i].rel == ATH12K_BHI_STATUS)
                bhi_status = value;
        }

        terminal_print("[K:QWIFI] BHI decode status=");
        terminal_print_inline_hex64((bhi_status >> 30) & 0x3u);
        terminal_print(" raw=");
        terminal_print_inline_hex64(bhi_status);
        terminal_flush_log();

        sbl_ok = qwifi_bhi_try_load_sbl(bar0_base, bhioff, bi);
    }

    if (bhieoff != 0u && bhieoff < 0x1000u)
    {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(bhie_regs) / sizeof(bhie_regs[0])); ++i)
        {
            rc = pci_probe_config_read32(bar0_base + bhieoff + bhie_regs[i].rel, &value);
            terminal_print("[K:QWIFI] BHIE reg ");
            terminal_print(bhie_regs[i].name);
            terminal_print(" off=");
            terminal_print_inline_hex64(bhieoff + bhie_regs[i].rel);
            terminal_print(" rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            terminal_print(" value=");
            terminal_print_inline_hex64(value);
            terminal_flush_log();
            if (rc != 0)
                break;
        }
    }

    if (sbl_ok && bhieoff != 0u && bhieoff < 0x1000u)
    {
        int m0_ok = qwifi_mhi_wait_ready_and_enter_m0(bar0_base, bhioff, mhicfg);
        if (m0_ok)
        {
            int amss_ok = qwifi_bhie_try_load_amss(bar0_base, bhioff, bhieoff, bi);
            if (amss_ok)
                (void)qwifi_mhi_start_ipcr_channels(bar0_base, chdboff, bi);
        }
        else
        {
            terminal_print("[K:QWIFI] BHIE AMSS skipped: MHI did not reach READY/M0");
            terminal_flush_log();
        }
    }

    value = 0;
    rc = qwifi_ath12k_read32(bar0_base, ATH12K_TCSR_SOC_HW_VERSION, &value);
    terminal_print("[K:QWIFI] ath12k TCSR_SOC_HW_VERSION rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" value=");
    terminal_print_inline_hex64(value);
    if (rc == 0)
    {
        uint32_t major = (value & ATH12K_TCSR_SOC_HW_VERSION_MAJOR_MASK) >> 8;
        uint32_t minor = (value & ATH12K_TCSR_SOC_HW_VERSION_MINOR_MASK) >> 4;
        terminal_print(" major=");
        terminal_print_inline_hex64(major);
        terminal_print(" minor=");
        terminal_print_inline_hex64(minor);
        if (major == 2u)
            terminal_print(" [WCN7850 hw2.x expected]");
    }
    terminal_flush_log();
}

static void qwifi_acpi_exec0(uint64_t rsdp, const char dev_name4[4], const char method_name4[4])
{
    uint64_t ret = 0;
    int rc;

    terminal_print("[K:QWIFI] ACPI ");
    terminal_print(dev_name4);
    terminal_print(".");
    terminal_print(method_name4);
    terminal_flush_log();

    rc = acpi_probe_net_exec_device_method(rsdp, dev_name4, method_name4, &ret);

    terminal_print("[K:QWIFI] ACPI result ");
    terminal_print(dev_name4);
    terminal_print(".");
    terminal_print(method_name4);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" ret=");
    terminal_print_inline_hex64(ret);
    terminal_flush_log();
}

static void qwifi_acpi_prekick(const boot_info *bi)
{
    uint64_t reg_args[2];

    if (!bi || !bi->acpi_rsdp)
        return;

    terminal_print("[K:QWIFI] hardcoded ACPI path: _SB.PCI4.RP1_.WLN_");
    terminal_flush_log();

    acpi_probe_net_exec_context_reset();

    reg_args[0] = 0x08u;
    reg_args[1] = 1u;
    terminal_print("[K:QWIFI] ACPI GIO0._REG(0x08,1)");
    terminal_flush_log();
    (void)acpi_probe_net_exec_device_method_args(bi->acpi_rsdp, "GIO0", "_REG", 2u, reg_args, 0);

    qwifi_acpi_exec0(bi->acpi_rsdp, "PCI4", "_STA");
    qwifi_acpi_exec0(bi->acpi_rsdp, "PCI4", "_PS0");
    qwifi_acpi_exec0(bi->acpi_rsdp, "RP1_", "_PS0");
}

static int qwifi_find_ecam(uint64_t rsdp_phys, dihos_pci_ecam *out_ecam)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count;

    if (!out_ecam || !rsdp_phys)
        return 0;

    count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);
    terminal_print("[K:QWIFI] MCFG ECAM count=");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        terminal_print("[K:QWIFI] ECAM seg=");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" base=");
        terminal_print_inline_hex64(ecams[i].base);
        terminal_print(" buses=");
        terminal_print_inline_hex64(ecams[i].start_bus);
        terminal_print("-");
        terminal_print_inline_hex64(ecams[i].end_bus);

        if (ecams[i].segment == QCOM_FC7800_SEGMENT &&
            QCOM_FC7800_BUS >= ecams[i].start_bus &&
            QCOM_FC7800_BUS <= ecams[i].end_bus)
        {
            *out_ecam = ecams[i];
            terminal_print("[K:QWIFI] selected segment 4 ECAM");
            terminal_flush_log();
            return 1;
        }
    }

    terminal_print("[K:QWIFI] segment 4 ECAM not found");
    terminal_flush_log();
    return 0;
}

static void qwifi_enable_and_dump_bar0(const boot_info *bi,
                                       uint64_t cfg,
                                       uint32_t cmd_status,
                                       uint32_t bar0,
                                       uint32_t bar1)
{
    uint64_t bar0_base = qwifi_bar0_base_from_config(bar0, bar1);
    uint16_t cmd = (uint16_t)(cmd_status & 0xFFFFu);
    uint16_t wanted_cmd = (uint16_t)(cmd | 0x0006u); /* Memory Space + Bus Master */
    uint32_t readback = 0;
    int rc;

    terminal_print("[K:QWIFI] BAR0 decoded base=");
    terminal_print_inline_hex64(bar0_base);
    terminal_print(" raw0=");
    terminal_print_inline_hex64(bar0);
    terminal_print(" raw1=");
    terminal_print_inline_hex64(bar1);
    terminal_flush_log();

    if (!bar0_base)
    {
        terminal_print("[K:QWIFI] BAR0 unavailable; skip MMIO dump");
        terminal_flush_log();
        return;
    }

    if ((cmd & 0x0006u) != 0x0006u)
    {
        terminal_print("[K:QWIFI] enable PCI command MEM|BUSMASTER old=");
        terminal_print_inline_hex64(cmd);
        terminal_print(" new=");
        terminal_print_inline_hex64(wanted_cmd);
        terminal_flush_log();

        rc = pci_probe_mmio_write32(cfg + 0x04u, (uint32_t)wanted_cmd);
        terminal_print("[K:QWIFI] command write rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();

        (void)pci_probe_config_read32(cfg + 0x04u, &readback);
        terminal_print("[K:QWIFI] command readback=");
        terminal_print_inline_hex64(readback);
        terminal_flush_log();
    }

    if (ecam_overlaps_efi_ram(bi, bar0_base, QWIFI_BAR0_PROBE_BYTES))
    {
        terminal_print("[K:QWIFI] skip BAR0 map: overlaps EFI RAM");
        terminal_flush_log();
        return;
    }

    rc = mmio_map_device_identity(bar0_base, QWIFI_BAR0_PROBE_BYTES);
    terminal_print("[K:QWIFI] BAR0 map rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" bytes=");
    terminal_print_inline_hex64(QWIFI_BAR0_PROBE_BYTES);
    terminal_flush_log();
    if (rc != 0)
        return;

    qwifi_dump_ath12k_probe_regs(bar0_base, bi);
}

static uint32_t qwifi_probe_one_function(const boot_info *bi,
                                         const dihos_pci_ecam *ecam,
                                         const char *label,
                                         uint8_t bus,
                                         uint8_t dev,
                                         uint8_t fn,
                                         uint16_t expected_device)
{
    uint64_t cfg;
    uint64_t page;
    uint32_t id = 0;
    uint32_t cmd = 0;
    uint32_t classrev = 0;
    uint32_t hdr = 0;
    uint32_t bar0 = 0;
    uint32_t bar1 = 0;
    uint32_t cap = 0;
    uint16_t vendor;
    uint16_t device;
    int rc;

    cfg = pci_ecam_config_phys(ecam, bus, dev, fn, 0u);
    if (!cfg)
    {
        terminal_print("[K:QWIFI] cfg unavailable for ");
        terminal_print(label);
        return 0;
    }

    page = cfg & ~0xFFFull;
    terminal_print("[K:QWIFI] probe ");
    terminal_print(label);
    terminal_print(" seg=");
    terminal_print_inline_hex64(QCOM_FC7800_SEGMENT);
    terminal_print(" bdf=");
    terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8) | (uint64_t)fn);
    terminal_print(" cfg=");
    terminal_print_inline_hex64(cfg);
    terminal_print(" page=");
    terminal_print_inline_hex64(page);
    terminal_flush_log();

    if (ecam_overlaps_efi_ram(bi, page, 0x1000ull))
    {
        terminal_print("[K:QWIFI] skip cfg page: overlaps EFI RAM");
        terminal_flush_log();
        return 0;
    }

    rc = mmio_map_device_identity(page, 0x1000ull);
    terminal_print("[K:QWIFI] cfg page map rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    terminal_print("[K:QWIFI] read id32 ");
    terminal_print(label);
    terminal_flush_log();
    rc = pci_probe_config_read32(cfg + 0x00u, &id);
    terminal_print("[K:QWIFI] id32 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" id=");
    terminal_print_inline_hex64(id);
    terminal_flush_log();
    if (rc != 0 || id == 0u || id == 0xFFFFFFFFu)
        return 0;

    vendor = (uint16_t)(id & 0xFFFFu);
    device = (uint16_t)((id >> 16) & 0xFFFFu);

    (void)pci_probe_config_read32(cfg + 0x04u, &cmd);
    (void)pci_probe_config_read32(cfg + 0x08u, &classrev);
    (void)pci_probe_config_read32(cfg + 0x0Cu, &hdr);
    (void)pci_probe_config_read32(cfg + 0x10u, &bar0);
    (void)pci_probe_config_read32(cfg + 0x14u, &bar1);
    (void)pci_probe_config_read32(cfg + 0x34u, &cap);

    terminal_print("[K:QWIFI] fn ");
    terminal_print(label);
    terminal_print(" vid=");
    terminal_print_inline_hex64(vendor);
    terminal_print(" did=");
    terminal_print_inline_hex64(device);
    terminal_print(" cmd=");
    terminal_print_inline_hex64(cmd);
    terminal_print(" classrev=");
    terminal_print_inline_hex64(classrev);
    terminal_print(" hdr=");
    terminal_print_inline_hex64(hdr);
    terminal_print(" bar0=");
    terminal_print_inline_hex64(bar0);
    terminal_print(" bar1=");
    terminal_print_inline_hex64(bar1);
    terminal_print(" cap=");
    terminal_print_inline_hex64(cap);
    terminal_flush_log();

    if (vendor == QCOM_FC7800_VENDOR && device == expected_device)
    {
        terminal_print("[K:QWIFI] MATCH ");
        terminal_print(label);
        terminal_print(" Qualcomm ");
        terminal_print_inline_hex64(vendor);
        terminal_print(":");
        terminal_print_inline_hex64(device);
        terminal_flush_log();
        if (expected_device == QCOM_FC7800_DEVICE)
            qwifi_dump_pci_caps(cfg, cap);
        if (expected_device == QCOM_FC7800_DEVICE)
            qwifi_enable_and_dump_bar0(bi, cfg, cmd, bar0, bar1);
        return 1;
    }

    return 0;
}

static uint32_t probe_hardcoded_qcom_fc7800_wifi(const boot_info *bi)
{
    dihos_pci_ecam ecam = {0};
    uint32_t hits = 0u;

    terminal_print("[K:QWIFI] Windows hint: FastConnect 7800 PCI 17CB:1107 at PCI4.RP1_.WLN_ seg4 bus1 dev0 fn0");
    terminal_flush_log();

    if (!bi || !bi->acpi_rsdp)
    {
        terminal_print("[K:QWIFI] no boot info / no RSDP");
        return 0u;
    }

    qwifi_acpi_prekick(bi);

    if (!qwifi_find_ecam(bi->acpi_rsdp, &ecam))
        return 0u;

    (void)qwifi_probe_one_function(bi, &ecam, "parent-RP1", 0u, 0u, 0u, QCOM_FC7800_ROOTPORT_DEVICE);
    hits += qwifi_probe_one_function(bi, &ecam, "wifi-WLN", QCOM_FC7800_BUS, QCOM_FC7800_DEV, QCOM_FC7800_FN, QCOM_FC7800_DEVICE);

    terminal_print("[K:QWIFI] hardcoded hit count=");
    terminal_print_inline_hex64(hits);
    terminal_flush_log();
    return hits;
}

static int sdhci_reset_cmd_dat(uint64_t base)
{
    const uint8_t SDHCI_RESET_CMD = 0x02u;
    const uint8_t SDHCI_RESET_DAT = 0x04u;
    const uint8_t mask = (uint8_t)(SDHCI_RESET_CMD | SDHCI_RESET_DAT);

    if (!base)
        return -1;

    mmio_write8(base + 0x2Fu, mask);
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if ((mmio_read8(base + 0x2Fu) & mask) == 0u)
            return 0;
    }

    return -2;
}

static void sdhci_enable_status_events(uint64_t base)
{
    const uint32_t SDHCI_STATUS_EN_CMD_COMPLETE = 1u << 0;
    const uint32_t SDHCI_STATUS_EN_ERROR_SUMMARY = 1u << 15;
    const uint32_t SDHCI_ERROR_STATUS_EN_ALL = 0xFFFF0000u;

    if (!base)
        return;

    /*
      SDHCI has a status-enable register separate from the signal-enable
      register. Keep IRQ signalling off, but allow command/error status bits
      to latch so the polling path can see completion.
    */
    mmio_write32(base + 0x34u,
                 SDHCI_ERROR_STATUS_EN_ALL |
                     SDHCI_STATUS_EN_ERROR_SUMMARY |
                     SDHCI_STATUS_EN_CMD_COMPLETE);
    mmio_write32(base + 0x38u, 0u);
}

static uint16_t sdhci_clock_for_400khz(uint32_t base_mhz, uint16_t version)
{
    const uint16_t SDHCI_CLOCK_INT_EN = 0x0001u;
    uint64_t base_hz = (uint64_t)base_mhz * 1000000ull;
    uint32_t spec = (uint32_t)(version & 0xFFu);
    uint32_t divisor = 0;

    if (base_hz == 0u)
        base_hz = 200000000ull;

    if (spec >= 3u)
    {
        divisor = (uint32_t)((base_hz + 799999ull) / 800000ull);
        if (divisor < 1u)
            divisor = 1u;
        if (divisor > 0x3FFu)
            divisor = 0x3FFu;
    }
    else if (base_hz > 400000ull)
    {
        uint32_t actual_div = 2u;
        divisor = 1u;
        while ((base_hz / actual_div) > 400000ull && divisor < 0x80u)
        {
            divisor <<= 1;
            actual_div <<= 1;
        }
    }

    return (uint16_t)(((divisor & 0xFFu) << 8) |
                      ((divisor & 0x300u) >> 2) |
                      SDHCI_CLOCK_INT_EN);
}

static int sdhci_power_clock_init(uint64_t base, uint32_t caps0, uint16_t version)
{
    const uint8_t SDHCI_RESET_ALL = 0x01u;
    const uint8_t SDHCI_POWER_ON = 0x01u;
    const uint8_t SDHCI_POWER_180 = 0x0Au;
    const uint8_t SDHCI_POWER_300 = 0x0Cu;
    const uint8_t SDHCI_POWER_330 = 0x0Eu;
    const uint16_t SDHCI_CLOCK_INT_STABLE = 0x0002u;
    const uint16_t SDHCI_CLOCK_CARD_EN = 0x0004u;
    uint32_t base_mhz;
    uint16_t clock;
    uint8_t power_sel;

    if (!base)
        return -1;

    mmio_write8(base + 0x2Fu, SDHCI_RESET_ALL);
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if ((mmio_read8(base + 0x2Fu) & SDHCI_RESET_ALL) == 0u)
            break;
        if (i + 1u == 1000000u)
            return -2;
    }

    mmio_write8(base + 0x2Eu, 0x0Eu);
    sdhci_enable_status_events(base);

    if (caps0 & 0x04000000u)
        power_sel = SDHCI_POWER_180;
    else if (caps0 & 0x02000000u)
        power_sel = SDHCI_POWER_300;
    else
        power_sel = SDHCI_POWER_330;
    mmio_write8(base + 0x29u, (uint8_t)(power_sel | SDHCI_POWER_ON));

    base_mhz = (caps0 >> 8) & 0xFFu;
    if (base_mhz == 0u)
        base_mhz = 200u;

    clock = sdhci_clock_for_400khz(base_mhz, version);
    terminal_print("[K:SDIO] clock ctl=");
    terminal_print_inline_hex64(clock);
    terminal_print(" base_mhz=");
    terminal_print_inline_hex64(base_mhz);
    terminal_print(" spec=");
    terminal_print_inline_hex64((uint64_t)(version & 0xFFu));
    terminal_print(" int_en=");
    terminal_print_inline_hex64(mmio_read32(base + 0x34u));

    mmio_write16(base + 0x2Cu, clock);
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if (mmio_read16(base + 0x2Cu) & SDHCI_CLOCK_INT_STABLE)
            break;
        if (i + 1u == 1000000u)
            return -3;
    }

    mmio_write16(base + 0x2Cu, (uint16_t)(clock | SDHCI_CLOCK_CARD_EN));
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if (mmio_read16(base + 0x2Cu) & SDHCI_CLOCK_INT_STABLE)
            break;
        if (i + 1u == 1000000u)
            return -4;
    }
    return 0;
}

static int sdhci_send_cmd(uint64_t base, uint32_t idx, uint32_t arg, uint16_t flags, uint32_t *resp_out)
{
    const uint32_t SDHCI_PRESENT_CMD_INHIBIT = 1u << 0;
    const uint32_t SDHCI_INT_CMD_COMPLETE = 1u << 0;
    const uint32_t SDHCI_INT_ERROR = 1u << 15;
    const uint32_t timeout = 1000000u;
    uint32_t status = 0;

    if (!base)
        return -1;

    if (resp_out)
        *resp_out = 0u;

    sdhci_enable_status_events(base);

    for (uint32_t i = 0; i < timeout; ++i)
    {
        if ((mmio_read32(base + 0x24u) & SDHCI_PRESENT_CMD_INHIBIT) == 0u)
            break;
        if (i + 1u == timeout)
        {
            (void)sdhci_reset_cmd_dat(base);
            return -2;
        }
    }

    mmio_write32(base + 0x30u, 0xFFFFFFFFu);
    mmio_write32(base + 0x08u, arg);
    mmio_write16(base + 0x0Eu, (uint16_t)((idx << 8) | flags));

    for (uint32_t i = 0; i < timeout; ++i)
    {
        status = mmio_read32(base + 0x30u);
        if (status & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR))
            break;
        if (i + 1u == timeout)
        {
            terminal_print("[K:SDIO] CMD timeout idx=");
            terminal_print_inline_hex64(idx);
            terminal_print(" present=");
            terminal_print_inline_hex64(mmio_read32(base + 0x24u));
            terminal_print(" int=");
            terminal_print_inline_hex64(status);
            (void)sdhci_reset_cmd_dat(base);
            return -3;
        }
    }

    if (resp_out)
        *resp_out = mmio_read32(base + 0x10u);
    mmio_write32(base + 0x30u, status);

    if (status & SDHCI_INT_ERROR)
    {
        terminal_print("[K:SDIO] CMD int status=");
        terminal_print_inline_hex64(status);
        terminal_print(" idx=");
        terminal_print_inline_hex64(idx);
        (void)sdhci_reset_cmd_dat(base);
        return -4;
    }

    return 0;
}

static int sdhci_cmd0_go_idle(uint64_t base)
{
    return sdhci_send_cmd(base, 0u, 0u, 0u, 0);
}

static int sdhci_cmd5_probe(uint64_t base, uint32_t *resp_out)
{
    const uint16_t SDHCI_CMD_RESP_SHORT = 0x0002u;

    /* CMD5 arg 0 asks an SDIO card for its OCR. */
    return sdhci_send_cmd(base, 5u, 0u, SDHCI_CMD_RESP_SHORT, resp_out);
}

static int sdhci_cmd5_probe_arg(uint64_t base, uint32_t arg, uint32_t *resp_out)
{
    const uint16_t SDHCI_CMD_RESP_SHORT = 0x0002u;

    return sdhci_send_cmd(base, 5u, arg, SDHCI_CMD_RESP_SHORT, resp_out);
}

static int sdhci_cmd8_probe(uint64_t base, uint32_t *resp_out)
{
    const uint16_t SDHCI_CMD_RESP_SHORT = 0x0002u;
    const uint16_t SDHCI_CMD_CRC = 0x0008u;
    const uint16_t SDHCI_CMD_INDEX = 0x0010u;

    return sdhci_send_cmd(base, 8u, 0x000001AAu,
                          (uint16_t)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX),
                          resp_out);
}

static void sdhci_set_bus_power(uint64_t base, uint8_t power_sel, const char *tag)
{
    const uint8_t SDHCI_POWER_ON = 0x01u;

    if (!base)
        return;

    mmio_write8(base + 0x29u, 0u);
    short_delay();
    mmio_write8(base + 0x29u, (uint8_t)(power_sel | SDHCI_POWER_ON));
    short_delay();

    terminal_print("[K:SDIO] bus power ");
    terminal_print(tag ? tag : "?");
    terminal_print(" power=");
    terminal_print_inline_hex64(mmio_read8(base + 0x29u));
}

static int sdio_try_cmd5_arg(uint64_t base, const char *tag, uint32_t arg, uint32_t *resp_out)
{
    int rc;

    terminal_print("[K:SDIO] CMD5 ");
    terminal_print(tag ? tag : "arg");
    terminal_print(" arg=");
    terminal_print_inline_hex64(arg);

    (void)sdhci_cmd0_go_idle(base);
    rc = sdhci_cmd5_probe_arg(base, arg, resp_out);
    terminal_print("[K:SDIO] CMD5 ");
    terminal_print(tag ? tag : "arg");
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    if (resp_out)
    {
        terminal_print(" resp=");
        terminal_print_inline_hex64(*resp_out);
    }

    return rc;
}

static int sdio_voltage_ocr_retries(uint64_t base, uint32_t caps0, uint8_t original_power, uint32_t *resp_out)
{
    const uint8_t SDHCI_POWER_180 = 0x0Au;
    const uint8_t SDHCI_POWER_300 = 0x0Cu;
    const uint8_t SDHCI_POWER_330 = 0x0Eu;
    const uint32_t SDIO_OCR_27_36V = 0x00FF8000u;
    uint8_t original_sel = (uint8_t)(original_power & 0x0Eu);
    int best_rc;

    best_rc = sdio_try_cmd5_arg(base, "ocr-current", SDIO_OCR_27_36V, resp_out);
    if (best_rc == 0)
        return 0;

    if ((caps0 & 0x02000000u) && original_sel != SDHCI_POWER_300)
    {
        sdhci_set_bus_power(base, SDHCI_POWER_300, "3.0V");
        best_rc = sdio_try_cmd5_arg(base, "3.0V-arg0", 0u, resp_out);
        if (best_rc == 0)
            return 0;
        best_rc = sdio_try_cmd5_arg(base, "3.0V-ocr", SDIO_OCR_27_36V, resp_out);
        if (best_rc == 0)
            return 0;
    }

    if ((caps0 & 0x01000000u) && original_sel != SDHCI_POWER_330)
    {
        sdhci_set_bus_power(base, SDHCI_POWER_330, "3.3V");
        best_rc = sdio_try_cmd5_arg(base, "3.3V-arg0", 0u, resp_out);
        if (best_rc == 0)
            return 0;
        best_rc = sdio_try_cmd5_arg(base, "3.3V-ocr", SDIO_OCR_27_36V, resp_out);
        if (best_rc == 0)
            return 0;
    }

    if ((caps0 & 0x04000000u) && original_sel != SDHCI_POWER_180)
    {
        sdhci_set_bus_power(base, SDHCI_POWER_180, "1.8V");
        best_rc = sdio_try_cmd5_arg(base, "1.8V-arg0", 0u, resp_out);
        if (best_rc == 0)
            return 0;
    }

    if (original_sel)
        sdhci_set_bus_power(base, original_sel, "restore");

    return best_rc;
}

static void sdio_probe_non_sdio_card(uint64_t base)
{
    uint32_t resp = 0;
    int rc;

    terminal_print("[K:SDIO] CMD8 memory-card probe");
    (void)sdhci_cmd0_go_idle(base);
    rc = sdhci_cmd8_probe(base, &resp);
    terminal_print("[K:SDIO] CMD8 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" resp=");
    terminal_print_inline_hex64(resp);
    if (rc == 0)
        terminal_print("[K:SDIO] CMD8 answered; SDC2 looks like SD memory, not SDIO WiFi");
    else
        terminal_print("[K:SDIO] CMD8 no answer; SDC2 has no visible SD/SDIO card");
}

static int sdio_gpio_wake_and_cmd5(uint64_t base, uint32_t *resp_out)
{
    const acpi_net_gpio_hint *gpios = acpi_probe_net_gpios();
    uint32_t count = acpi_probe_net_gpio_count();
    int best_rc = -10;
    uint32_t tried = 0;

    if (!base || !gpios)
        return -1;

    terminal_print("[K:SDIO] GPIO hint count=");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        int is_sdc2 = str_eq(gpios[i].dev_name, "SDC2") || str_eq(gpios[i].hid_name, "QCOM2466");
        int is_gpio_io = (gpios[i].conn_type == 1u);
        int rc;

        if (!is_sdc2 || !is_gpio_io || gpios[i].pin == 0u || gpios[i].pin > 255u)
            continue;

        tried++;
        terminal_print("[K:SDIO] GPIO wake pin=");
        terminal_print_inline_hex64(gpios[i].pin);
        terminal_print(" flags=");
        terminal_print_inline_hex64(gpios[i].flags);
        terminal_print(" cfg=");
        terminal_print_inline_hex64(gpios[i].pin_config);

        rc = gpio_write(gpios[i].pin, GPIO_VALUE_HIGH);
        terminal_print("[K:SDIO] GPIO latch-high rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);

        rc = gpio_set_direction(gpios[i].pin, GPIO_DIR_OUTPUT);
        terminal_print("[K:SDIO] GPIO dir rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (rc != 0)
        {
            best_rc = rc;
            continue;
        }

        rc = gpio_write(gpios[i].pin, GPIO_VALUE_HIGH);
        terminal_print("[K:SDIO] GPIO high rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        short_delay();

        (void)sdhci_cmd0_go_idle(base);
        rc = sdhci_cmd5_probe(base, resp_out);
        terminal_print("[K:SDIO] CMD5 after GPIO rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (resp_out)
        {
            terminal_print(" resp=");
            terminal_print_inline_hex64(*resp_out);
        }
        best_rc = rc;
        if (rc == 0)
            return 0;

        rc = gpio_write(gpios[i].pin, GPIO_VALUE_LOW);
        terminal_print("[K:SDIO] GPIO pulse-low rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        short_delay();
        rc = gpio_write(gpios[i].pin, GPIO_VALUE_HIGH);
        terminal_print("[K:SDIO] GPIO pulse-high rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        short_delay();

        (void)sdhci_cmd0_go_idle(base);
        rc = sdhci_cmd5_probe(base, resp_out);
        terminal_print("[K:SDIO] CMD5 after GPIO pulse rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (resp_out)
        {
            terminal_print(" resp=");
            terminal_print_inline_hex64(*resp_out);
        }
        best_rc = rc;
        if (rc == 0)
            return 0;
    }

    if (tried == 0u)
        terminal_print("[K:SDIO] no usable SDC2 GpioIo hint");

    return best_rc;
}

static void sdio_bootstrap_identify(uint64_t base)
{
    uint32_t present;
    uint32_t caps0;
    uint32_t caps1;
    uint32_t int_status;
    uint16_t clock;
    uint16_t version;
    uint8_t hostctl;
    uint8_t power;

    if (!base)
        return;

    terminal_print("[K:SDIO] identify base=");
    terminal_print_inline_hex64(base);
    terminal_flush_log();

    present = mmio_read32(base + 0x24u);
    caps0 = mmio_read32(base + 0x40u);
    caps1 = mmio_read32(base + 0x44u);
    int_status = mmio_read32(base + 0x30u);
    hostctl = mmio_read8(base + 0x28u);
    power = mmio_read8(base + 0x29u);
    clock = mmio_read16(base + 0x2Cu);
    version = mmio_read16(base + 0xFEu);

    terminal_print("[K:SDIO] present=");
    terminal_print_inline_hex64(present);
    terminal_print(" caps0=");
    terminal_print_inline_hex64(caps0);
    terminal_print(" caps1=");
    terminal_print_inline_hex64(caps1);
    terminal_print(" version=");
    terminal_print_inline_hex64(version);
    terminal_print(" spec=");
    terminal_print_inline_hex64((uint64_t)(version & 0xFFu));
    terminal_print(" vendor=");
    terminal_print_inline_hex64((uint64_t)((version >> 8) & 0xFFu));
    terminal_print("[K:SDIO] hostctl=");
    terminal_print_inline_hex64(hostctl);
    terminal_print(" power=");
    terminal_print_inline_hex64(power);
    terminal_print(" clock=");
    terminal_print_inline_hex64(clock);
    terminal_print(" int=");
    terminal_print_inline_hex64(int_status);

    if (!(power & 0x01u) || ((clock & 0x0005u) != 0x0005u))
    {
        int init_rc;
        terminal_print("[K:SDIO] init power/clock");
        terminal_flush_log();
        init_rc = sdhci_power_clock_init(base, caps0, version);
        terminal_print("[K:SDIO] init rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)init_rc);
        power = mmio_read8(base + 0x29u);
        clock = mmio_read16(base + 0x2Cu);
        present = mmio_read32(base + 0x24u);
        terminal_print("[K:SDIO] post-init present=");
        terminal_print_inline_hex64(present);
        terminal_print(" power=");
        terminal_print_inline_hex64(power);
        terminal_print(" clock=");
        terminal_print_inline_hex64(clock);
    }

    if ((power & 0x01u) && ((clock & 0x0005u) == 0x0005u))
    {
        uint32_t resp = 0;
        int rc;
        terminal_print("[K:SDIO] CMD5 probe");
        (void)sdhci_cmd0_go_idle(base);
        rc = sdhci_cmd5_probe(base, &resp);
        terminal_print("[K:SDIO] CMD5 rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_print(" resp=");
        terminal_print_inline_hex64(resp);
        if (rc != 0)
        {
            int gpio_rc = sdio_gpio_wake_and_cmd5(base, &resp);
            if (gpio_rc != 0)
            {
                int vrc = sdio_voltage_ocr_retries(base, caps0, power, &resp);
                if (vrc != 0)
                    sdio_probe_non_sdio_card(base);
            }
        }
    }
    else
    {
        terminal_print("[K:SDIO] CMD5 skipped: power/clock still off");
    }
}

static uint32_t map_sdio_bootstrap_windows(const boot_info *bi)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();
    uint32_t mapped = 0u;

    for (uint32_t i = 0; i < nres; ++i)
    {
        int is_sdc2 = str_eq(res[i].dev_name, "SDC2") || str_eq(res[i].hid_name, "QCOM2466");
        int is_mmio = (res[i].kind == 3u || res[i].kind == 4u || res[i].kind == 5u);
        int rc;

        if (!is_sdc2 || !is_mmio || !res[i].min_addr || !res[i].span_len)
            continue;

        if (res[i].span_len > 0x00100000ull)
        {
            terminal_print("[K:PCI] SDIO map skip: window too large");
            terminal_print(" len=");
            terminal_print_inline_hex64(res[i].span_len);
            continue;
        }

        if (ecam_overlaps_efi_ram(bi, res[i].min_addr, res[i].span_len))
        {
            terminal_print("[K:PCI] SDIO map skip: overlaps EFI RAM");
            continue;
        }

        terminal_print("[K:PCI] SDIO bootstrap map dev=");
        terminal_print(res[i].dev_name);
        terminal_print(" hid=");
        terminal_print(res[i].hid_name);
        terminal_print(" base=");
        terminal_print_inline_hex64(res[i].min_addr);
        terminal_print(" len=");
        terminal_print_inline_hex64(res[i].span_len);

        rc = mmio_map_device_identity(res[i].min_addr, res[i].span_len);
        terminal_print("[K:PCI] SDIO bootstrap map rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (rc == 0)
        {
            mapped++;
            sdio_bootstrap_identify(res[i].min_addr);
        }
    }

    return mapped;
}

static void probe_pci5_mmio_windows(const boot_info *bi)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();

    for (uint32_t i = 0; i < nres; ++i)
    {
        uint64_t probe_base;
        uint64_t probe_len;
        int rc;

        if (!str_eq(res[i].dev_name, "PCI5"))
            continue;
        if (!(res[i].kind == 1u && res[i].rtype == 0u))
            continue;
        if (!res[i].min_addr || !res[i].span_len)
            continue;

        probe_base = res[i].min_addr;
        probe_len = res[i].span_len;
        if (probe_len > 0x00100000ull)
            probe_len = 0x00100000ull;

        if (ecam_overlaps_efi_ram(bi, probe_base, probe_len))
        {
            terminal_print("[K:PCI] PCI5 MMIO probe skip: overlaps EFI RAM");
            continue;
        }

        terminal_print("[K:PCI] PCI5 MMIO probe map base=");
        terminal_print_inline_hex64(probe_base);
        terminal_print(" len=");
        terminal_print_inline_hex64(probe_len);
        terminal_flush_log();
        rc = mmio_map_device_identity(probe_base, probe_len);
        terminal_print("[K:PCI] PCI5 MMIO probe map rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();
        if (rc != 0)
            continue;
        terminal_print("[K:PCI] PCI5 MMIO probe: mapped (read disabled; aperture aborts unrecoverably)");
        terminal_flush_log();
    }
}

void pci_kernel_probe_nics_from_mcfg(const boot_info *bi)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint16_t preferred_seg = 0;
    int have_preferred_seg = 0;
    int has_wlan_hint = ((g_net_hints & (DIHOS_NET_HINT_WLAN | DIHOS_NET_HINT_WIFI | DIHOS_NET_HINT_WCN)) != 0u);
    int has_wwan_hint = ((g_net_hints & (DIHOS_NET_HINT_WWAN | DIHOS_NET_HINT_MHI)) != 0u);
    uint32_t sdc2_maps = 0u;
    uint32_t nic_hits = 0u;

    terminal_print("[K:PCI] aa64 ECAM config reads disabled");
    terminal_print("[K:PCI] reason: unreadable ECAM can raise a fatal external abort");

    have_preferred_seg = choose_preferred_segment_from_net_resources(&preferred_seg);
    if (have_preferred_seg)
    {
        terminal_print("[K:PCI] ACPI points network at PCI segment: ");
        terminal_print_inline_hex64(preferred_seg);
    }

    print_acpi_net_resource_windows();
    nic_hits += probe_hardcoded_qcom_fc7800_wifi(bi);
    if (nic_hits != 0u)
    {
        terminal_print("[K:PCI] hardcoded Qualcomm WiFi path found; skipping PCI5/SDIO fallback");
        terminal_flush_log();
    }

    if (nic_hits == 0u && has_wwan_hint && bi && bi->acpi_rsdp)
    {
        uint64_t sta_ret = 0;
        uint64_t ret = 0;
        acpi_probe_net_exec_context_reset();
        {
            uint64_t reg_args[2];
            reg_args[0] = 0x08u;
            reg_args[1] = 1u;
            terminal_print("[K:PCI] ACPI pre-kick GIO0._REG(0x08,1)");
            (void)acpi_probe_net_exec_device_method_args(bi->acpi_rsdp, "GIO0", "_REG", 2u, reg_args, 0);
        }

        terminal_print("[K:PCI] ACPI pre-kick SDC2._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "SDC2", "_STA", &ret);
        terminal_print("[K:PCI] SDC2._STA ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI pre-kick SDC2._PS0");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "SDC2", "_PS0", 0);
        terminal_print("[K:PCI] ACPI pre-kick SDC2._INI");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "SDC2", "_INI", 0);

        terminal_print("[K:PCI] ACPI pre-kick QPPX._RST");
        for (uint64_t rst_arg = 0u; rst_arg <= 3u; ++rst_arg)
        {
            uint64_t args[1];
            args[0] = rst_arg;
            terminal_print("[K:PCI] QPPX._RST arg=");
            terminal_print_inline_hex64(rst_arg);
            (void)acpi_probe_net_exec_device_method_args(bi->acpi_rsdp, "QPPX", "_RST", 1u, args, 0);
            short_delay();
        }
        terminal_print("[K:PCI] ACPI pre-kick QPPX._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "QPPX", "_STA", &sta_ret);
        terminal_print("[K:PCI] QPPX._STA ret=");
        terminal_print_inline_hex64(sta_ret);

        terminal_print("[K:PCI] ACPI pre-kick PCI5._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_STA", &ret);
        terminal_print("[K:PCI] PCI5._STA ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI pre-kick PCI5._PS0");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_PS0", 0);
        terminal_print("[K:PCI] ACPI probe PCI5._PSC");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_PSC", &ret);
        terminal_print("[K:PCI] PCI5._PSC ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI probe PCI5.PVD5");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "PVD5", &ret);
        terminal_print("[K:PCI] PCI5.PVD5 ret=");
        terminal_print_inline_hex64(ret);
        terminal_flush_log();
        terminal_print("[K:PCI] ACPI pre-kick PCI5._INI");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_INI", 0);

        terminal_print("[K:PCI] ACPI pre-kick WWAN._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_STA", &ret);
        terminal_print("[K:PCI] WWAN._STA ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI pre-kick WWAN._PS0");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_PS0", 0);
        terminal_print("[K:PCI] ACPI pre-kick WWAN._INI");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_INI", 0);

        for (uint32_t settle = 0; settle < 8u; ++settle)
        {
            terminal_print("[K:PCI] settle poll iter=");
            terminal_print_inline_hex64(settle);

            (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "QPPX", "_STA", &ret);
            terminal_print("[K:PCI] settle QPPX._STA=");
            terminal_print_inline_hex64(ret);

            (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_STA", &ret);
            terminal_print("[K:PCI] settle PCI5._STA=");
            terminal_print_inline_hex64(ret);

            (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_STA", &ret);
            terminal_print("[K:PCI] settle WWAN._STA=");
            terminal_print_inline_hex64(ret);

            short_delay();
            short_delay();
        }

        probe_pci5_mmio_windows(bi);
    }
    if (nic_hits != 0u)
    {
        terminal_print("[K:PCI] SDC2 SDIO bootstrap skipped: real WiFi is PCIe 17CB:1107");
    }
    else if (!has_wlan_hint && has_wwan_hint)
    {
        terminal_print("[K:PCI] defer SDC2 SDIO bootstrap: ACPI points to WWAN/MHI path and has no WLAN marker");
    }
    else
    {
        sdc2_maps = map_sdio_bootstrap_windows(bi);
    }

    if (nic_hits == 0u && sdc2_maps == 0u)
    {
        terminal_print("[K:PCI] fallback: run safe SDC2 SDIO bootstrap (no NIC path found)");
        sdc2_maps = map_sdio_bootstrap_windows(bi);
    }

    terminal_print("[K:PCI] SDC2 map attempts: ");
    terminal_print_inline_hex64(sdc2_maps);
    terminal_print("[K:PCI] NIC hits total: ");
    terminal_print_inline_hex64(nic_hits);
    terminal_flush_log();
    return;
#else
    const int probe_level = 3; /* 0=plan only, 1=map only, 2=tiny read, 3=bounded full scan */
    const uint32_t max_map_attempts = 2u;
    const uint32_t max_probe_buses_per_segment = 2u;
    const uint32_t max_visible_logs = 24u;
    uint32_t map_attempts = 0;
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t ecam_count;
    uint32_t nic_hits = 0;
    uint32_t visible_fns = 0;
    uint32_t visible_logs = 0;
    uint16_t preferred_seg = 0;
    int have_preferred_seg = 0;

    if (!bi || !bi->acpi_rsdp)
    {
        terminal_print("[K:PCI] no boot info / no RSDP");
        return;
    }

    ecam_count = acpi_pci_get_ecams_from_rsdp(bi->acpi_rsdp, ecams, DIHOS_PCI_ECAM_MAX);
    terminal_print("[K:PCI] kernel MCFG NIC probe");
    terminal_print("[K:PCI] ECAM count: ");
    terminal_print_inline_hex64(ecam_count);

    have_preferred_seg = choose_preferred_segment_from_net_resources(&preferred_seg);
    if (have_preferred_seg)
    {
        terminal_print("[K:PCI] preferred segment from ACPI net resources: ");
        terminal_print_inline_hex64(preferred_seg);
    }

    nic_hits += probe_hardcoded_qcom_fc7800_wifi(bi);
    if (nic_hits != 0u)
    {
        terminal_print("[K:PCI] hardcoded Qualcomm WiFi path found; skipping broad PCI scan");
        terminal_print("[K:PCI] NIC hits total: ");
        terminal_print_inline_hex64(nic_hits);
        return;
    }

    for (uint32_t i = 0; i < ecam_count; ++i)
    {
        uint64_t total_buses = 0;
        uint64_t probe_buses = 0;
        uint64_t map_base = ecams[i].base;
        uint64_t map_size;
        int prefer_this_segment = 0;
        int map_rc;

        if (ecams[i].end_bus < ecams[i].start_bus)
            continue;

        total_buses = (uint64_t)ecams[i].end_bus - (uint64_t)ecams[i].start_bus + 1ull;
        probe_buses = total_buses;
        if (probe_buses > max_probe_buses_per_segment)
            probe_buses = max_probe_buses_per_segment;
        if (probe_buses == 0u)
            continue;
        map_size = probe_buses << 20; /* 1 MiB per bus */
        prefer_this_segment = (have_preferred_seg && ecams[i].segment == preferred_seg);

        terminal_print("[K:PCI] seg=");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" buses=");
        terminal_print_inline_hex64(ecams[i].start_bus);
        terminal_print("-");
        terminal_print_inline_hex64((uint64_t)(ecams[i].start_bus + (uint8_t)(probe_buses - 1u)));
        terminal_print(" base=");
        terminal_print_inline_hex64(map_base);
        terminal_print(" map_size=");
        terminal_print_inline_hex64(map_size);
        if (prefer_this_segment)
            terminal_print(" [preferred]");

        if (ecam_overlaps_efi_ram(bi, map_base, map_size))
        {
            terminal_print("[K:PCI] skip: overlaps EFI RAM");
            continue;
        }

        if (map_base < 0x0000000100000000ull)
        {
            if (!prefer_this_segment)
            {
                terminal_print("[K:PCI] skip: sub-4G segment (not preferred)");
                continue;
            }
            terminal_print("[K:PCI] allow: sub-4G preferred segment");
        }

        if (map_attempts >= max_map_attempts && !prefer_this_segment)
        {
            terminal_print("[K:PCI] skip: staged map attempt limit reached");
            continue;
        }

        if (probe_level <= 0)
        {
            terminal_print("[K:PCI] plan-only mode: mapping/read disabled");
            continue;
        }

        map_rc = mmio_map_device_identity(map_base, map_size);
        if (map_rc != 0)
        {
            terminal_print("[K:PCI] map failed rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)map_rc);
            continue;
        }
        map_attempts++;

        if (probe_level == 1)
        {
            terminal_print("[K:PCI] map-only mode: scan disabled");
            continue;
        }

        if (probe_level == 2)
        {
            for (uint32_t b = 0; b < probe_buses; ++b)
            {
                uint8_t bus = (uint8_t)(ecams[i].start_bus + (uint8_t)b);
                for (uint8_t dev = 0; dev < 8u; ++dev)
                {
                    uint64_t cfg = pci_ecam_config_phys(&ecams[i], bus, dev, 0, 0);
                    uint32_t id = cfg ? mmio_read32(cfg + 0x00) : 0xFFFFFFFFu;
                    terminal_print("[K:PCI] tiny-read bdf=");
                    terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8));
                    terminal_print(" id=");
                    terminal_print_inline_hex64(id);
                }
            }
            continue;
        }

        for (uint32_t b = 0; b < probe_buses; ++b)
        {
            uint8_t bus = (uint8_t)(ecams[i].start_bus + (uint8_t)b);
            for (uint8_t dev = 0; dev < 32u; ++dev)
            {
                for (uint8_t fn = 0; fn < 8u; ++fn)
                {
                    uint64_t cfg = pci_ecam_config_phys(&ecams[i], bus, dev, fn, 0);
                    uint32_t id;
                    uint32_t cc;
                    uint8_t class_code;
                    uint8_t subclass;
                    uint8_t prog_if;

                    if (!cfg)
                        continue;

                    id = mmio_read32(cfg + 0x00);
                    if (id == 0xFFFFFFFFu || id == 0x00000000u)
                    {
                        if (fn == 0)
                            break;
                        continue;
                    }

                    cc = mmio_read32(cfg + 0x08);
                    class_code = (uint8_t)(cc >> 24);
                    subclass = (uint8_t)(cc >> 16);
                    prog_if = (uint8_t)(cc >> 8);
                    visible_fns++;

                    if (visible_logs < max_visible_logs)
                    {
                        terminal_print("[K:PCI] fn seg=");
                        terminal_print_inline_hex64(ecams[i].segment);
                        terminal_print(" bdf=");
                        terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8) | (uint64_t)fn);
                        terminal_print(" vid=");
                        terminal_print_inline_hex64((uint16_t)(id & 0xFFFFu));
                        terminal_print(" did=");
                        terminal_print_inline_hex64((uint16_t)((id >> 16) & 0xFFFFu));
                        terminal_print(" cls=");
                        terminal_print_inline_hex64(class_code);
                        terminal_print(" sub=");
                        terminal_print_inline_hex64(subclass);
                        terminal_print(" if=");
                        terminal_print_inline_hex64(prog_if);
                        visible_logs++;
                    }

                    if (is_likely_network_class(class_code))
                    {
                        nic_hits++;
                        terminal_print("[K:PCI] NET seg=");
                        terminal_print_inline_hex64(ecams[i].segment);
                        terminal_print(" bdf=");
                        terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8) | (uint64_t)fn);
                        terminal_print(" vid=");
                        terminal_print_inline_hex64((uint16_t)(id & 0xFFFFu));
                        terminal_print(" did=");
                        terminal_print_inline_hex64((uint16_t)((id >> 16) & 0xFFFFu));
                        terminal_print(" cls=");
                        terminal_print_inline_hex64(class_code);
                        terminal_print(" sub=");
                        terminal_print_inline_hex64(subclass);
                        terminal_print(" if=");
                        terminal_print_inline_hex64(prog_if);
                    }
                }
            }
        }
    }

    terminal_print("[K:PCI] visible function count: ");
    terminal_print_inline_hex64(visible_fns);
    terminal_print("[K:PCI] NIC hits total: ");
    terminal_print_inline_hex64(nic_hits);
#endif
}
