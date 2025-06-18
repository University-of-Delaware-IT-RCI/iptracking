/*
 * iptracking
 * log_queue.h
 *
 * Event-logging queue API.
 *
 */

#ifndef __LOG_QUEUE_H__
#define __LOG_QUEUE_H__

#include "iptracking.h"
#include "log_data.h"
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

/*!
 * @function log_queue_interrupt_pop
 *
 * Used to interrupt the log_queue_pop() function.
 */
void log_queue_interrupt_pop(log_queue_ref *lq);

#endif /* __LOG_QUEUE_H__ */
