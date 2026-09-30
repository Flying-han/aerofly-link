/*
 * Compile the vendored Nuklear core and its Win32/GDI backend once.
 * Feature macros must match those in gui.c whenever nuklear.h is included.
 */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_GDI_IMPLEMENTATION

#include "../vendor/nuklear/nuklear.h"
#include "../vendor/nuklear/nuklear_gdi.h"
