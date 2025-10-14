/* third_party/fnv/longlong.h
 * Minimal static detection for modern 64-bit compilers.
 * Upstream generates this; we vendor a fixed variant.
 */
#ifndef __LONGLONG_H__
#define __LONGLONG_H__

#include <limits.h>

/* Define one (or more) of these if the corresponding type is 64-bit. */
#if defined(ULLONG_MAX) && ULLONG_MAX == 0xffffffffffffffffULL
#  define HAVE_64BIT_LONG_LONG 1
#endif

#if defined(ULONG_MAX) && ULONG_MAX == 0xffffffffffffffffULL
#  define HAVE_64BIT_LONG 1
#endif

#if defined(UINT_MAX) && UINT_MAX == 0xffffffffffffffffULL
#  define HAVE_64BIT_INT 1
#endif

#endif /* __LONGLONG_H__ */
