/* Firmware RTC anchor plus real elapsed time, independent of UI frame rate. */
static uint64_t g_utc_boot_seconds,g_utc_anchor_ms;
static uint32_t g_utc_valid;
void dihos_time_init_utc(uint64_t boot_seconds,uint32_t flags)
{
    g_utc_boot_seconds=boot_seconds;
    g_utc_anchor_ms=dihos_time_monotonic_ms();
    g_utc_valid=(flags&1u)!=0 && boot_seconds<=253402300799ull;
}
int dihos_time_utc(uint64_t *seconds)
{
    if (!seconds || !g_utc_valid) return -1;
    uint64_t now=dihos_time_monotonic_ms();
    if (now<g_utc_anchor_ms) return -1;
    uint64_t current=g_utc_boot_seconds+(now-g_utc_anchor_ms)/1000;
    if (current>253402300799ull) return -1;
    *seconds=current;return 0;
}
