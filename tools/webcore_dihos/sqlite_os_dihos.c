/* Experimental owner-thread SQLite profile. No filesystem authority or default
 * VFS is provided. Database open must fail until a real scoped VFS is supplied.
 * These callbacks initialize the empty OS layer; they do not claim working I/O.
 */
#include <sqlite3.h>
int sqlite3_os_init(void) { return SQLITE_OK; }
int sqlite3_os_end(void) { return SQLITE_OK; }
