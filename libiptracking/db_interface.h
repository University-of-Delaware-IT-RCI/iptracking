/*
 * iptracking
 * db_interface.h
 *
 * Abstract database interface.
 *
 */
 
#ifndef __DB_INTERFACE_H__
#define __DB_INTERFACE_H__

#include "iptracking.h"
#include "log_data.h"
#include "yaml_helpers.h"

/*!
 * @function db_driver_is_available
 *
 * Returns true if the database driver named <db_driver> is compiled
 * into the program.
 */
bool db_driver_is_available(const char *db_driver);

/*!
 * @typedef db_driver_iterator_t
 *
 * Opaque pointer used to iterate over the list of database driver
 * names.  See the db_driver_enumerate_drivers() function.
 */
typedef const void * db_driver_iterator_t;

/*!
 * @function db_driver_enumerate_drivers
 *
 * A local variable of type db_driver_iterator_t should be initialized
 * to NULL.  This function can then be called repeatedly with the
 * address o that local variable as the sole argument:  non-NULL
 * C string pointers are returned for each driver.  When this function
 * returns NULL the iteration is complete.  E.g.
 *
 *     db_driver_iterator_t    iter = NULL;
 *     while ( db_driver_enumerate_drivers(&iter) ) {
 *        :
 *     }
 *      
 */
const char* db_driver_enumerate_drivers(db_driver_iterator_t *iterator);

/*!
 * @typedef db_ref
 *
 * Opaque pointer to an initialized database instance.
 */
typedef struct db_instance * db_ref;

/*!
 * @enum database options
 *
 * Options that control database capabilities.
 */
enum {
    db_options_no_pam_logging = 1 << 0,
    db_options_no_firewall = 1 << 1
};

/*!
 * @defined DB_OPTIONS_TEST
 *
 * Test if the bitmask V is set in options variable O.
 */
#define DB_OPTIONS_ISSET(O,V)    (((O) & (V)) == (V))
#define DB_OPTIONS_NOTSET(O,V)    (((O) & (V)) != (V))

/*!
 * @function db_alloc
 *
 * Allocate and initialize a new database instance.  A pointer to the YAML
 * configuration document (<config_doc>) and the node containing the database
 * configuration mapping (<database_node>) are queried for options.
 *
 * If <db_driver> is NULL, then the 'driver_name' key in the <database_node>
 * mapping is checked and its value used as the driver name.
 *
 * The requested database driver may pull additional configuration options from
 * the <database_node> mapping.  If all necessary options exist and the new
 * instance can be allocated, it is returned by this function.
 *
 * All failures yield a return value of NULL.
 */
db_ref db_alloc(const char *db_driver, yaml_document_t *config_doc, yaml_node_t *database_node, unsigned int options);

/*!
 * @function db_dealloc
 *
 * Deallocate an existing database instance.  If the database is open, it is
 * first closed.  After this function is called <the_db> is no longer valid.
 */
void db_dealloc(db_ref the_db);

/*!
 * @function db_get_last_error
 *
 * Returns a pointer to the last error message string retained by <the_db> 
 * in the course of operation.
 *
 * The caller SHOULD NOT modify or free the returned pointer.
 */
const char* db_get_last_error(db_ref the_db);

/*!
 * @function db_has_valid_configuration
 *
 * Returns true is <the_db> has a valid configuration.  This function may
 * do complex checks of the parameter values to ensure consistency, for
 * example.
 */
bool db_has_valid_configuration(db_ref the_db, const char **error_msg);

/*!
 * @functon db_summarize_to_log
 *
 * Summarize the configurational details of <the_db> using the INFO()
 * logging interface (from logging.h).
 */
void db_summarize_to_log(db_ref the_db);

/*!
 * @function db_open
 *
 * Attempt to open a connection to the database represented by the
 * <the_db> instance.  This may include preparation of database
 * queries.
 *
 * If the procedure fails and error_msg is non-NULL, then
 * *<error_msg> will be set to point to a C string containing a
 * decription of the error and false will be returned.
 *
 * If successful, true is returned.
 */
bool db_open(db_ref the_db, const char **error_msg);

/*!
 * @function db_close
 *
 * Attempt to close the connection to the database represented by
 * the <the_db> instance.  This may include teardown of database
 * queries.
 *
 * If the procedure fails and error_msg is non-NULL, then
 * *<error_msg> will be set to point to a C string containing a
 * decription of the error and false will be returned.
 *
 * If successful, true is returned.
 */
bool db_close(db_ref the_db, const char **error_msg);

/*!
 * @function db_log_one_event
 *
 * Attempt to add <the_event> to the database represented by the
 * <the_db> instance.
 *
 * If the procedure fails and error_msg is non-NULL, then
 * *<error_msg> will be set to point to a C string containing a
 * decription of the error and false will be returned.
 *
 * If successful, true is returned.
 */
bool db_log_one_event(db_ref the_db, log_data_t *the_event, const char **error_msg);

/*!
 * @typedef db_blocklist_enum_ref
 *
 * Opaque pointer to a structure representing the enumeration
 * of the firewall block list table.
 */
typedef const struct db_blocklist_enum * db_blocklist_enum_ref;

/*!
 * @function db_blocklist_enum_open
 *
 * Query the firewall block list table and return an enumeration context
 * for the results.  If the query fails or no results were produced, then
 * NULL will be returned.
 *
 * The caller is ultimately reponsible for releasing the returned
 * enumeration context using the db_blocklist_enum_close() function.
 */
db_blocklist_enum_ref db_blocklist_enum_open(db_ref the_db, const char **error_msg);

/*!
 * @function db_blocklist_enum_next
 *
 * Return the next result from a firewall block list query.  If no more
 * results exist, NULL is returned.
 */
const char* db_blocklist_enum_next(db_blocklist_enum_ref the_enum);

/*!
 * @function db_blocklist_enum_close
 *
 * Release a firewall block list query enumeration context.
 */
void db_blocklist_enum_close(db_blocklist_enum_ref the_enum);

/*!
 * @typedef db_blocklist_async_notification
 *
 * Type of a function that receives asynchronous notification of
 * modifications to the firewall block list.  The callback receives
 * an ip_entity enumerator ready to be iterated for the list of
 * blocked subnets/addresses.  If the block list is empty then
 * <eblocklist> will be NULL -- but the callback is still invoked
 * so that it can ostensibly remove previously-blocked contents.
 *
 * The context argument is set when the callback is registered.
 */
typedef void (*db_blocklist_async_notification)(db_blocklist_enum_ref eblocklist, const void *context);

/*!
 * @function db_has_blocklist_async_notification
 *
 * Returns true if the database driver supports asynchronous notifications
 * of data changes.
 */
bool db_has_blocklist_async_notification(db_ref the_db, const char **error_msg);

/*!
 * @function db_blocklist_async_notification_register
 *
 * Register a callback function to handle asynchronous notifications
 * of data changes.
 *
 * The <context> is used to pass arbitrary data to the callback
 * function.
 *
 * Pass NULL for <the_notify> to unregister a previously-registered
 * callback.
 */
bool db_blocklist_async_notification_register(db_ref the_db, db_blocklist_async_notification the_notify, const void *context, const char **error_msg);

#endif /* __DB_INTERFACE_H__ */
