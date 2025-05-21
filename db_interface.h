/*
 * iptracking
 * db_interface.h
 *
 * Abstract database interface.
 *
 */
 
#ifndef __DB_INTERFACE_H__
#define __DB_INTERFACE_H__

#include "iptracking-daemon.h"
#include "log_queue.h"
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
db_ref db_alloc(const char *db_driver, yaml_document_t *config_doc, yaml_node_t *database_node);

/*!
 * @function db_dealloc
 *
 * Deallocate an existing database instance.  If the database is open, it is
 * first closed.  After this function is called <the_db> is no longer valid.
 */
void db_dealloc(db_ref the_db);

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

#endif /* __DB_INTERFACE_H__ */
