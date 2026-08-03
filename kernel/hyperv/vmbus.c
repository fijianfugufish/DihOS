#include "hyperv/vmbus.h"
#include "hyperv/hyperv.h"
#include "asm/asm.h"
#include "memory/pmem.h"
#include "terminal/terminal_api.h"

enum
{
    HV_CALL_POST_MESSAGE = 0x005Cu,
    HV_CALL_SIGNAL_EVENT = 0x005Du,
    HV_MESSAGE_TYPE_CHANNEL = 1u,
    HV_REGISTER_SINT2 = 0x000A0002u,
    HV_REGISTER_SCONTROL = 0x000A0010u,
    HV_REGISTER_SIEFP = 0x000A0012u,
    HV_REGISTER_SIMP = 0x000A0013u,
    HV_REGISTER_EOM = 0x000A0014u,
    VMBUS_CONNECTION_ID_4 = 4u,
    VMBUS_MESSAGE_SINT = 2u,
    VMBUS_CHANNEL_OFFER = 1u,
    VMBUS_CHANNEL_REQUEST_OFFERS = 3u,
    VMBUS_CHANNEL_ALL_OFFERS_DELIVERED = 4u,
    VMBUS_CHANNEL_OPEN = 5u,
    VMBUS_CHANNEL_OPEN_RESULT = 6u,
    VMBUS_CHANNEL_GPADL_HEADER = 8u,
    VMBUS_CHANNEL_GPADL_CREATED = 10u,
    VMBUS_CHANNEL_INITIATE_CONTACT = 14u,
    VMBUS_CHANNEL_VERSION_RESPONSE = 15u,
    VMBUS_VERSION_6_0 = 0x00060000u,
    VMBUS_PACKET_DATA_INBAND = 6u,
    VMBUS_PACKET_DATA_USING_GPA_DIRECT = 9u,
    VMBUS_PACKET_COMPLETION_REQUESTED = 1u,
    VSTOR_OPERATION_COMPLETE_IO = 1u,
    VSTOR_OPERATION_EXECUTE_SRB = 3u,
    VSTOR_OPERATION_BEGIN_INITIALIZATION = 7u,
    VSTOR_OPERATION_END_INITIALIZATION = 8u,
    VSTOR_OPERATION_QUERY_PROTOCOL_VERSION = 9u,
    VSTOR_OPERATION_QUERY_PROPERTIES = 10u,
    SCSI_OPERATION_TEST_UNIT_READY = 0u,
    SCSI_OPERATION_INQUIRY = 0x12u,
    SCSI_OPERATION_READ_CAPACITY_10 = 0x25u,
    SCSI_OPERATION_READ_10 = 0x28u,
    SCSI_OPERATION_WRITE_10 = 0x2Au,
    SRB_STATUS_SUCCESS = 1u,
    SRB_FLAGS_DISABLE_SYNCH_TRANSFER = 0x00000008u,
    SRB_FLAGS_DATA_IN = 0x00000040u,
    SRB_FLAGS_DATA_OUT = 0x00000080u,
};

#define HV_HYPERCALL_FAST_BIT (1ull << 16)
#define HV_SYNIC_PAGE_ENABLE 1ull
#define HV_SYNIC_CONTROL_ENABLE 1ull
#define VMBUS_POLL_ROUNDS 10000000u
#define VMBUS_RING_PAGES 8u
#define VMBUS_RING_DOWNSTREAM_OFFSET 4u
#define VMBUS_GPADL_HANDLE 0x000E1E10u
#define VMBUS_KBD_GPADL_HANDLE 0x000E1E11u
#define VMBUS_MOUSE_GPADL_HANDLE 0x000E1E12u
#define VMBUS_OPEN_ID 1u
#define VMBUS_KBD_OPEN_ID 2u
#define VMBUS_MOUSE_OPEN_ID 3u
#define VMBUS_RING_DATA_SIZE ((VMBUS_RING_DOWNSTREAM_OFFSET - 1u) * 4096u)
#define VMBUS_RQST_INIT (~0ull - 1ull)

static uint8_t *G_message_page;
static uint8_t *G_event_page;
static uint8_t *G_monitor_page1;
static uint8_t *G_monitor_page2;
static uint32_t G_vmbus_version;
static uint32_t G_vmbus_interrupt;
static uint32_t G_vmbus_message_connection = VMBUS_CONNECTION_ID_4;
static uint32_t G_storvsc_relid;
static uint32_t G_storvsc_connection_id;
static uint8_t *G_storvsc_ring;
static uint32_t G_storvsc_gpadl;
static uint32_t G_kbd_relid;
static uint32_t G_kbd_connection_id;
static uint8_t *G_kbd_ring;
static uint32_t G_kbd_gpadl;
static uint32_t G_mouse_relid;
static uint32_t G_mouse_connection_id;
static uint8_t *G_mouse_ring;
static uint32_t G_mouse_gpadl;

static const uint8_t G_storvsc_guid[16] = {
    0xD9, 0x63, 0x61, 0xBA, 0xA1, 0x04, 0x29, 0x4D,
    0xB6, 0x05, 0x72, 0xE2, 0xFF, 0xB1, 0xDC, 0x7F};

static const uint8_t G_kbd_guid[16] = {
    0x6D, 0xAD, 0x12, 0xF9, 0x17, 0x2B, 0xEA, 0x48,
    0xBD, 0x65, 0xF9, 0x27, 0xA6, 0x1C, 0x76, 0x84};

static const uint8_t G_mouse_guid[16] = {
    0x9E, 0xB6, 0xA8, 0xCF, 0x4A, 0x5B, 0xC0, 0x4C,
    0xB9, 0x8B, 0x8B, 0xA1, 0xA1, 0xF3, 0xF9, 0x5A};

static void vmbus_zero_page(uint8_t *page)
{
    for (uint32_t i = 0; i < 4096u; ++i)
        page[i] = 0;
}

