/* Single C translation unit that compiles miniaudio's implementation.
 * Every consumer of <miniaudio.h> elsewhere in the engine pulls in
 * declarations only; the symbols are linked from this object.
 *
 * Compiled as C (not C++) on purpose: miniaudio is a C library, and
 * keeping the implementation out of any C++ TU dodges -fno-exceptions
 * subtleties around its callback-heavy interior.
 */
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
