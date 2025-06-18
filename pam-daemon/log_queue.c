/*
 * iptracking
 * log_queue.c
 *
 * Event-logging queue API.
 *
 */

#include "log_queue.h"
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

static log_queue_params_t __log_queue_default_params = {
        .records = {
            .min = LOG_POOL_RECORDS_MIN,
            .max = LOG_POOL_RECORDS_MAX,
            .delta = LOG_POOL_RECORDS_DELTA
        },
        .push_wait_seconds = {
            .min = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN,
            .max = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX,
            .delta = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT,
            .grow_threshold = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH
        }
    };

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

//

typedef struct log_record {
    struct log_record   *link;  /* for linking in the avail vs. used queues */
    log_data_t          data;
} log_record_t;

//

static log_record_t*
__log_record_create(
    uint32_t    n_records
)
{
    return (log_record_t*)calloc(n_records, sizeof(log_record_t));
}

//

typedef struct log_queue {
    log_queue_params_t      params;
    pthread_mutex_t         lock;
    pthread_cond_t          data_ready;
    
    uint32_t                n_rec_free, n_rec_used;
    log_record_t            *free_head, *used_head, *used_tail;
    
    uint32_t                n_rec_pools;
    log_record_t*           rec_pools[];
} log_queue_t;
  
//

static log_queue_t*
__log_queue_add_pool(
    log_queue_t     *lq,
    uint32_t        n_records
)
{
    log_record_t        *next_pool = __log_record_create(n_records);
    log_queue_t         *new_queue = NULL;
    
    if ( next_pool ) {
        /* Grow the queue object: */
        size_t          new_nbytes = sizeof(log_queue_t) + 
                                        (1 + lq->n_rec_pools) * sizeof(log_record_t*);
        new_queue = (log_queue_t*)realloc(lq, new_nbytes);
        if ( new_queue ) {
            /* Add the pool to the list */
            new_queue->rec_pools[new_queue->n_rec_pools++] = next_pool;
            
            /* Adjust the free count: */
            new_queue->n_rec_free += n_records;
            
            /* Add records from the pool to the free queue: */
            while ( n_records-- ) {
                next_pool->link = new_queue->free_head;
                new_queue->free_head = next_pool;
                next_pool++;
            }
        } else {
            free((void*)next_pool);
        }
    }
    return new_queue;
}

//

static log_record_t*
__log_queue_alloc_record(
    log_queue_t     **lq,
    bool            *at_limit
)
{
    log_queue_t     *LQ = *lq;
    log_record_t    *new_rec = NULL, *lrp;
    
    *at_limit = false;
    if ( LQ->n_rec_free == 0 ) {
        uint32_t    n_records;
        
        if ( LQ->n_rec_pools == 0 ) {
            n_records = LQ->params.records.min;
        } else {
            n_records = (LQ->n_rec_free + LQ->n_rec_used);
            if ( LQ->params.records.max ) {
                n_records = LQ->params.records.max - n_records;
            } else {
                n_records = UINT32_MAX - n_records;
            }
            if ( n_records == 0 ) {
                *at_limit = true;
                return NULL;
            }
            if ( n_records > LQ->params.records.delta ) n_records = LQ->params.records.delta;
        }
        LQ = __log_queue_add_pool(LQ, n_records);
        if ( ! LQ ) return NULL;
        *lq = LQ;
    }
        
    /* Remove from the free queue: */
    new_rec = LQ->free_head;
    LQ->free_head = new_rec->link;
    LQ->n_rec_free--;
    
    /* Add at the tail end of the queue: */
    new_rec->link = NULL;
    if ( LQ->used_tail ) {
        LQ->used_tail->link = new_rec;
        LQ->used_tail = new_rec;
    } else {
        LQ->used_head = LQ->used_tail = new_rec;
    }
    LQ->n_rec_used++;
    return new_rec;
}

//

void
static __log_queue_dealloc_record(
    log_queue_t     **lq,
    log_record_t    *old_rec
)
{
    log_record_t    *last_rec = NULL, *cur_rec = (*lq)->used_head;
    
    /* Find the record in the used list: */
    while ( cur_rec && (cur_rec != old_rec) ) {
        last_rec = cur_rec;
        cur_rec = cur_rec->link;
    }
    if ( cur_rec ) {
        /* We found it!  Remove from the used chain: */
        if ( last_rec ) last_rec->link = cur_rec->link;
        
        /* Is this record the tail of the used queue? */
        if ( cur_rec == (*lq)->used_tail ) (*lq)->used_tail = last_rec;
        
        /* Is this record the head of the used queue? */
        if ( cur_rec == (*lq)->used_head ) (*lq)->used_head = cur_rec->link;
        
        /* Prepend to the free chain: */
        cur_rec->link = (*lq)->free_head;
        (*lq)->free_head = cur_rec;
        (*lq)->n_rec_free++, (*lq)->n_rec_used--;
    }
}

