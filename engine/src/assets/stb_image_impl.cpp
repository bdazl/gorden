// Single translation unit that compiles stb_image's implementation.
// Every other consumer of <stb_image.h> in the engine includes it for
// declarations only; the symbols are linked from this object.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_FAILURE_REASON // optional; keep failure_reason() string
#include <stb_image.h>