static void vmbus_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void vmbus_put_u64(uint8_t *p, uint64_t v)
{
    vmbus_put_u32(p, (uint32_t)v);
    vmbus_put_u32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t vmbus_get_u32(const volatile uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t vmbus_get_u64(const volatile uint8_t *p)
{
    return (uint64_t)vmbus_get_u32(p) |
           ((uint64_t)vmbus_get_u32(p + 4) << 32);
}

static uint32_t vmbus_get_be32(const volatile uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void vmbus_put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void vmbus_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void vmbus_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t vmbus_get_u16(const volatile uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t vmbus_ring_copy_to(uint8_t *ring, uint32_t offset,
                                   const uint8_t *source, uint32_t count)
{
    uint8_t *data = ring + 4096u;

    for (uint32_t i = 0; i < count; ++i)
    {
        data[offset] = source[i];
        offset = (offset + 1u) % VMBUS_RING_DATA_SIZE;
    }
    return offset;
}

static void vmbus_ring_copy_from(const uint8_t *ring, uint32_t offset,
                                 uint8_t *destination, uint32_t count)
{
    const uint8_t *data = ring + 4096u;

    for (uint32_t i = 0; i < count; ++i)
    {
        destination[i] = data[offset];
        offset = (offset + 1u) % VMBUS_RING_DATA_SIZE;
    }
}

static int vmbus_bytes_equal(const volatile uint8_t *p,
                             const char *text,
                             uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (p[i] != (uint8_t)text[i])
            return 0;
    }
    return 1;
}

static int vmbus_guid_equal(const volatile uint8_t *p, const uint8_t guid[16])
{
    for (uint32_t i = 0; i < 16u; ++i)
    {
        if (p[i] != guid[i])
            return 0;
    }
    return 1;
}

static const volatile uint8_t *vmbus_find_fadt(uint64_t rsdp_phys)
{
    const volatile uint8_t *rsdp =
        (const volatile uint8_t *)(uintptr_t)rsdp_phys;
    const volatile uint8_t *root;
    uint64_t root_phys;
    uint32_t entry_size;
    uint32_t root_len;

    if (rsdp_phys < 0x1000ull ||
        !vmbus_bytes_equal(rsdp, "RSD PTR ", 8))
        return 0;

    if (rsdp[15] >= 2u && vmbus_get_u64(rsdp + 24) != 0u)
    {
        root_phys = vmbus_get_u64(rsdp + 24);
        entry_size = 8u;
    }
    else
    {
        root_phys = vmbus_get_u32(rsdp + 16);
        entry_size = 4u;
    }
    root = (const volatile uint8_t *)(uintptr_t)root_phys;
    root_len = vmbus_get_u32(root + 4);
    if (root_len < 36u || root_len > 1024u * 1024u)
        return 0;

    for (uint32_t off = 36u; off + entry_size <= root_len; off += entry_size)
    {
        uint64_t phys = entry_size == 8u
                            ? vmbus_get_u64(root + off)
                            : vmbus_get_u32(root + off);
        const volatile uint8_t *table =
            (const volatile uint8_t *)(uintptr_t)phys;
        if (phys >= 0x1000ull &&
            vmbus_bytes_equal(table, "FACP", 4))
            return table;
    }
    return 0;
}

static uint32_t vmbus_acpi_interrupt(uint64_t rsdp_phys)
{
    const volatile uint8_t *fadt = vmbus_find_fadt(rsdp_phys);
    const volatile uint8_t *dsdt;
    uint64_t dsdt_phys;
    uint32_t dsdt_len;

    if (!fadt)
        return 0u;
    if (vmbus_get_u32(fadt + 4) >= 148u &&
        vmbus_get_u64(fadt + 140) != 0u)
        dsdt_phys = vmbus_get_u64(fadt + 140);
    else
        dsdt_phys = vmbus_get_u32(fadt + 40);

    if (dsdt_phys < 0x1000ull)
        return 0u;
    dsdt = (const volatile uint8_t *)(uintptr_t)dsdt_phys;
    dsdt_len = vmbus_get_u32(dsdt + 4);
    if (!vmbus_bytes_equal(dsdt, "DSDT", 4) ||
        dsdt_len < 36u || dsdt_len > 16u * 1024u * 1024u)
        return 0u;

    for (uint32_t hid = 36u; hid + 5u < dsdt_len; ++hid)
    {
        uint32_t scan_end;

        if (!vmbus_bytes_equal(dsdt + hid, "VMBUS", 5))
            continue;
        scan_end = hid + 4096u;
        if (scan_end > dsdt_len)
            scan_end = dsdt_len;

        /*
         * ACPI large resource item 0x89 is Extended Interrupt:
         * tag, length[2], flags, interrupt_count, then u32 INTIDs.
         */
        for (uint32_t p = hid; p + 9u <= scan_end; ++p)
        {
            uint16_t length;
            uint8_t count;

            if (dsdt[p] != 0x89u)
                continue;
            length = (uint16_t)dsdt[p + 1] |
                     ((uint16_t)dsdt[p + 2] << 8);
            count = dsdt[p + 4];
            if (count == 0u || length < 6u ||
                p + 3u + length > dsdt_len)
                continue;
            return vmbus_get_u32(dsdt + p + 5);
        }
    }
    return 0u;
}

static int vmbus_alloc_pages(void)
{
    if (G_message_page)
        return 0;

    G_message_page = (uint8_t *)pmem_alloc_pages(1);
    G_event_page = (uint8_t *)pmem_alloc_pages(1);
    G_monitor_page1 = (uint8_t *)pmem_alloc_pages(1);
    G_monitor_page2 = (uint8_t *)pmem_alloc_pages(1);
    if (!G_message_page || !G_event_page ||
        !G_monitor_page1 || !G_monitor_page2)
        return -1;

    vmbus_zero_page(G_message_page);
    vmbus_zero_page(G_event_page);
    vmbus_zero_page(G_monitor_page1);
    vmbus_zero_page(G_monitor_page2);
    asm_dma_clean_range(G_message_page, 4096u);
    asm_dma_clean_range(G_event_page, 4096u);
    asm_dma_clean_range(G_monitor_page1, 4096u);
    asm_dma_clean_range(G_monitor_page2, 4096u);
    return 0;
}

static int vmbus_enable_synic(uint32_t interrupt)
{
    uint64_t message_phys = pmem_virt_to_phys(G_message_page);
    uint64_t event_phys = pmem_virt_to_phys(G_event_page);

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    /*
     * Let Hyper-V deliver SINT2 into the message page, but keep the physical
     * CPU IRQ masked until DihOS has a real GIC interrupt path.
     */
    __asm__ __volatile__("msr daifset, #2" ::: "memory");
#endif

    if (hyperv_set_vpreg(HV_REGISTER_SIMP,
                         (message_phys & ~0xFFFull) |
                             HV_SYNIC_PAGE_ENABLE) != 0)
        return -1;
    if (hyperv_set_vpreg(HV_REGISTER_SIEFP,
                         (event_phys & ~0xFFFull) |
                             HV_SYNIC_PAGE_ENABLE) != 0)
        return -1;
    if (hyperv_set_vpreg(HV_REGISTER_SINT2,
                         (interrupt & 0xFFu)) != 0)
        return -1;
    if (hyperv_set_vpreg(HV_REGISTER_SCONTROL,
                         HV_SYNIC_CONTROL_ENABLE) != 0)
        return -1;

    terminal_print("vmbus: SynIC pages enabled; CPU IRQ masked for polling");
    return 0;
}

static uint64_t vmbus_post_contact(void)
{
    uint8_t *input = (uint8_t *)pmem_alloc_pages(1);
    uint8_t *payload;
    uint64_t input_phys;

    if (!input)
        return ~0ull;
    vmbus_zero_page(input);
    payload = input + 16;

    vmbus_put_u32(input + 0, VMBUS_CONNECTION_ID_4);
    vmbus_put_u32(input + 8, HV_MESSAGE_TYPE_CHANNEL);
    vmbus_put_u32(input + 12, 40u);

    vmbus_put_u32(payload + 0, VMBUS_CHANNEL_INITIATE_CONTACT);
    vmbus_put_u32(payload + 8, VMBUS_VERSION_6_0);
    vmbus_put_u32(payload + 12, 0u);
    payload[16] = VMBUS_MESSAGE_SINT;
    payload[17] = 0u;
    vmbus_put_u64(payload + 24, pmem_virt_to_phys(G_monitor_page1));
    vmbus_put_u64(payload + 32, pmem_virt_to_phys(G_monitor_page2));

    asm_dma_clean_range(input, 256u);
    input_phys = pmem_virt_to_phys(input);
    return hyperv_hypercall(HV_CALL_POST_MESSAGE, input_phys, 0u);
}

static uint64_t vmbus_post_channel_message(uint32_t connection_id,
                                           const uint8_t *payload,
                                           uint32_t payload_size)
{
    uint8_t *input = (uint8_t *)pmem_alloc_pages(1);

    if (!input || !payload || payload_size > 240u)
        return ~0ull;
    vmbus_zero_page(input);
    vmbus_put_u32(input + 0, connection_id);
    vmbus_put_u32(input + 8, HV_MESSAGE_TYPE_CHANNEL);
    vmbus_put_u32(input + 12, payload_size);
    for (uint32_t i = 0; i < payload_size; ++i)
        input[16u + i] = payload[i];
    asm_dma_clean_range(input, 256u);
    return hyperv_hypercall(HV_CALL_POST_MESSAGE,
                            pmem_virt_to_phys(input),
                            0u);
}

static void vmbus_release_message(volatile uint8_t *message)
{
    message[0] = 0u;
    message[1] = 0u;
    message[2] = 0u;
    message[3] = 0u;
    asm_dma_clean_range((const void *)message, 16u);
    (void)hyperv_set_vpreg(HV_REGISTER_EOM, 0u);
}

int vmbus_probe_contact(uint64_t acpi_rsdp)
{
    volatile uint8_t *message;
    uint64_t status;

    G_vmbus_version = 0u;
    if (!hyperv_core_ready())
        return -1;
    if (vmbus_alloc_pages() != 0)
    {
        terminal_error("vmbus: page allocation failed");
        return -1;
    }
    G_vmbus_interrupt = vmbus_acpi_interrupt(acpi_rsdp);
    if (G_vmbus_interrupt < 16u || G_vmbus_interrupt > 255u)
    {
        terminal_error("vmbus: ACPI VMBUS interrupt missing/invalid value=");
        terminal_print_inline_hex32(G_vmbus_interrupt);
        return -1;
    }
    terminal_print("vmbus: ACPI VMBUS interrupt=");
    terminal_print_inline_hex32(G_vmbus_interrupt);

    if (vmbus_enable_synic(G_vmbus_interrupt) != 0)
    {
        terminal_error("vmbus: SynIC setup failed");
        return -1;
    }

    terminal_print("vmbus: posting INITIATE_CONTACT version 6.0");
    status = vmbus_post_contact();
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: POST_MESSAGE failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }

    message = G_message_page + VMBUS_MESSAGE_SINT * 256u;
    for (uint32_t round = 0; round < VMBUS_POLL_ROUNDS; ++round)
    {
        if ((round & 0x3FFFu) == 0u)
        {
            /*
             * EOM asks Hyper-V to move a queued message into any empty
             * SynIC slot. This is the portable polling path when the
             * optional SINT polling bit is unavailable.
             */
            (void)hyperv_set_vpreg(HV_REGISTER_EOM, 0u);
            asm_dma_invalidate_range((const void *)message, 256u);
        }

        if (vmbus_get_u32(message) != 0u)
        {
            const volatile uint8_t *payload = message + 16;
            uint32_t channel_type = vmbus_get_u32(payload);

            terminal_print("vmbus: SynIC message type=");
            terminal_print_inline_hex32(vmbus_get_u32(message));
            terminal_print(" channel_type=");
            terminal_print_inline_hex32(channel_type);

            if (channel_type == VMBUS_CHANNEL_VERSION_RESPONSE)
            {
                uint8_t supported = payload[8];
                uint8_t state = payload[9];
                uint32_t connection_id = vmbus_get_u32(payload + 12);

                terminal_print("vmbus: VERSION_RESPONSE supported=");
                terminal_print_inline_hex32(supported);
                terminal_print(" state=");
                terminal_print_inline_hex32(state);
                terminal_print(" connection=");
                terminal_print_inline_hex32(connection_id);
                if (supported)
                {
                    G_vmbus_version = VMBUS_VERSION_6_0;
                    G_vmbus_message_connection =
                        connection_id ? connection_id : 1u;
                    vmbus_release_message(message);
                    return 0;
                }
            }
            return -1;
        }
        asm_relax();
    }

    terminal_error("vmbus: contact response timeout");
    return -1;
}

int vmbus_find_storvsc_offer(void)
{
    uint8_t request[8] = {0};
    volatile uint8_t *message;
    uint64_t status;
    uint32_t offer_count = 0u;

    if (!G_vmbus_version || !G_message_page)
        return -1;
    G_storvsc_relid = 0u;
    G_storvsc_connection_id = 0u;
    G_kbd_relid = 0u;
    G_kbd_connection_id = 0u;
    G_mouse_relid = 0u;
    G_mouse_connection_id = 0u;

    vmbus_put_u32(request, VMBUS_CHANNEL_REQUEST_OFFERS);
    terminal_print("vmbus: requesting channel offers connection=");
    terminal_print_inline_hex32(G_vmbus_message_connection);
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        request,
                                        sizeof(request));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: REQUESTOFFERS failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }

    message = G_message_page + VMBUS_MESSAGE_SINT * 256u;
    for (uint32_t round = 0; round < VMBUS_POLL_ROUNDS; ++round)
    {
        uint32_t channel_type;
        const volatile uint8_t *payload;

        if ((round & 0x3FFFu) == 0u)
        {
            (void)hyperv_set_vpreg(HV_REGISTER_EOM, 0u);
            asm_dma_invalidate_range((const void *)message, 256u);
        }
        if (vmbus_get_u32(message) == 0u)
        {
            asm_relax();
            continue;
        }

        payload = message + 16;
        channel_type = vmbus_get_u32(payload);
        if (channel_type == VMBUS_CHANNEL_OFFER)
        {
            ++offer_count;
            if (vmbus_guid_equal(payload + 8, G_storvsc_guid))
            {
                uint32_t relid = vmbus_get_u32(payload + 184);
                uint32_t connection_id = vmbus_get_u32(payload + 192);

                terminal_print("vmbus: StorVSC offer relid=");
                terminal_print_inline_hex32(relid);
                terminal_print(" connection=");
                terminal_print_inline_hex32(connection_id);
                if (!G_storvsc_relid)
                {
                    G_storvsc_relid = relid;
                    G_storvsc_connection_id = connection_id;
                }
            }
            else if (vmbus_guid_equal(payload + 8, G_kbd_guid))
            {
                uint32_t relid = vmbus_get_u32(payload + 184);
                uint32_t connection_id = vmbus_get_u32(payload + 192);

                terminal_print("vmbus: keyboard offer relid=");
                terminal_print_inline_hex32(relid);
                terminal_print(" connection=");
                terminal_print_inline_hex32(connection_id);
                if (!G_kbd_relid)
                {
                    G_kbd_relid = relid;
                    G_kbd_connection_id = connection_id;
                }
            }
            else if (vmbus_guid_equal(payload + 8, G_mouse_guid))
            {
                uint32_t relid = vmbus_get_u32(payload + 184);
                uint32_t connection_id = vmbus_get_u32(payload + 192);

                terminal_print("vmbus: mouse offer relid=");
                terminal_print_inline_hex32(relid);
                terminal_print(" connection=");
                terminal_print_inline_hex32(connection_id);
                if (!G_mouse_relid)
                {
                    G_mouse_relid = relid;
                    G_mouse_connection_id = connection_id;
                }
            }
        }
        else if (channel_type == VMBUS_CHANNEL_ALL_OFFERS_DELIVERED)
        {
            terminal_print("vmbus: all offers delivered count=");
            terminal_print_inline_hex32(offer_count);
            vmbus_release_message(message);
            if (!G_storvsc_relid)
            {
                terminal_error("vmbus: StorVSC channel not offered");
                return -1;
            }
            return 0;
        }
        else
        {
            terminal_print("vmbus: ignoring channel message type=");
            terminal_print_inline_hex32(channel_type);
        }

        vmbus_release_message(message);
    }

    terminal_error("vmbus: channel offer timeout");
    return -1;
}

