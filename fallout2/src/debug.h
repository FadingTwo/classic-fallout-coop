#ifndef DEBUG_H
#define DEBUG_H

#include <stddef.h>

namespace fallout {

typedef int(DebugPrintProc)(char* string);

void _GNW_debug_init();
void _debug_register_mono();
void _debug_register_log(const char* fileName, const char* mode);
void _debug_register_screen();
void _debug_register_env();
void _debug_register_func(DebugPrintProc* proc);
int debugPrint(const char* format, ...);
void _debug_exit(void);

// CE: the most recent debug output (for co-op bug reports): as two parts
// of a ring buffer, or copied into `buffer` (NUL-terminated).
void debug_get_recent_parts(const char** first, size_t* firstLength, const char** second, size_t* secondLength);
size_t debug_get_recent(char* buffer, size_t size);

} // namespace fallout

#endif /* DEBUG_H */
