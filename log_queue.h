/*
 * iptracking
 * log_queue.h
 *
 * Event-logging queue API.
 *
 */

#ifndef __LOG_QUEUE_H__
#define __LOG_QUEUE_H__

#include "iptracking-daemon.h"
#include "logging.h"

/*!
 * @typedef log_queue_params_t
 *
 * Data structure used to communicate event record behavioral
 * options to this API.
 *
 * @field records           parameters controlling the number of event records
 * @field push_wait_seconds parameters controlling the time the API waits for
 *                          a record to be allocated
 */
typedef struct {
    struct {
        uint32_t        min, max, delta;
    } records;
    struct {
        int             min, max, delta;
        int             grow_threshold;
    } push_wait_seconds;
} log_queue_params_t;

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
    char        uid[64];        /* various sizes */
    char        log_date[28];   /* ####-##-## ##:##:##±#### */
    /* 64 is a good choice for uid because on 64-bit LP each struct is
       16 + 16 + 2 + 2 + 64 + 28 = 128 bytes, or 4KiB = 32 of them */
} log_data_t;
#if ! defined __GCC__ && ! defined __clang__
#pragma pack()
#endif

/*!
 * @function log_data_parse
 *
 * Fill=in the <data> record with information from <p_len> bytes in
 * character buffer <p>.  Returns true if the data could be parsed,
 * false otherwise.
 *
 * A parsable event string looks like:
 *
 *     [dst_ipaddr],[src_ipddr],[src_port],[event],[uid],[log_date]
 */
bool log_data_parse(log_data_t *data, const char *p, size_t p_len);

/*!
 * @function log_data_parse_cstr
 *
 * Convenience function that 
 */
static inline
bool log_data_parse_cstr(log_data_t *data, const char *cstr)
{
    return log_data_parse(data, cstr, strlen(cstr));
}

/*!
 * @typedef log_queue_ref
 *
 * Opaque pointer to a log_queue data structure.  All fields are
 * internal to the implementation of this API and not visible
 * directly to external code.
 */
typedef struct log_queue * log_queue_ref;

/*!
 * @function log_queue_create
 *
 * Create a new PAM event record queue with record count and
 * wait times dictated by the values in <params>.  If <params>
 * is NULL, the compiled-in defaults are used.
 *
 * Returns NULL if any error occurs, a log_queue_ref if
 * successful.
 */
log_queue_ref log_queue_create(log_queue_params_t *params);

/*!
 * @function log_queue_destroy
 *
 * Dispose of PAM event record queue *<lq>.
 */
void log_queue_destroy(log_queue_ref *lq);

/*!
 * @function log_queue_summary
 *
 * Debugging aide -- print a verbose summary of *<lq> and its
 * records to stdout.
 */
void log_queue_summary(log_queue_ref *lq);

/*!
 * @function log_queue_push
 *
 * Attempt to copy the contents of *<data> to an event record in
 * *<lq>.  If no unused records are available and the limit has not
 * been reached, a new set of records will be allocated and the
 * data stored immediately.  Otherwise, this call will block
 * until an unused record becomes available.
 *
 * Returns true if the data were successfully added to *<lq>,
 * false otherwise.
 */
bool log_queue_push(log_queue_ref *lq, log_data_t *data);

/*!
 * @function log_queue_pop
 *
 * Attempt to copy the contents of an event record in *<lq> to
 * to *<data>.  If no records are available this call will block
 * until one has been added.
 *
 * Returns true if data is successfully copied to *<data>,
 * false otherwise.
 */
bool log_queue_pop(log_queue_ref *lq, log_data_t *data);

#endif /* __LOG_QUEUE_H__ */