static int vmbus_wait_channel_response(uint32_t expected_type,
                                       uint32_t *value0,
                                       uint32_t *value1,
                                       uint32_t *value2)
{
    volatile uint8_t *message =
        G_message_page + VMBUS_MESSAGE_SINT * 256u;

    for (uint32_t round = 0; round < VMBUS_POLL_ROUNDS; ++round)
    {
        const volatile uint8_t *payload;
        uint32_t channel_type;

        if ((round & 0x3FFFu) == 0u)
        {
            (void)hyperv_set_vpreg(HV_REGISTER_EOM, 0u);
            asm_dma_invalidate_range((const void *)message, 256u);
        }
        if (vmbus_get_u32(message) == 0u)
        {
            asm_relax();
            continue;
        }

        payload = message + 16;
        channel_type = vmbus_get_u32(payload);
        if (channel_type == expected_type)
        {
            if (value0)
                *value0 = vmbus_get_u32(payload + 8);
            if (value1)
                *value1 = vmbus_get_u32(payload + 12);
            if (value2)
                *value2 = vmbus_get_u32(payload + 16);
            vmbus_release_message(message);
            return 0;
        }

        terminal_print("vmbus: unexpected response while opening type=");
        terminal_print_inline_hex32(channel_type);
        vmbus_release_message(message);
    }
    return -1;
}

static int vmbus_create_ring_gpadl(void)
{
    uint8_t message[92] = {0};
    uint64_t ring_phys;
    uint64_t status;
    uint32_t child = 0u;
    uint32_t handle = 0u;
    uint32_t creation_status = ~0u;

    G_storvsc_ring = (uint8_t *)pmem_alloc_pages(VMBUS_RING_PAGES);
    if (!G_storvsc_ring)
    {
        terminal_error("vmbus: StorVSC ring allocation failed");
        return -1;
    }
    for (uint32_t i = 0; i < VMBUS_RING_PAGES * 4096u; ++i)
        G_storvsc_ring[i] = 0u;
    asm_dma_clean_range(G_storvsc_ring, VMBUS_RING_PAGES * 4096u);
    ring_phys = pmem_virt_to_phys(G_storvsc_ring);

    vmbus_put_u32(message + 0, VMBUS_CHANNEL_GPADL_HEADER);
    vmbus_put_u32(message + 8, G_storvsc_relid);
    vmbus_put_u32(message + 12, VMBUS_GPADL_HANDLE);
    message[16] = 72u;
    message[17] = 0u;
    message[18] = 1u;
    message[19] = 0u;
    vmbus_put_u32(message + 20, VMBUS_RING_PAGES * 4096u);
    vmbus_put_u32(message + 24, 0u);
    for (uint32_t i = 0; i < VMBUS_RING_PAGES; ++i)
        vmbus_put_u64(message + 28u + i * 8u,
                      (ring_phys >> 12) + i);

    terminal_print("vmbus: creating StorVSC ring GPADL handle=");
    terminal_print_inline_hex32(VMBUS_GPADL_HANDLE);
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        message,
                                        sizeof(message));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: GPADL_HEADER post failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    if (vmbus_wait_channel_response(VMBUS_CHANNEL_GPADL_CREATED,
                                    &child,
                                    &handle,
                                    &creation_status) != 0)
    {
        terminal_error("vmbus: GPADL_CREATED timeout");
        return -1;
    }

    terminal_print("vmbus: GPADL_CREATED child=");
    terminal_print_inline_hex32(child);
    terminal_print(" handle=");
    terminal_print_inline_hex32(handle);
    terminal_print(" status=");
    terminal_print_inline_hex32(creation_status);
    if (child != G_storvsc_relid ||
        handle != VMBUS_GPADL_HANDLE ||
        creation_status != 0u)
        return -1;

    G_storvsc_gpadl = handle;
    return 0;
}

