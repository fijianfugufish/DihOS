/* Included by terminal_api.cpp and the host allocation regression test.
 * Keep the boot terminal resident; construct the other five only on demand.
 * Slots are retained for reuse, preserving the existing maximum/session APIs. */
inline void *operator new(size_t, void *storage) noexcept { return storage; }
static Terminal g_primary_terminal;
static Terminal *g_terminals[MAX_TERMINALS] = { &g_primary_terminal };
static Terminal *terminal_slot_create(uint32_t slot)
{
    if (slot >= MAX_TERMINALS) return 0;
    if (g_terminals[slot]) return g_terminals[slot];
    uint64_t pages = (sizeof(Terminal) + 4095ull) / 4096ull;
    void *storage = pmem_alloc_pages(pages);
    if (!storage) return 0;
    /* Static instances used to start zeroed; retain that constructor contract. */
    memset(storage, 0, sizeof(Terminal));
    g_terminals[slot] = new (storage) Terminal;
    return g_terminals[slot];
}
