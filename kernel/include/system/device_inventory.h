#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DEVICE_INVENTORY_NAME_CAP 32u
#define DEVICE_INVENTORY_STATUS_CAP 32u
#define DEVICE_INVENTORY_DETAIL_CAP 128u

typedef struct device_inventory_row
{
    char group[DEVICE_INVENTORY_NAME_CAP];
    char name[DEVICE_INVENTORY_NAME_CAP];
    char status[DEVICE_INVENTORY_STATUS_CAP];
    char detail[DEVICE_INVENTORY_DETAIL_CAP];
} device_inventory_row;

uint32_t device_inventory_snapshot(device_inventory_row *out_rows, uint32_t max_rows);

#ifdef __cplusplus
}
#endif