int vmbus_open_storvsc_channel(void)
{
    uint8_t message[148] = {0};
    uint64_t status;
    uint32_t child = 0u;
    uint32_t open_id = 0u;
    uint32_t open_status = ~0u;

    if (!G_storvsc_relid || vmbus_create_ring_gpadl() != 0)
        return -1;

    vmbus_put_u32(message + 0, VMBUS_CHANNEL_OPEN);
    vmbus_put_u32(message + 8, G_storvsc_relid);
    vmbus_put_u32(message + 12, VMBUS_OPEN_ID);
    vmbus_put_u32(message + 16, G_storvsc_gpadl);
    vmbus_put_u32(message + 20, 0u);
    vmbus_put_u32(message + 24, VMBUS_RING_DOWNSTREAM_OFFSET);

    terminal_print("vmbus: opening StorVSC channel");
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        message,
                                        sizeof(message));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: OPENCHANNEL post failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    if (vmbus_wait_channel_response(VMBUS_CHANNEL_OPEN_RESULT,
                                    &child,
                                    &open_id,
                                    &open_status) != 0)
    {
        terminal_error("vmbus: OPENCHANNEL_RESULT timeout");
        return -1;
    }

    terminal_print("vmbus: OPENCHANNEL_RESULT child=");
    terminal_print_inline_hex32(child);
    terminal_print(" open_id=");
    terminal_print_inline_hex32(open_id);
    terminal_print(" status=");
    terminal_print_inline_hex32(open_status);
    if (child != G_storvsc_relid ||
        open_id != VMBUS_OPEN_ID ||
        open_status != 0u)
        return -1;
    return 0;
}

static int vmbus_create_kbd_ring_gpadl(void)
{
    uint8_t message[92] = {0};
    uint64_t ring_phys;
    uint64_t status;
    uint32_t child = 0u;
    uint32_t handle = 0u;
    uint32_t creation_status = ~0u;

    G_kbd_ring = (uint8_t *)pmem_alloc_pages(VMBUS_RING_PAGES);
    if (!G_kbd_ring)
    {
        terminal_error("vmbus: keyboard ring allocation failed");
        return -1;
    }
    for (uint32_t i = 0; i < VMBUS_RING_PAGES * 4096u; ++i)
        G_kbd_ring[i] = 0u;
    asm_dma_clean_range(G_kbd_ring, VMBUS_RING_PAGES * 4096u);
    ring_phys = pmem_virt_to_phys(G_kbd_ring);

    vmbus_put_u32(message + 0, VMBUS_CHANNEL_GPADL_HEADER);
    vmbus_put_u32(message + 8, G_kbd_relid);
    vmbus_put_u32(message + 12, VMBUS_KBD_GPADL_HANDLE);
    message[16] = 72u;
    message[17] = 0u;
    message[18] = 1u;
    message[19] = 0u;
    vmbus_put_u32(message + 20, VMBUS_RING_PAGES * 4096u);
    vmbus_put_u32(message + 24, 0u);
    for (uint32_t i = 0; i < VMBUS_RING_PAGES; ++i)
        vmbus_put_u64(message + 28u + i * 8u,
                      (ring_phys >> 12) + i);

    terminal_print("vmbus: creating keyboard ring GPADL handle=");
    terminal_print_inline_hex32(VMBUS_KBD_GPADL_HANDLE);
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        message,
                                        sizeof(message));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: keyboard GPADL_HEADER post failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    if (vmbus_wait_channel_response(VMBUS_CHANNEL_GPADL_CREATED,
                                    &child,
                                    &handle,
                                    &creation_status) != 0)
    {
        terminal_error("vmbus: keyboard GPADL_CREATED timeout");
        return -1;
    }

    if (child != G_kbd_relid ||
        handle != VMBUS_KBD_GPADL_HANDLE ||
        creation_status != 0u)
    {
        terminal_error("vmbus: keyboard GPADL_CREATED invalid");
        return -1;
    }

    G_kbd_gpadl = handle;
    return 0;
}

int vmbus_open_keyboard_channel(void)
{
    uint8_t message[148] = {0};
    uint64_t status;
    uint32_t child = 0u;
    uint32_t open_id = 0u;
    uint32_t open_status = ~0u;

    if (!G_kbd_relid)
        return -1;
    if (G_kbd_ring && G_kbd_gpadl)
        return 0;
    if (vmbus_create_kbd_ring_gpadl() != 0)
        return -1;

    vmbus_put_u32(message + 0, VMBUS_CHANNEL_OPEN);
    vmbus_put_u32(message + 8, G_kbd_relid);
    vmbus_put_u32(message + 12, VMBUS_KBD_OPEN_ID);
    vmbus_put_u32(message + 16, G_kbd_gpadl);
    vmbus_put_u32(message + 20, 0u);
    vmbus_put_u32(message + 24, VMBUS_RING_DOWNSTREAM_OFFSET);

    terminal_print("vmbus: opening keyboard channel");
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        message,
                                        sizeof(message));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: keyboard OPENCHANNEL post failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    if (vmbus_wait_channel_response(VMBUS_CHANNEL_OPEN_RESULT,
                                    &child,
                                    &open_id,
                                    &open_status) != 0)
    {
        terminal_error("vmbus: keyboard OPENCHANNEL_RESULT timeout");
        return -1;
    }

    if (child != G_kbd_relid ||
        open_id != VMBUS_KBD_OPEN_ID ||
        open_status != 0u)
    {
        terminal_error("vmbus: keyboard OPENCHANNEL_RESULT invalid");
        return -1;
    }
    return 0;
}

static int vmbus_kbd_signal(void)
{
    uint8_t *send_events = G_event_page + 2048u;
    uint32_t byte = G_kbd_relid >> 3;
    uint8_t mask = (uint8_t)(1u << (G_kbd_relid & 7u));
    uint64_t status;

    if (!G_event_page || !G_kbd_connection_id)
        return -1;

    asm_dma_invalidate_range(send_events + byte, 1u);
    send_events[byte] |= mask;
    asm_dma_clean_range(send_events + byte, 1u);

    status = hyperv_hypercall(HV_CALL_SIGNAL_EVENT | HV_HYPERCALL_FAST_BIT,
                              G_kbd_connection_id, 0u);
    return ((uint16_t)status == 0u) ? 0 : -1;
}

int vmbus_keyboard_send_inband(const void *payload, uint32_t payload_bytes)
{
    uint8_t packet[512] = {0};
    uint32_t packet_bytes;
    uint32_t packet_aligned;
    uint32_t write_index;
    uint32_t read_index;
    uint32_t next;

    if (!G_kbd_ring || !G_kbd_gpadl || !payload || payload_bytes == 0u ||
        payload_bytes > 256u)
        return -1;

    packet_bytes = 16u + payload_bytes;
    packet_aligned = (packet_bytes + 7u) & ~7u;
    if (packet_aligned + 8u > sizeof(packet))
        return -1;

    write_index = vmbus_get_u32(G_kbd_ring + 0);
    read_index = vmbus_get_u32(G_kbd_ring + 4);
    if (write_index >= VMBUS_RING_DATA_SIZE ||
        read_index >= VMBUS_RING_DATA_SIZE)
        return -1;

    vmbus_put_u16(packet + 0, VMBUS_PACKET_DATA_INBAND);
    vmbus_put_u16(packet + 2, 2u);
    vmbus_put_u16(packet + 4, (uint16_t)(packet_aligned / 8u));
    vmbus_put_u16(packet + 6, VMBUS_PACKET_COMPLETION_REQUESTED);
    vmbus_put_u64(packet + 8, VMBUS_RQST_INIT);
    for (uint32_t i = 0; i < payload_bytes; ++i)
        packet[16u + i] = ((const uint8_t *)payload)[i];

    next = vmbus_ring_copy_to(G_kbd_ring, write_index, packet, packet_aligned);
    next = vmbus_ring_copy_to(G_kbd_ring, next, (const uint8_t *)&write_index, 4u);
    next = vmbus_ring_copy_to(G_kbd_ring, next, (const uint8_t *)&write_index, 4u);
    asm_dma_clean_range(G_kbd_ring + 4096u, VMBUS_RING_DATA_SIZE);
    vmbus_put_u32(G_kbd_ring + 0, next);
    asm_dma_clean_range(G_kbd_ring, 16u);
    return vmbus_kbd_signal();
}

