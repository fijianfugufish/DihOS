/* Firmware RTC anchor plus real elapsed time, independent of UI frame rate. */
static uint64_t g_utc_boot_seconds,g_utc_anchor_ms;
static uint32_t g_utc_valid;
void dihos_time_init_utc(uint64_t boot_seconds,uint32_t flags)
{
    g_utc_boot_seconds=boot_seconds;
    g_utc_anchor_ms=dihos_time_monotonic_ms();
    g_utc_valid=(flags&1u)!=0 && boot_seconds<=253402300799ull;
}
int dihos_time_utc_ms(uint64_t *milliseconds)
{
    if (!milliseconds || !g_utc_valid) return -1;
    uint64_t now=dihos_time_monotonic_ms();
    if (now<g_utc_anchor_ms) return -1;
    uint64_t elapsed=now-g_utc_anchor_ms;
    uint64_t base=g_utc_boot_seconds*1000;
    if (elapsed>253402300799999ull-base) return -1;
    *milliseconds=base+elapsed;return 0;
}
int dihos_time_utc(uint64_t *seconds)
{
    uint64_t ms;if(!seconds || dihos_time_utc_ms(&ms))return -1;
    *seconds=ms/1000;return 0;
}
