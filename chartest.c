/*
 * iptracking
 * chartest.c
 *
 * Simple character string sequence testing.
 *
 */

#include "chartest.h"

bool
chartest(
    chartest_sequence_t *the_seq,
    const char          *p,
    size_t              p_len,
    const char          **endptr
)
{
    bool                okay = true;
    
    if ( p && p_len && the_seq && (the_seq->n_chunks > 0) ) {
        chartest_chunk_t    *chunks = &the_seq->chunks[0];
        int                 n_chunks = the_seq->n_chunks;
        
        while ( p_len && okay && n_chunks-- ) {
            int             n_char = chunks->n_char;
            
            while ( p_len && okay && n_char-- ) {
                if ( ! chunks->chartest_callback(*p) ) {
                    okay = false;
                } else {
                    p++, p_len--;
                }
            }
            chunks++;
        }
    }
    if ( okay && endptr ) *endptr = p;
    return okay;
}

#ifdef CHARTEST_TEST

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool
__digit_chartest_callback(int c) { return (isdigit(c) != 0) ? true : false; }
static bool
__dash_chartest_callback(int c) { return (c == '-') ? true : false; }
static bool
__colon_chartest_callback(int c) { return (c == ':') ? true : false; }
static bool
__space_chartest_callback(int c) { return (c == ' ') ? true : false; }

static chartest_sequence_t __datestr_chartest = {
            .n_chunks = 11,
            .chunks = {
                { .n_char = 4, .chartest_callback = __digit_chartest_callback },
                { .n_char = 1, .chartest_callback = __dash_chartest_callback },
                { .n_char = 2, .chartest_callback = __digit_chartest_callback },
                { .n_char = 1, .chartest_callback = __dash_chartest_callback },
                { .n_char = 2, .chartest_callback = __digit_chartest_callback },
                { .n_char = 1, .chartest_callback = __space_chartest_callback },
                { .n_char = 2, .chartest_callback = __digit_chartest_callback },
                { .n_char = 1, .chartest_callback = __colon_chartest_callback },
                { .n_char = 2, .chartest_callback = __digit_chartest_callback },
                { .n_char = 1, .chartest_callback = __colon_chartest_callback },
                { .n_char = 2, .chartest_callback = __digit_chartest_callback },
            }
        };

int
main(
    int         argc,
    const char  *argv[]
)
{
    int         argn = 1;
    
    while ( argn < argc ) {
        const char  *e;
        const char  *p = argv[argn++];
        size_t      p_len = strlen(p);
        
        while ( p_len && chartest(&__datestr_chartest, p, p_len, &e) ) {
            p_len -= (e - p);
            fputc('[', stdout);
            while ( p < e ) fputc(*p++, stdout);
            fprintf(stdout, "]\n");
        }
        printf("X  %s\n", p);
    }
    return 0;
}
    
#endif