int vmbus_keyboard_receive(uint8_t *payload, uint32_t payload_cap,
                           uint32_t *payload_bytes)
{
    uint8_t *ring;
    uint8_t packet[512];
    uint32_t write_index;
    uint32_t read_index;
    uint32_t packet_len8;
    uint32_t packet_offset8;
    uint32_t packet_bytes;
    uint32_t data_bytes;
    uint32_t next;

    if (payload_bytes)
        *payload_bytes = 0u;
    if (!G_kbd_ring || !payload || payload_cap == 0u)
        return -1;

    ring = G_kbd_ring + VMBUS_RING_DOWNSTREAM_OFFSET * 4096u;
    asm_dma_invalidate_range(ring, VMBUS_RING_DATA_SIZE + 16u);
    write_index = vmbus_get_u32(ring + 0);
    read_index = vmbus_get_u32(ring + 4);
    if (write_index == read_index)
        return 1;
    if (write_index >= VMBUS_RING_DATA_SIZE ||
        read_index >= VMBUS_RING_DATA_SIZE)
        return -1;

    vmbus_ring_copy_from(ring, read_index, packet, 16u);
    packet_offset8 = vmbus_get_u16(packet + 2);
    packet_len8 = vmbus_get_u16(packet + 4);
    packet_bytes = packet_len8 * 8u;
    if (packet_bytes < 16u || packet_bytes > sizeof(packet) ||
        packet_offset8 * 8u > packet_bytes)
        return -1;

    vmbus_ring_copy_from(ring, read_index, packet, packet_bytes);
    data_bytes = packet_bytes - packet_offset8 * 8u;
    if (data_bytes > payload_cap)
        data_bytes = payload_cap;
    for (uint32_t i = 0; i < data_bytes; ++i)
        payload[i] = packet[packet_offset8 * 8u + i];

    next = (read_index + packet_bytes + 8u) % VMBUS_RING_DATA_SIZE;
    vmbus_put_u32(ring + 4, next);
    asm_dma_clean_range(ring, 16u);
    if (payload_bytes)
        *payload_bytes = data_bytes;
    return 0;
}

static int vmbus_create_mouse_ring_gpadl(void)
{
    uint8_t message[92] = {0};
    uint64_t ring_phys;
    uint64_t status;
    uint32_t child = 0u;
    uint32_t handle = 0u;
    uint32_t creation_status = ~0u;

    G_mouse_ring = (uint8_t *)pmem_alloc_pages(VMBUS_RING_PAGES);
    if (!G_mouse_ring)
    {
        terminal_error("vmbus: mouse ring allocation failed");
        return -1;
    }
    for (uint32_t i = 0; i < VMBUS_RING_PAGES * 4096u; ++i)
        G_mouse_ring[i] = 0u;
    asm_dma_clean_range(G_mouse_ring, VMBUS_RING_PAGES * 4096u);
    ring_phys = pmem_virt_to_phys(G_mouse_ring);

    vmbus_put_u32(message + 0, VMBUS_CHANNEL_GPADL_HEADER);
    vmbus_put_u32(message + 8, G_mouse_relid);
    vmbus_put_u32(message + 12, VMBUS_MOUSE_GPADL_HANDLE);
    message[16] = 72u;
    message[17] = 0u;
    message[18] = 1u;
    message[19] = 0u;
    vmbus_put_u32(message + 20, VMBUS_RING_PAGES * 4096u);
    vmbus_put_u32(message + 24, 0u);
    for (uint32_t i = 0; i < VMBUS_RING_PAGES; ++i)
        vmbus_put_u64(message + 28u + i * 8u,
                      (ring_phys >> 12) + i);

    terminal_print("vmbus: creating mouse ring GPADL handle=");
    terminal_print_inline_hex32(VMBUS_MOUSE_GPADL_HANDLE);
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        message,
                                        sizeof(message));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: mouse GPADL_HEADER post failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    if (vmbus_wait_channel_response(VMBUS_CHANNEL_GPADL_CREATED,
                                    &child,
                                    &handle,
                                    &creation_status) != 0)
    {
        terminal_error("vmbus: mouse GPADL_CREATED timeout");
        return -1;
    }

    if (child != G_mouse_relid ||
        handle != VMBUS_MOUSE_GPADL_HANDLE ||
        creation_status != 0u)
    {
        terminal_error("vmbus: mouse GPADL_CREATED invalid");
        return -1;
    }

    G_mouse_gpadl = handle;
    return 0;
}

int vmbus_open_mouse_channel(void)
{
    uint8_t message[148] = {0};
    uint64_t status;
    uint32_t child = 0u;
    uint32_t open_id = 0u;
    uint32_t open_status = ~0u;

    if (!G_mouse_relid)
        return -1;
    if (G_mouse_ring && G_mouse_gpadl)
        return 0;
    if (vmbus_create_mouse_ring_gpadl() != 0)
        return -1;

    vmbus_put_u32(message + 0, VMBUS_CHANNEL_OPEN);
    vmbus_put_u32(message + 8, G_mouse_relid);
    vmbus_put_u32(message + 12, VMBUS_MOUSE_OPEN_ID);
    vmbus_put_u32(message + 16, G_mouse_gpadl);
    vmbus_put_u32(message + 20, 0u);
    vmbus_put_u32(message + 24, VMBUS_RING_DOWNSTREAM_OFFSET);

    terminal_print("vmbus: opening mouse channel");
    status = vmbus_post_channel_message(G_vmbus_message_connection,
                                        message,
                                        sizeof(message));
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: mouse OPENCHANNEL post failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    if (vmbus_wait_channel_response(VMBUS_CHANNEL_OPEN_RESULT,
                                    &child,
                                    &open_id,
                                    &open_status) != 0)
    {
        terminal_error("vmbus: mouse OPENCHANNEL_RESULT timeout");
        return -1;
    }

    if (child != G_mouse_relid ||
        open_id != VMBUS_MOUSE_OPEN_ID ||
        open_status != 0u)
    {
        terminal_error("vmbus: mouse OPENCHANNEL_RESULT invalid");
        return -1;
    }
    return 0;
}

static int vmbus_mouse_signal(void)
{
    uint8_t *send_events = G_event_page + 2048u;
    uint32_t byte = G_mouse_relid >> 3;
    uint8_t mask = (uint8_t)(1u << (G_mouse_relid & 7u));
    uint64_t status;

    if (!G_event_page || !G_mouse_connection_id)
        return -1;

    asm_dma_invalidate_range(send_events + byte, 1u);
    send_events[byte] |= mask;
    asm_dma_clean_range(send_events + byte, 1u);

    status = hyperv_hypercall(HV_CALL_SIGNAL_EVENT | HV_HYPERCALL_FAST_BIT,
                              G_mouse_connection_id, 0u);
    return ((uint16_t)status == 0u) ? 0 : -1;
}

int vmbus_mouse_send_inband(const void *payload, uint32_t payload_bytes)
{
    uint8_t packet[512] = {0};
    uint32_t packet_bytes;
    uint32_t packet_aligned;
    uint32_t write_index;
    uint32_t read_index;
    uint32_t next;

    if (!G_mouse_ring || !G_mouse_gpadl || !payload || payload_bytes == 0u ||
        payload_bytes > 256u)
        return -1;

    packet_bytes = 16u + payload_bytes;
    packet_aligned = (packet_bytes + 7u) & ~7u;
    if (packet_aligned + 8u > sizeof(packet))
        return -1;

    write_index = vmbus_get_u32(G_mouse_ring + 0);
    read_index = vmbus_get_u32(G_mouse_ring + 4);
    if (write_index >= VMBUS_RING_DATA_SIZE ||
        read_index >= VMBUS_RING_DATA_SIZE)
        return -1;

    vmbus_put_u16(packet + 0, VMBUS_PACKET_DATA_INBAND);
    vmbus_put_u16(packet + 2, 2u);
    vmbus_put_u16(packet + 4, (uint16_t)(packet_aligned / 8u));
    vmbus_put_u16(packet + 6, VMBUS_PACKET_COMPLETION_REQUESTED);
    vmbus_put_u64(packet + 8, VMBUS_RQST_INIT);
    for (uint32_t i = 0; i < payload_bytes; ++i)
        packet[16u + i] = ((const uint8_t *)payload)[i];

    next = vmbus_ring_copy_to(G_mouse_ring, write_index, packet, packet_aligned);
    next = vmbus_ring_copy_to(G_mouse_ring, next, (const uint8_t *)&write_index, 4u);
    next = vmbus_ring_copy_to(G_mouse_ring, next, (const uint8_t *)&write_index, 4u);
    asm_dma_clean_range(G_mouse_ring + 4096u, VMBUS_RING_DATA_SIZE);
    vmbus_put_u32(G_mouse_ring + 0, next);
    asm_dma_clean_range(G_mouse_ring, 16u);
    return vmbus_mouse_signal();
}

