/*
 * iptracking
 * asprintf.c
 *
 * Implementation of asprintf() if the OS doesn't provide one.
 *
 */

#include "iptracking.h"
#include <stdarg.h>

int
asprintf(
    char        **restrict strp,
    const char  *restrict fmt,
    ...
)
{
    va_list     argv;
    int         fmted_len;
    char        *out_str = NULL;
    
    va_start(argv, fmt);
    fmted_len = vsnprintf(NULL, 0, fmt, argv);
    va_end(argv);
    if ( fmted_len >= 0 ) {
        out_str = malloc(++fmted_len);
        if ( out_str ) {
            va_start(argv, fmt);
            fmted_len = vsnprintf(out_str, fmted_len, fmt, argv);
            if ( fmted_len >= 0 ) *strp = out_str;
            va_end(argv);
        } else {
            fmted_len = -1;
        }
    }
    return fmted_len;
}
