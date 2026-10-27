#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int vmbus_probe_contact(uint64_t acpi_rsdp);
    int vmbus_find_storvsc_offer(void);
    int vmbus_open_storvsc_channel(void);
    int vmbus_storvsc_begin_initialization(void);
    int vmbus_storvsc_negotiate_protocol(uint16_t *negotiated_version);
    int vmbus_storvsc_query_properties(uint16_t *max_channels,
                                       uint32_t *flags,
                                       uint32_t *max_transfer_bytes);
    int vmbus_storvsc_end_initialization(void);
    int vmbus_storvsc_test_unit_ready(uint8_t target, uint8_t lun);
    int vmbus_storvsc_inquiry(uint8_t target, uint8_t lun);
    int vmbus_storvsc_read_capacity(uint8_t target, uint8_t lun,
                                    uint64_t *block_count,
                                    uint32_t *block_size);
    int vmbus_storvsc_read10(uint8_t target, uint8_t lun,
                             uint32_t lba, uint16_t block_count,
                             void *buffer, uint32_t buffer_bytes);
    int vmbus_storvsc_write10(uint8_t target, uint8_t lun,
                              uint32_t lba, uint16_t block_count,
                              const void *buffer, uint32_t buffer_bytes);
    int vmbus_open_keyboard_channel(void);
    int vmbus_keyboard_send_inband(const void *payload, uint32_t payload_bytes);
    int vmbus_keyboard_receive(uint8_t *payload, uint32_t payload_cap,
                               uint32_t *payload_bytes);
    int vmbus_open_mouse_channel(void);
    int vmbus_mouse_send_inband(const void *payload, uint32_t payload_bytes);
    int vmbus_mouse_receive(uint8_t *payload, uint32_t payload_cap,
                            uint32_t *payload_bytes);
    uint32_t vmbus_negotiated_version(void);
    uint32_t vmbus_storvsc_relid(void);
    uint32_t vmbus_storvsc_connection_id(void);

    enum { VMBUS_DEVICE_NET = 0, VMBUS_DEVICE_VIDEO = 1 };
    enum { VMBUS_SEND_REQUEST_COMPLETION = 0x8000 };
    /* Init/open/GPADL calls are serialized during boot. Each device owns its
     * ring after open; a caller must serialize send/receive on that device. */
    void vmbus_device_diag_print(uint32_t device);
    int vmbus_device_open(uint32_t device);
    int vmbus_device_send(uint32_t device, uint16_t type, uint64_t transaction,
                          const void *payload, uint32_t bytes,
                          const void *gpa, uint32_t gpa_bytes);
    int vmbus_device_receive(uint32_t device, void *packet, uint32_t capacity,
                             uint32_t *bytes);
    int vmbus_device_share(uint32_t device, void *buffer, uint32_t pages,
                           uint32_t *handle);

#ifdef __cplusplus
}
#endif