int vmbus_mouse_receive(uint8_t *payload, uint32_t payload_cap,
                        uint32_t *payload_bytes)
{
    uint8_t *ring;
    uint8_t packet[512];
    uint32_t write_index;
    uint32_t read_index;
    uint32_t packet_len8;
    uint32_t packet_offset8;
    uint32_t packet_bytes;
    uint32_t data_bytes;
    uint32_t next;

    if (payload_bytes)
        *payload_bytes = 0u;
    if (!G_mouse_ring || !payload || payload_cap == 0u)
        return -1;

    ring = G_mouse_ring + VMBUS_RING_DOWNSTREAM_OFFSET * 4096u;
    asm_dma_invalidate_range(ring, VMBUS_RING_DATA_SIZE + 16u);
    write_index = vmbus_get_u32(ring + 0);
    read_index = vmbus_get_u32(ring + 4);
    if (write_index == read_index)
        return 1;
    if (write_index >= VMBUS_RING_DATA_SIZE ||
        read_index >= VMBUS_RING_DATA_SIZE)
        return -1;

    vmbus_ring_copy_from(ring, read_index, packet, 16u);
    packet_offset8 = vmbus_get_u16(packet + 2);
    packet_len8 = vmbus_get_u16(packet + 4);
    packet_bytes = packet_len8 * 8u;
    if (packet_bytes < 16u || packet_bytes > sizeof(packet) ||
        packet_offset8 * 8u > packet_bytes)
        return -1;

    vmbus_ring_copy_from(ring, read_index, packet, packet_bytes);
    data_bytes = packet_bytes - packet_offset8 * 8u;
    if (data_bytes > payload_cap)
        data_bytes = payload_cap;
    for (uint32_t i = 0; i < data_bytes; ++i)
        payload[i] = packet[packet_offset8 * 8u + i];

    next = (read_index + packet_bytes + 8u) % VMBUS_RING_DATA_SIZE;
    vmbus_put_u32(ring + 4, next);
    asm_dma_clean_range(ring, 16u);
    if (payload_bytes)
        *payload_bytes = data_bytes;
    return 0;
}

static int vmbus_storvsc_signal(void)
{
    uint8_t *send_events = G_event_page + 2048u;
    uint32_t byte = G_storvsc_relid >> 3;
    uint8_t mask = (uint8_t)(1u << (G_storvsc_relid & 7u));
    uint64_t status;

    /*
     * Hyper-V clears this shared bit after consuming a notification.
     * Discard our cached copy first so every new ring write publishes a
     * genuine zero-to-one transition instead of cleaning a stale one.
     */
    asm_dma_invalidate_range(send_events + byte, 1u);
    send_events[byte] |= mask;
    asm_dma_clean_range(send_events + byte, 1u);
    status = hyperv_hypercall(HV_CALL_SIGNAL_EVENT | HV_HYPERCALL_FAST_BIT,
                              G_storvsc_connection_id, 0u);
    if ((uint16_t)status != 0u)
    {
        terminal_error("vmbus: StorVSC SIGNAL_EVENT failed status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    return 0;
}

static int vmbus_storvsc_send_inband(const uint8_t payload[64])
{
    uint8_t packet[80] = {0};
    uint8_t trailer[8];
    volatile uint8_t *ring = G_storvsc_ring;
    uint32_t write_index;
    uint32_t read_index;
    uint32_t next;

    asm_dma_invalidate_range((const void *)ring, 16u);
    write_index = vmbus_get_u32(ring + 0);
    read_index = vmbus_get_u32(ring + 4);
    if (write_index >= VMBUS_RING_DATA_SIZE ||
        read_index >= VMBUS_RING_DATA_SIZE)
    {
        terminal_error("vmbus: StorVSC outbound ring indices invalid");
        return -1;
    }

    vmbus_put_u16(packet + 0, VMBUS_PACKET_DATA_INBAND);
    vmbus_put_u16(packet + 2, 2u);
    vmbus_put_u16(packet + 4, 10u);
    vmbus_put_u16(packet + 6, VMBUS_PACKET_COMPLETION_REQUESTED);
    vmbus_put_u64(packet + 8, VMBUS_RQST_INIT);
    for (uint32_t i = 0; i < 64u; ++i)
        packet[16u + i] = payload[i];

    vmbus_put_u32(trailer + 0, write_index);
    vmbus_put_u32(trailer + 4, read_index);
    next = vmbus_ring_copy_to(G_storvsc_ring, write_index,
                              packet, sizeof(packet));
    next = vmbus_ring_copy_to(G_storvsc_ring, next,
                              trailer, sizeof(trailer));
    asm_dma_clean_range(G_storvsc_ring + 4096u, VMBUS_RING_DATA_SIZE);
    vmbus_put_u32(G_storvsc_ring + 0, next);
    asm_dma_clean_range(G_storvsc_ring, 16u);
    return vmbus_storvsc_signal();
}

static int vmbus_storvsc_send_gpa_direct(const uint8_t payload[64],
                                         void *buffer,
                                         uint32_t buffer_bytes)
{
    uint8_t packet[104] = {0};
    uint8_t trailer[8];
    volatile uint8_t *ring = G_storvsc_ring;
    uint64_t buffer_phys;
    uint32_t write_index;
    uint32_t read_index;
    uint32_t next;

    if (!buffer || buffer_bytes == 0u || buffer_bytes > 4096u)
        return -1;
    buffer_phys = pmem_virt_to_phys(buffer);
    if (!buffer_phys)
        return -1;

    asm_dma_invalidate_range((const void *)ring, 16u);
    write_index = vmbus_get_u32(ring + 0);
    read_index = vmbus_get_u32(ring + 4);
    if (write_index >= VMBUS_RING_DATA_SIZE ||
        read_index >= VMBUS_RING_DATA_SIZE)
    {
        terminal_error("vmbus: StorVSC outbound ring indices invalid");
        return -1;
    }

    vmbus_put_u16(packet + 0, VMBUS_PACKET_DATA_USING_GPA_DIRECT);
    vmbus_put_u16(packet + 2, 5u);
    vmbus_put_u16(packet + 4, 13u);
    vmbus_put_u16(packet + 6, VMBUS_PACKET_COMPLETION_REQUESTED);
    vmbus_put_u64(packet + 8, VMBUS_RQST_INIT);
    vmbus_put_u32(packet + 16, 0u);
    vmbus_put_u32(packet + 20, 1u);
    vmbus_put_u32(packet + 24, buffer_bytes);
    vmbus_put_u32(packet + 28, (uint32_t)(buffer_phys & 0xFFFu));
    vmbus_put_u64(packet + 32, buffer_phys >> 12);
    for (uint32_t i = 0; i < 64u; ++i)
        packet[40u + i] = payload[i];

    vmbus_put_u32(trailer + 0, write_index);
    vmbus_put_u32(trailer + 4, read_index);
    next = vmbus_ring_copy_to(G_storvsc_ring, write_index,
                              packet, sizeof(packet));
    next = vmbus_ring_copy_to(G_storvsc_ring, next,
                              trailer, sizeof(trailer));
    asm_dma_clean_range(buffer, buffer_bytes);
    asm_dma_clean_range(G_storvsc_ring + 4096u, VMBUS_RING_DATA_SIZE);
    vmbus_put_u32(G_storvsc_ring + 0, next);
    asm_dma_clean_range(G_storvsc_ring, 16u);
    return vmbus_storvsc_signal();
}

static int vmbus_storvsc_receive(uint8_t payload[64],
                                 uint32_t *operation, uint32_t *status)
{
    uint8_t descriptor[16];
    uint8_t packet[80];
    uint8_t *ring = G_storvsc_ring + VMBUS_RING_DOWNSTREAM_OFFSET * 4096u;

    for (uint32_t round = 0; round < VMBUS_POLL_ROUNDS; ++round)
    {
        uint32_t write_index;
        uint32_t read_index;
        uint32_t packet_bytes;
        uint32_t payload_offset;
        uint32_t next;

        if ((round & 0x3FFFu) != 0u)
        {
            asm_relax();
            continue;
        }
        asm_dma_invalidate_range(ring, 4u * 4096u);
        write_index = vmbus_get_u32(ring + 0);
        read_index = vmbus_get_u32(ring + 4);
        if (write_index == read_index)
            continue;
        if (write_index >= VMBUS_RING_DATA_SIZE ||
            read_index >= VMBUS_RING_DATA_SIZE)
        {
            terminal_error("vmbus: StorVSC inbound ring indices invalid");
            return -1;
        }

        vmbus_ring_copy_from(ring, read_index,
                             descriptor, sizeof(descriptor));
        packet_bytes = (uint32_t)vmbus_get_u16(descriptor + 4) * 8u;
        payload_offset = (uint32_t)vmbus_get_u16(descriptor + 2) * 8u;
        if (packet_bytes < payload_offset + 64u ||
            packet_bytes > sizeof(packet))
        {
            terminal_error("vmbus: StorVSC response packet length invalid=");
            terminal_print_inline_hex32(packet_bytes);
            return -1;
        }

        vmbus_ring_copy_from(ring, read_index, packet, packet_bytes);
        for (uint32_t i = 0; i < 64u; ++i)
            payload[i] = packet[payload_offset + i];
        next = (read_index + packet_bytes + 8u) % VMBUS_RING_DATA_SIZE;
        vmbus_put_u32(ring + 4, next);
        asm_dma_clean_range(ring, 16u);
        if (operation)
            *operation = vmbus_get_u32(payload + 0);
        if (status)
            *status = vmbus_get_u32(payload + 8);
        return 0;
    }
    return -1;
}

static int vmbus_storvsc_wait_completion(uint8_t payload[64],
                                         uint32_t *operation,
                                         uint32_t *status)
{
    for (uint32_t attempt = 0; attempt < 3u; ++attempt)
    {
        if (vmbus_storvsc_receive(payload, operation, status) == 0)
            return 0;
        if (attempt + 1u < 3u)
        {
            terminal_warn("storvsc: completion delayed; re-signalling attempt=");
            terminal_print_inline_hex32(attempt + 1u);
            if (vmbus_storvsc_signal() != 0)
                return -1;
        }
    }
    return -1;
}

int vmbus_storvsc_begin_initialization(void)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint32_t operation = 0u;
    uint32_t status = ~0u;

    if (!G_storvsc_ring || !G_storvsc_gpadl)
        return -1;
    vmbus_put_u32(request + 0, VSTOR_OPERATION_BEGIN_INITIALIZATION);
    vmbus_put_u32(request + 4, 1u);
    terminal_print("storvsc: sending VSTOR BEGIN_INITIALIZATION");
    if (vmbus_storvsc_send_inband(request) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: BEGIN_INITIALIZATION response timeout");
        return -1;
    }

    terminal_print("storvsc: BEGIN_INITIALIZATION completion operation=");
    terminal_print_inline_hex32(operation);
    terminal_print(" status=");
    terminal_print_inline_hex32(status);
    if (operation != VSTOR_OPERATION_COMPLETE_IO || status != 0u)
        return -1;
    return 0;
}

int vmbus_storvsc_negotiate_protocol(uint16_t *negotiated_version)
{
    static const uint16_t versions[] = {
        0x0602u,
        0x0600u,
        0x0501u,
    };

    if (negotiated_version)
        *negotiated_version = 0u;

    for (uint32_t attempt = 0;
         attempt < sizeof(versions) / sizeof(versions[0]);
         ++attempt)
    {
        uint8_t request[64] = {0};
        uint8_t response[64] = {0};
        uint32_t operation = 0u;
        uint32_t status = ~0u;

        vmbus_put_u32(request + 0,
                      VSTOR_OPERATION_QUERY_PROTOCOL_VERSION);
        vmbus_put_u32(request + 4, 1u);
        vmbus_put_u16(request + 12, versions[attempt]);
        vmbus_put_u16(request + 14, 0u);

        terminal_print("storvsc: querying VSTOR protocol version=");
        terminal_print_inline_hex32(versions[attempt]);
        if (vmbus_storvsc_send_inband(request) != 0)
            return -1;
        if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
        {
            terminal_error("storvsc: protocol version response timeout");
            return -1;
        }

        terminal_print("storvsc: protocol response operation=");
        terminal_print_inline_hex32(operation);
        terminal_print(" status=");
        terminal_print_inline_hex32(status);
        if (operation != VSTOR_OPERATION_COMPLETE_IO)
        {
            terminal_error("storvsc: protocol response operation invalid");
            return -1;
        }
        if (status == 0u)
        {
            if (negotiated_version)
                *negotiated_version = versions[attempt];
            terminal_print("storvsc: negotiated VSTOR protocol=");
            terminal_print_inline_hex32(versions[attempt]);
            return 0;
        }
    }

    terminal_error("storvsc: host rejected all supported VSTOR versions");
    return -1;
}

int vmbus_storvsc_query_properties(uint16_t *max_channels,
                                   uint32_t *flags,
                                   uint32_t *max_transfer_bytes)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint32_t operation = 0u;
    uint32_t status = ~0u;

    if (max_channels)
        *max_channels = 0u;
    if (flags)
        *flags = 0u;
    if (max_transfer_bytes)
        *max_transfer_bytes = 0u;

    vmbus_put_u32(request + 0, VSTOR_OPERATION_QUERY_PROPERTIES);
    vmbus_put_u32(request + 4, 1u);
    terminal_print("storvsc: querying storage channel properties");
    if (vmbus_storvsc_send_inband(request) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: storage properties response timeout");
        return -1;
    }

    terminal_print("storvsc: properties response operation=");
    terminal_print_inline_hex32(operation);
    terminal_print(" status=");
    terminal_print_inline_hex32(status);
    if (operation != VSTOR_OPERATION_COMPLETE_IO || status != 0u)
        return -1;

    if (max_channels)
        *max_channels = vmbus_get_u16(response + 16);
    if (flags)
        *flags = vmbus_get_u32(response + 20);
    if (max_transfer_bytes)
        *max_transfer_bytes = vmbus_get_u32(response + 24);

    terminal_print("storvsc: properties max_channels=");
    terminal_print_inline_hex32(vmbus_get_u16(response + 16));
    terminal_print(" flags=");
    terminal_print_inline_hex32(vmbus_get_u32(response + 20));
    terminal_print(" max_transfer_bytes=");
    terminal_print_inline_hex32(vmbus_get_u32(response + 24));
    return 0;
}

