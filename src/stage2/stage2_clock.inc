/* Read firmware while boot services are still active; no firmware calls from
 * the kernel are needed. EFI_TIME's ABI is 16 bytes (natural alignment). */
#include "dihos_calendar.h"
typedef struct {
    uint16_t year; uint8_t month,day,hour,minute,second,pad1;
    uint32_t nanosecond; int16_t timezone; uint8_t daylight,pad2;
} dihos_efi_time;
typedef EFI_STATUS (*dihos_efi_get_time)(dihos_efi_time *,void *);
typedef struct { EFI_TABLE_HEADER header; dihos_efi_get_time get_time; } dihos_efi_runtime_clock;
static void stage2_capture_clock(EFI_SYSTEM_TABLE *st,boot_info *bi)
{
    dihos_efi_time time={0};
    dihos_efi_runtime_clock *rt=(dihos_efi_runtime_clock *)st->RuntimeServices;
    bi->boot_time_flags=0;bi->boot_utc_seconds=0;
    if (!rt || !rt->get_time || rt->get_time(&time,0)) return;
    if (dihos_calendar_utc(time.year,time.month,time.day,time.hour,time.minute,
        time.second,time.timezone,&bi->boot_utc_seconds)) return;
    bi->boot_time_flags=1u | (time.timezone==2047 ? 2u:0u);
}
