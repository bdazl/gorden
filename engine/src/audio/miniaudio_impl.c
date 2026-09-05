/* Single C translation unit that compiles miniaudio's implementation.
 * Every consumer of <miniaudio.h> elsewhere in the engine pulls in
 * declarations only; the symbols are linked from this object.
 *
 * Compiled as C (not C++) on purpose: miniaudio is a C library, and
 * compiling it as such keeps the engine's C++ warning set and language
 * flags out of its callback-heavy interior.
 */
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