int vmbus_storvsc_end_initialization(void)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint32_t operation = 0u;
    uint32_t status = ~0u;

    vmbus_put_u32(request + 0, VSTOR_OPERATION_END_INITIALIZATION);
    vmbus_put_u32(request + 4, 1u);
    terminal_print("storvsc: sending VSTOR END_INITIALIZATION");
    if (vmbus_storvsc_send_inband(request) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: END_INITIALIZATION response timeout");
        return -1;
    }

    terminal_print("storvsc: END_INITIALIZATION completion operation=");
    terminal_print_inline_hex32(operation);
    terminal_print(" status=");
    terminal_print_inline_hex32(status);
    if (operation != VSTOR_OPERATION_COMPLETE_IO || status != 0u)
        return -1;
    return 0;
}

int vmbus_storvsc_test_unit_ready(uint8_t target, uint8_t lun)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint32_t operation = 0u;
    uint32_t status = ~0u;
    uint8_t srb_status;
    uint8_t scsi_status;

    vmbus_put_u32(request + 0, VSTOR_OPERATION_EXECUTE_SRB);
    vmbus_put_u32(request + 4, 1u);
    vmbus_put_u16(request + 12, 52u);
    request[16] = 0u;
    request[17] = 0u;
    request[18] = target;
    request[19] = lun;
    request[20] = 6u;
    request[21] = 20u;
    request[22] = 0u;
    vmbus_put_u32(request + 24, 0u);
    request[28] = SCSI_OPERATION_TEST_UNIT_READY;
    vmbus_put_u32(request + 52, SRB_FLAGS_DISABLE_SYNCH_TRANSFER);
    vmbus_put_u32(request + 56, 60u);

    terminal_print("storvsc: SCSI TEST_UNIT_READY target=");
    terminal_print_inline_hex32(target);
    terminal_print(" lun=");
    terminal_print_inline_hex32(lun);
    if (vmbus_storvsc_send_inband(request) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: TEST_UNIT_READY response timeout");
        return -1;
    }

    srb_status = response[14];
    scsi_status = response[15];
    terminal_print("storvsc: TEST_UNIT_READY completion operation=");
    terminal_print_inline_hex32(operation);
    terminal_print(" vstor_status=");
    terminal_print_inline_hex32(status);
    terminal_print(" srb_status=");
    terminal_print_inline_hex32(srb_status);
    terminal_print(" scsi_status=");
    terminal_print_inline_hex32(scsi_status);
    terminal_print(" transfer_bytes=");
    terminal_print_inline_hex32(vmbus_get_u32(response + 24));

    if (operation != VSTOR_OPERATION_COMPLETE_IO ||
        status != 0u ||
        (srb_status & 0x3Fu) != SRB_STATUS_SUCCESS ||
        scsi_status != 0u)
        return -1;
    return 0;
}

