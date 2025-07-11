/*
 * iptracking
 * log_data.c
 *
 * Data associated with an event.
 *
 */

#include "log_data.h"
#include "chartest.h"

//

bool
log_data_is_valid(
    log_data_t  *data
)
{
    return ( data &&
         (data->event >= log_event_unknown && data->event < log_event_max) &&
         (data->dst_ipaddr[0] && memchr(data->dst_ipaddr, 0, sizeof(data->dst_ipaddr))) &&
         (data->src_ipaddr[0] && memchr(data->src_ipaddr, 0, sizeof(data->src_ipaddr)) ) &&
         (data->uid[0] && memchr(data->uid, 0, sizeof(data->uid))) &&
         (data->log_date[0] && memchr(data->log_date, 0, sizeof(data->log_date))) );
}

//

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

bool
log_data_parse(
    log_data_t  *data,
    const char  *p,
    size_t      p_len,
    const char  **endptr
)
{
    memset(data, 0, sizeof(log_data_t));
    while ( p && p_len ) {
        /* [dst_ipaddr],[src_ipddr],[src_port],[event],[uid],[log_date] */
        const char  *e;
        uint32_t    last_val;
        
        /* Drop any leading whitespace: */
        while ( p_len && *p && isspace(*p) ) p++, p_len--;
        if ( ! p_len || ! *p ) break;
        e = p; /* p = start of the string */
        
        /* dst_ipaddr */
        while ( p_len && *e && (*e != ',') ) e++, p_len--;
        if ( p_len == 0 ) break;
        if ( (e - p) + 1 > sizeof(data->dst_ipaddr) ) break;
        memcpy(&data->dst_ipaddr[0], p, (e - p) + 1); data->dst_ipaddr[e - p] = '\0';
        p = ++e, p_len--;
        
        /* src_ipaddr */
        while ( p_len && *e && (*e != ',') ) e++, p_len--;
        if ( p_len == 0 ) break;
        if ( (e - p) + 1 > sizeof(data->src_ipaddr) ) break;
        memcpy(&data->src_ipaddr[0], p, (e - p) + 1); data->src_ipaddr[e - p] = '\0';
        p = ++e, p_len--;
        
        /* src_port */
        data->src_port = last_val = 0;
        while ( p_len && *e && isdigit(*e) ) {
            data->src_port = 10 * data->src_port + (*e - '0');
            if ( data->src_port < last_val ) {
                /* Integer overflow */
                p_len = 0;
                break;
            }
            last_val = data->src_port;
            e++, p_len--;
        }
        if ( (p_len == 0) || (*e != ',') ) break;
        p = ++e, p_len--;
        
        /* event */
        data->event = last_val = 0;
        while ( p_len && *e && isdigit(*e) ) {
            data->event = 10 * data->event + (*e - '0');
            if ( data->event < last_val ) {
                /* Integer overflow */
                p_len = 0;
                break;
            }
            last_val = data->event;
            e++, p_len--;
        }
        if ( (p_len == 0) || (*e != ',') ) break;
        p = ++e, p_len--;
        
        /* sshd_pid */
        data->sshd_pid = last_val = 0;
        while ( p_len && *e && isdigit(*e) ) {
            data->sshd_pid = 10 * data->sshd_pid + (*e - '0');
            if ( data->sshd_pid < last_val ) {
                /* Integer overflow */
                p_len = 0;
                break;
            }
            last_val = data->sshd_pid;
            e++, p_len--;
        }
        if ( (p_len == 0) || (*e != ',') ) break;
        p = ++e, p_len--;
        
        /* uid */
        while ( p_len && *e && (*e != ',') ) e++, p_len--;
        if ( p_len == 0 ) break;
        if ( (e - p) + 1 > sizeof(data->uid) ) break;
        memcpy(&data->uid[0], p, (e - p) + 1); data->uid[e - p] = '\0';
        p = ++e, p_len--;
        
        /* timestamp */
        if ( p_len == 0 ) break;
        if ( ! chartest(&__datestr_chartest, p, p_len, &e) ) break;
        if ( e - p + 1 > sizeof(data->log_date) ) break;
        memcpy(&data->log_date[0], p, (e - p)); data->log_date[e - p] = '\0';
        
        if ( endptr ) *endptr = e;
        
        return true;
    }
    return false;
}