//

log_queue_ref
log_queue_create(
    log_queue_params_t  *params
)
{
    log_queue_t         *new_lq = (log_queue_t*)malloc(sizeof(log_queue_t));
    
    if ( new_lq ) {
        memset(new_lq, 0, sizeof(log_queue_t));
        if ( params ) {
            new_lq->params = *params;
        } else {
            new_lq->params = __log_queue_default_params;
        }
        pthread_mutex_init(&new_lq->lock, NULL);
        pthread_cond_init(&new_lq->data_ready, NULL);
    }
    return new_lq;
}

//

void
log_queue_destroy(
    log_queue_ref   *lq
)
{
    if ( lq && *lq ) {
        /* Destroy all pools: */
        while ( (*lq)->n_rec_pools > 0 ) free((void*)((*lq)->rec_pools[--(*lq)->n_rec_pools]));
        
        /* Destroy the lock et al.: */
        pthread_mutex_destroy(&(*lq)->lock);
        pthread_cond_destroy(&(*lq)->data_ready);
        
        /* Destroy the queue: */
        free((void*)*lq);
        *lq = NULL;
    }
}

//

void
log_queue_summary(
    log_queue_ref   *lq
)
{
    log_record_t    *lrp;
    
    pthread_mutex_lock(&(*lq)->lock);
    printf( "log_queue@%p {\n"
            "    n_rec = %lu / (%lu ≤ %lu ≤ %lu)\n"
            "    n_rec_pools = %lu\n"
            "    records = {\n",
            *lq,
            (unsigned long)(*lq)->n_rec_used,
            (unsigned long)(*lq)->params.records.min,
            (unsigned long)((*lq)->n_rec_free + (*lq)->n_rec_used),
            (unsigned long)(*lq)->params.records.max,
            (unsigned long)(*lq)->n_rec_pools);
    
    lrp = (*lq)->used_head;
    while ( lrp ) {
        printf("        [%s] %-15s <= %15s:%hu (%s)\n",
            lrp->data.log_date,
            lrp->data.dst_ipaddr,
            lrp->data.src_ipaddr,
            lrp->data.src_port,
            lrp->data.uid);
        lrp = lrp->link;
    }
    printf("}\n");
    pthread_mutex_unlock(&(*lq)->lock);
}

//

bool
log_queue_push(
    log_queue_ref   *lq,
    log_data_t      *data
)
{
    log_record_t    *new_record = NULL;
    bool            rc = false, at_limit;
    int             wait_sec = (*lq)->params.push_wait_seconds.min;
    int             n_waits = 1;
    
    pthread_mutex_lock(&(*lq)->lock);
    while ( ! rc ) {
        new_record = __log_queue_alloc_record(lq, &at_limit);
        if ( new_record ) {
            memcpy(&new_record->data, data, sizeof(log_data_t));
            rc = true;
        } else if ( at_limit ) {
            pthread_mutex_unlock(&(*lq)->lock);
            WARN("log_queue_push:  max records allocated, waiting %d s for records to become free...", wait_sec);
            sleep(wait_sec);
            if ( n_waits >= (*lq)->params.push_wait_seconds.grow_threshold ) {
                wait_sec += (*lq)->params.push_wait_seconds.delta;
                if ( wait_sec > (*lq)->params.push_wait_seconds.max ) wait_sec = (*lq)->params.push_wait_seconds.max;
                n_waits = 0;
            } else {
                n_waits++;
            }
            pthread_mutex_lock(&(*lq)->lock);
        } else {
            break;
        }
    }
    if ( rc ) {
        /* Let anyone watching for data to become available wake up now... */
        pthread_cond_broadcast(&(*lq)->data_ready);
    }
    pthread_mutex_unlock(&(*lq)->lock);
    
    return rc;
}

//

bool
log_queue_pop(
    log_queue_ref   *lq,
    log_data_t      *data
)
{
    bool            rc = false;
    
    pthread_mutex_lock(&(*lq)->lock);
    if ( ! (*lq)->used_head ) {
        INFO("log_queue_pop:  waiting on data...");
        pthread_cond_wait(&(*lq)->data_ready, &(*lq)->lock);
        INFO("log_queue_pop:  ...data is ready");
    }
    if ( (*lq)->used_head ) {
        memcpy(data, &(*lq)->used_head->data, sizeof(log_data_t));
        rc = true;
        __log_queue_dealloc_record(lq, (*lq)->used_head);
    }
    pthread_mutex_unlock(&(*lq)->lock);
    return rc;
}

//

void
log_queue_interrupt_pop(
    log_queue_ref   *lq
)
{
    pthread_mutex_lock(&(*lq)->lock);
    pthread_cond_broadcast(&(*lq)->data_ready);
    pthread_mutex_unlock(&(*lq)->lock);
}
