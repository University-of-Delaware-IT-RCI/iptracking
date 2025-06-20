/*
 * iptracking
 * log_data.h
 *
 * Data associated with an event.
 *
 */

#ifndef __LOG_DATA_H__
#define __LOG_DATA_H__

#include "iptracking.h"

/*!
 * @enum log_event
 *
 * The PAM event types to which the daemon responds.
 *
 * @constant log_event_unknown          any unclassifiable event
 * @constant log_event_auth             an "auth" event
 * @constant log_event_open_session     an "auth" event
 * @constant log_event_close_session    an "auth" event
 */
typedef enum log_event {
    log_event_unknown = 0,
    log_event_auth,
    log_event_open_session,
    log_event_close_session,
    log_event_max
} log_event_t;

/*!
 * @function log_event_to_str
 *
 * Return a C string representation of the <event> or NULL if the
 * <event> is not valid.
 */
static inline
const char* log_event_to_str(
    log_event_t     event
)
{
    switch ( event ) {
        case log_event_unknown: return "unknown";
        case log_event_auth: return "auth";
        case log_event_open_session: return "open_session";
        case log_event_close_session: return "close_session";
        default: return NULL;
    }
    return NULL;
}

/*!
 * @function log_event_parse_str
 *
 * Parse a C-string representation of an event (in <event_str>) and
 * return the proper value from the log_event enumeration, or
 * log_event_unknown otherwise.
 */
static inline
log_event_t log_event_parse_str(
    const char  *event_str
)
{
    if ( strcmp(event_str, "auth") == 0 ) return log_event_auth;
    if ( strcmp(event_str, "open_session") == 0 ) return log_event_open_session;
    if ( strcmp(event_str, "close_session") == 0 ) return log_event_close_session;
    return log_event_unknown;
}

/*!
 * @typedef log_data_t
 *
 * Data structure that holds event information.  Forced to be
 * 128 bytes in size.
 *
 * @field dst_ipaddr    IPv4 address of the server
 * @field src_ipaddr    IPv4 address of the client
 * @field src_port      TCP/IP port from which the client connected
 * @field event         The PAM event id
 * @field uid           The user identifier used for the connection
 * @field log_date      The timestamp of the connection:
 *                          YYYY-MM-DD HH:MM:SS
 */
#if defined __GCC__ || defined __clang__
typedef struct __attribute__((packed)) log_data {
#else
#pragma pack(1)
typedef struct log_data {
#endif
    char        dst_ipaddr[16]; /* ###.###.###.### */
    char        src_ipaddr[16]; /* ###.###.###.### */
    uint16_t    src_port;
    uint16_t    event;          /* event id from log_event */
    int32_t     sshd_pid;       /* pid of the sshd */
    char        uid[60];        /* various sizes */
    char        log_date[28];   /* ####-##-## ##:##:##±#### */
    /* 64 is a good choice for uid because on 64-bit LP each struct is
       16 + 16 + 2 + 2 + 64 + 28 = 128 bytes, or 4KiB = 32 of them */
} log_data_t;
#if ! defined __GCC__ && ! defined __clang__
#pragma pack()
#endif

/*!
 * @function log_data_is_valid
 *
 * Checks the content of <data> to ensure all fields are properly
 * filled-in.  Returns true if so, false otherwise.
 */
bool log_data_is_valid(log_data_t *data);

/*!
 * @function log_data_parse
 *
 * Fill=in the <data> record with information from <p_len> bytes in
 * character buffer <p>.  Returns true if the data could be parsed,
 * false otherwise.
 *
 * If parsing is successful and <endptr> is not NULL, *<endptr> is
 * set to the character position directly following the parsed data.
 *
 * A parsable event string looks like:
 *
 *     [dst_ipaddr],[src_ipddr],[src_port],[event],[sshd_pid],[uid],[log_date]
 */
bool log_data_parse(log_data_t *data, const char *p, size_t p_len, const char  **endptr);

/*!
 * @function log_data_parse_cstr
 *
 * Convenience function that 
 */
static inline
bool log_data_parse_cstr(log_data_t *data, const char *cstr, const char  **endptr)
{
    return log_data_parse(data, cstr, strlen(cstr), endptr);
}

#endif /* __LOG_DATA_H__ */