int vmbus_storvsc_inquiry(uint8_t target, uint8_t lun)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint8_t *inquiry = (uint8_t *)pmem_alloc_pages(1);
    char vendor[9];
    char product[17];
    uint32_t operation = 0u;
    uint32_t status = ~0u;
    uint8_t srb_status;
    uint8_t scsi_status;

    if (!inquiry)
    {
        terminal_error("storvsc: INQUIRY buffer allocation failed");
        return -1;
    }
    vmbus_zero_page(inquiry);

    vmbus_put_u32(request + 0, VSTOR_OPERATION_EXECUTE_SRB);
    vmbus_put_u32(request + 4, 1u);
    vmbus_put_u16(request + 12, 52u);
    request[18] = target;
    request[19] = lun;
    request[20] = 6u;
    request[21] = 20u;
    request[22] = 1u;
    vmbus_put_u32(request + 24, 36u);
    request[28] = SCSI_OPERATION_INQUIRY;
    request[32] = 36u;
    vmbus_put_u32(request + 52,
                  SRB_FLAGS_DISABLE_SYNCH_TRANSFER | SRB_FLAGS_DATA_IN);
    vmbus_put_u32(request + 56, 60u);

    terminal_print("storvsc: SCSI INQUIRY target=0 lun=0 bytes=36");
    if (vmbus_storvsc_send_gpa_direct(request, inquiry, 36u) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: INQUIRY response timeout");
        return -1;
    }
    asm_dma_invalidate_range(inquiry, 36u);

    srb_status = response[14];
    scsi_status = response[15];
    terminal_print("storvsc: INQUIRY completion operation=");
    terminal_print_inline_hex32(operation);
    terminal_print(" vstor_status=");
    terminal_print_inline_hex32(status);
    terminal_print(" srb_status=");
    terminal_print_inline_hex32(srb_status);
    terminal_print(" scsi_status=");
    terminal_print_inline_hex32(scsi_status);
    terminal_print(" transfer_bytes=");
    terminal_print_inline_hex32(vmbus_get_u32(response + 24));
    if (operation != VSTOR_OPERATION_COMPLETE_IO ||
        status != 0u ||
        (srb_status & 0x3Fu) != SRB_STATUS_SUCCESS ||
        scsi_status != 0u ||
        vmbus_get_u32(response + 24) < 36u)
        return -1;

    for (uint32_t i = 0; i < 8u; ++i)
        vendor[i] = (char)inquiry[8u + i];
    vendor[8] = 0;
    for (uint32_t i = 0; i < 16u; ++i)
        product[i] = (char)inquiry[16u + i];
    product[16] = 0;
    terminal_print("storvsc: SCSI device type=");
    terminal_print_inline_hex32(inquiry[0] & 0x1Fu);
    terminal_print(" vendor=");
    terminal_print_inline(vendor);
    terminal_print(" product=");
    terminal_print_inline(product);
    return 0;
}

int vmbus_storvsc_read_capacity(uint8_t target, uint8_t lun,
                                uint64_t *block_count,
                                uint32_t *block_size)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint8_t *capacity = (uint8_t *)pmem_alloc_pages(1);
    uint32_t operation = 0u;
    uint32_t status = ~0u;
    uint32_t last_lba;
    uint32_t bytes_per_block;
    uint64_t blocks;

    if (block_count)
        *block_count = 0u;
    if (block_size)
        *block_size = 0u;
    if (!capacity)
    {
        terminal_error("storvsc: READ CAPACITY buffer allocation failed");
        return -1;
    }
    vmbus_zero_page(capacity);

    vmbus_put_u32(request + 0, VSTOR_OPERATION_EXECUTE_SRB);
    vmbus_put_u32(request + 4, 1u);
    vmbus_put_u16(request + 12, 52u);
    request[18] = target;
    request[19] = lun;
    request[20] = 10u;
    request[21] = 20u;
    request[22] = 1u;
    vmbus_put_u32(request + 24, 8u);
    request[28] = SCSI_OPERATION_READ_CAPACITY_10;
    vmbus_put_u32(request + 52,
                  SRB_FLAGS_DISABLE_SYNCH_TRANSFER | SRB_FLAGS_DATA_IN);
    vmbus_put_u32(request + 56, 60u);

    terminal_print("storvsc: SCSI READ_CAPACITY_10 target=0 lun=0");
    if (vmbus_storvsc_send_gpa_direct(request, capacity, 8u) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: READ CAPACITY response timeout");
        return -1;
    }
    asm_dma_invalidate_range(capacity, 8u);

    terminal_print("storvsc: READ CAPACITY completion operation=");
    terminal_print_inline_hex32(operation);
    terminal_print(" vstor_status=");
    terminal_print_inline_hex32(status);
    terminal_print(" srb_status=");
    terminal_print_inline_hex32(response[14]);
    terminal_print(" scsi_status=");
    terminal_print_inline_hex32(response[15]);
    terminal_print(" transfer_bytes=");
    terminal_print_inline_hex32(vmbus_get_u32(response + 24));
    if (operation != VSTOR_OPERATION_COMPLETE_IO ||
        status != 0u ||
        (response[14] & 0x3Fu) != SRB_STATUS_SUCCESS ||
        response[15] != 0u ||
        vmbus_get_u32(response + 24) < 8u)
        return -1;

    last_lba = vmbus_get_be32(capacity + 0);
    bytes_per_block = vmbus_get_be32(capacity + 4);
    if (last_lba == 0xFFFFFFFFu || bytes_per_block == 0u)
    {
        terminal_error("storvsc: READ CAPACITY requires unsupported 16-byte form");
        return -1;
    }
    blocks = (uint64_t)last_lba + 1u;
    if (block_count)
        *block_count = blocks;
    if (block_size)
        *block_size = bytes_per_block;

    terminal_print("storvsc: capacity last_lba=");
    terminal_print_inline_hex32(last_lba);
    terminal_print(" blocks=");
    terminal_print_inline_hex64(blocks);
    terminal_print(" block_size=");
    terminal_print_inline_hex32(bytes_per_block);
    terminal_print(" disk_mib=");
    terminal_print_inline_hex64((blocks * bytes_per_block) >> 20);
    return 0;
}

int vmbus_storvsc_read10(uint8_t target, uint8_t lun,
                         uint32_t lba, uint16_t block_count,
                         void *buffer, uint32_t buffer_bytes)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint32_t operation = 0u;
    uint32_t status = ~0u;

    if (!buffer || !block_count || !buffer_bytes || buffer_bytes > 4096u)
        return -1;

    vmbus_put_u32(request + 0, VSTOR_OPERATION_EXECUTE_SRB);
    vmbus_put_u32(request + 4, 1u);
    vmbus_put_u16(request + 12, 52u);
    request[18] = target;
    request[19] = lun;
    request[20] = 10u;
    request[21] = 20u;
    request[22] = 1u;
    vmbus_put_u32(request + 24, buffer_bytes);
    request[28] = SCSI_OPERATION_READ_10;
    vmbus_put_be32(request + 30, lba);
    vmbus_put_be16(request + 35, block_count);
    vmbus_put_u32(request + 52,
                  SRB_FLAGS_DISABLE_SYNCH_TRANSFER | SRB_FLAGS_DATA_IN);
    vmbus_put_u32(request + 56, 60u);

    if (vmbus_storvsc_send_gpa_direct(request, buffer, buffer_bytes) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: READ_10 response timeout after re-signals");
        return -1;
    }
    asm_dma_invalidate_range(buffer, buffer_bytes);

    if (operation != VSTOR_OPERATION_COMPLETE_IO ||
        status != 0u ||
        (response[14] & 0x3Fu) != SRB_STATUS_SUCCESS ||
        response[15] != 0u ||
        vmbus_get_u32(response + 24) != buffer_bytes)
        return -1;
    return 0;
}

int vmbus_storvsc_write10(uint8_t target, uint8_t lun,
                          uint32_t lba, uint16_t block_count,
                          const void *buffer, uint32_t buffer_bytes)
{
    uint8_t request[64] = {0};
    uint8_t response[64] = {0};
    uint32_t operation = 0u;
    uint32_t status = ~0u;

    if (!buffer || !block_count || !buffer_bytes || buffer_bytes > 4096u)
        return -1;

    vmbus_put_u32(request + 0, VSTOR_OPERATION_EXECUTE_SRB);
    vmbus_put_u32(request + 4, 1u);
    vmbus_put_u16(request + 12, 52u);
    request[18] = target;
    request[19] = lun;
    request[20] = 10u;
    request[21] = 20u;
    request[22] = 0u;
    vmbus_put_u32(request + 24, buffer_bytes);
    request[28] = SCSI_OPERATION_WRITE_10;
    vmbus_put_be32(request + 30, lba);
    vmbus_put_be16(request + 35, block_count);
    vmbus_put_u32(request + 52,
                  SRB_FLAGS_DISABLE_SYNCH_TRANSFER | SRB_FLAGS_DATA_OUT);
    vmbus_put_u32(request + 56, 60u);

    if (vmbus_storvsc_send_gpa_direct(request, (void *)buffer, buffer_bytes) != 0)
        return -1;
    if (vmbus_storvsc_wait_completion(response, &operation, &status) != 0)
    {
        terminal_error("storvsc: WRITE_10 response timeout after re-signals");
        return -1;
    }

    if (operation != VSTOR_OPERATION_COMPLETE_IO ||
        status != 0u ||
        (response[14] & 0x3Fu) != SRB_STATUS_SUCCESS ||
        response[15] != 0u)
        return -1;
    return 0;
}

uint32_t vmbus_negotiated_version(void)
{
    return G_vmbus_version;
}

uint32_t vmbus_storvsc_relid(void)
{
    return G_storvsc_relid;
}

uint32_t vmbus_storvsc_connection_id(void)
{
    return G_storvsc_connection_id;
}
