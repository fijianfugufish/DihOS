#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "bootinfo.h"
typedef uint64_t EFI_STATUS;
typedef struct { uint64_t signature; uint32_t revision,size,crc,reserved; } EFI_TABLE_HEADER;
typedef struct { void *RuntimeServices; } EFI_SYSTEM_TABLE;
#include "../src/stage2/stage2_clock.inc"
static uint64_t now;
uint64_t dihos_time_monotonic_ms(void) { return now; }
#include "../kernel/system/dihos_wallclock.inc"
static int fail_rtc;
static dihos_efi_time rtc={.year=2026,.month=10,.day=26,.hour=12,.timezone=2047};
static EFI_STATUS get_time(dihos_efi_time *out,void *cap)
{ assert(!cap);*out=rtc;return fail_rtc ? 1:0; }
int main(void)
{
    uint64_t utc;
    assert(!dihos_calendar_utc(1970,1,1,0,0,0,0,&utc) && utc==0);
    assert(!dihos_calendar_utc(2000,2,29,0,0,0,0,&utc) && utc==951782400ull);
    assert(!dihos_calendar_utc(2026,10,26,0,0,0,0,&utc) && utc==1792972800ull);
    assert(!dihos_calendar_utc(2100,3,1,0,0,0,0,&utc) && utc==4107542400ull);
    assert(dihos_calendar_utc(2100,2,29,0,0,0,0,&utc)<0);
    assert(dihos_calendar_utc(2026,13,1,0,0,0,0,&utc)<0);
    assert(dihos_calendar_utc(2026,1,1,24,0,0,0,&utc)<0);
    assert(dihos_calendar_utc(2026,1,1,0,0,60,0,&utc)<0);
    assert(dihos_calendar_utc(2026,1,1,0,0,0,1500,&utc)<0);
    assert(!dihos_calendar_utc(2026,10,26,0,0,0,60,&utc) && utc==1792976400ull);
    assert(!dihos_calendar_utc(2026,10,26,0,0,0,-60,&utc) && utc==1792969200ull);
    assert(!dihos_calendar_utc(2026,10,26,0,0,0,2047,&utc) && utc==1792972800ull);
    assert(dihos_calendar_utc(1970,1,1,0,0,0,-1,&utc)<0);
    dihos_efi_runtime_clock runtime={.get_time=get_time};
    EFI_SYSTEM_TABLE table={.RuntimeServices=&runtime};boot_info bi={0};
    assert(sizeof(dihos_efi_time)==16);
    stage2_capture_clock(&table,&bi);
    assert(bi.boot_time_flags==3 && bi.boot_utc_seconds==1793016000ull);
    now=5000;dihos_time_init_utc(bi.boot_utc_seconds,bi.boot_time_flags);
    now=7500;assert(!dihos_time_utc(&utc) && utc==1793016002ull);
    now=86405000;assert(!dihos_time_utc(&utc) && utc==1793102400ull);
    now=0;assert(dihos_time_utc(&utc)<0); /* counter cannot go backwards */
    dihos_time_init_utc(0,0);assert(dihos_time_utc(&utc)<0);
    fail_rtc=1;stage2_capture_clock(&table,&bi);assert(!bi.boot_time_flags);
    fail_rtc=0;rtc.day=32;stage2_capture_clock(&table,&bi);assert(!bi.boot_time_flags);
    rtc.day=26;rtc.timezone=0;stage2_capture_clock(&table,&bi);assert(bi.boot_time_flags==1);
    puts("PASS: firmware GetTime capture/ABI/failure, UTC offsets, leap years, epoch conversion, monotonic clock advancement and missing/backwards clock rejection");
    return 0;
}
