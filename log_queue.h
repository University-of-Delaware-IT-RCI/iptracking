

#ifndef __LOG_QUEUE_H__
#define __LOG_QUEUE_H__


#include <stdint.h>
#include <stdbool.h>

//

#include "logging.h"

//

typedef struct {
    struct {
        uint32_t        min, max, delta;
    } records;
    struct {
        int             min, max, delta;
        int             grow_threshold;
    } push_wait_seconds;
} log_queue_params_t;

//

typedef enum log_event {
    log_event_unknown = 0,
    log_event_auth,
    log_event_session_start,
    log_event_session_end,
    log_event_max
} log_event_t;

//

const char* log_event_str(log_event_t event);

//

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

//

/*
 *  Message format:
 *
 *      [dst_ipaddr],[src_ipddr],[src_port],[event],[uid],[log_date]
 *
 */
bool log_data_parse(log_data_t *data, const char *p, size_t p_len);
bool log_data_parse_cstr(log_data_t *data, const char *cstr);

//

typedef struct log_queue * log_queue_ref;

//

log_queue_ref log_queue_create(log_queue_params_t *params);
void log_queue_destroy(log_queue_ref *lq);
void log_queue_summary(log_queue_ref *lq);
bool log_queue_push(log_queue_ref *lq, log_data_t *data);
bool log_queue_pop(log_queue_ref *lq, log_data_t *data);

#endif
