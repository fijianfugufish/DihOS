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

#ifdef __cplusplus
}
#endif
