/*
 * iptracking
 * chartest.h
 *
 * Simple character string sequence testing.
 *
 */

#ifndef __CHARTEST_H__
#define __CHARTEST_H__

#include <stdlib.h>
#include <stdbool.h>

typedef bool (*chartest_callback_t)(int c);


typedef struct chartest_chunk {
    int                 n_char;
    chartest_callback_t chartest_callback;
} chartest_chunk_t;


typedef struct chartest_sequence {
    int                 n_chunks;
    chartest_chunk_t    chunks[];
} chartest_sequence_t;


bool chartest(chartest_sequence_t *the_seq, const char *p, size_t p_len, const char **endptr);


#endif /* __CHARTEST_H__ */
