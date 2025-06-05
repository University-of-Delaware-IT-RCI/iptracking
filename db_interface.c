/*
 * iptracking
 * db_interface.c
 *
 * Abstract database interface.
 *
 */

#include "db_interface.h"

//

typedef struct db_instance* (*db_driver_alloc)(yaml_document_t *config_doc, yaml_node_t *database_node);
typedef void (*db_driver_dealloc)(struct db_instance *the_db);
typedef bool (*db_driver_has_valid_configuration)(struct db_instance *the_db, const char **error_msg);
typedef void (*db_driver_summarize_to_log)(struct db_instance *the_db);
typedef bool (*db_driver_open)(struct db_instance *the_db, const char **error_msg);
typedef bool (*db_driver_close)(struct db_instance *the_db, const char **error_msg);
typedef bool (*db_driver_log_one_event)(struct db_instance *the_db, log_data_t *the_event, const char **error_msg);

typedef struct {
    const char                          *driver_name;
    
    db_driver_alloc                     alloc;
    db_driver_dealloc                   dealloc;
    
    db_driver_has_valid_configuration   has_valid_configuration;
    
    db_driver_summarize_to_log          summarize_to_log;
    
    db_driver_open                      open;
    db_driver_close                     close;
    db_driver_log_one_event             log_one_event;
} db_driver_callbacks_t;

//

typedef struct db_instance {
    db_driver_callbacks_t   *driver_callbacks;
} db_instance_t;

//

static db_instance_t*
__db_instance_alloc(
    db_driver_callbacks_t *driver_callbacks,
    size_t              actual_size
)
{
    db_instance_t       *new_db_instance = NULL;
    
    if ( driver_callbacks ) {
        new_db_instance = (db_instance_t*)malloc(actual_size);
        if ( new_db_instance ) {
            memset(new_db_instance, 0, actual_size);
            new_db_instance->driver_callbacks = driver_callbacks;
        }
    }
    return new_db_instance;
}

//

#include "db_drivers/db_instance_csvfile.c"

#ifdef HAVE_POSTGRESQL
#   include "db_drivers/db_instance_postgresql.c"
#endif

#ifdef HAVE_SQLITE3
#   include "db_drivers/db_instance_sqlite3.c"
#endif

#ifdef HAVE_MYSQL
#   include "db_drivers/db_instance_mysql.c"
#endif

static db_driver_callbacks_t* __db_drivers[] = {
        &db_driver_csvfile_callbacks,
#ifdef HAVE_POSTGRESQL
        &db_driver_postgresql_callbacks,
#endif
#ifdef HAVE_SQLITE3
        &db_driver_sqlite3_callbacks,
#endif
        NULL
    };

//

static inline db_driver_callbacks_t*
__db_driver_lookup(
    const char *db_driver
)
{
    db_driver_callbacks_t**   drivers = __db_drivers;
    
    while ( *drivers ) {
        if ( strcasecmp(db_driver, (*drivers)->driver_name) == 0 ) break;
        drivers++;
    }
    return (*drivers);
}

//

bool
db_driver_is_available(
    const char  *db_driver
)
{
    return (__db_driver_lookup(db_driver) != NULL);
}

//

const char*
db_driver_enumerate_drivers(
    db_driver_iterator_t    *iterator
)
{
    if ( iterator ) {
        db_driver_callbacks_t   **current;
        
        current = (*iterator) ? (db_driver_callbacks_t**)*iterator : __db_drivers;
        if ( *current ) {
            const char  *driver_name = (*current)->driver_name;
            *iterator = ++current;
            return driver_name;
        }
    }
    return NULL;
}

//

db_ref
db_alloc(
    const char      *db_driver,
    yaml_document_t *config_doc,
    yaml_node_t     *database_node
)
{
    db_driver_callbacks_t   *driver_callbacks = NULL;
    
    if ( ! db_driver ) {
        yaml_node_t         *prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "driver-name");
        
        if ( ! prop_node ) {
            FATAL("Database: no 'driver-name' present in configuration");
            return NULL;
        }
        db_driver = yaml_helper_get_scalar_value(prop_node);
    }
    
    driver_callbacks = __db_driver_lookup(db_driver);
    
    return (driver_callbacks) ? driver_callbacks->alloc(config_doc, database_node) : NULL;
}

//

void
db_dealloc(
    db_ref  the_db
)
{
    if ( the_db ) {
        the_db->driver_callbacks->close(the_db, NULL);
        the_db->driver_callbacks->dealloc(the_db);
    }
}

//

bool
db_has_valid_configuration(
    db_ref      the_db,
    const char  **error_msg
)
{
    if ( the_db) return the_db->driver_callbacks->has_valid_configuration(the_db, error_msg);
    
    if ( error_msg ) *error_msg = "Invalid database (NULL)";
    return false;
}

//

void
db_summarize_to_log(
    db_ref  the_db
)
{
    if ( the_db ) the_db->driver_callbacks->summarize_to_log(the_db);
}

//

bool
db_open(
    db_ref      the_db,
    const char  **error_msg
)
{
    if ( the_db ) return the_db->driver_callbacks->open(the_db, error_msg);
    
    if ( error_msg ) *error_msg = "Invalid database (NULL)";
    return false;
}

//

bool
db_close(
    db_ref      the_db,
    const char  **error_msg
)
{
    if ( the_db ) return the_db->driver_callbacks->close(the_db, error_msg);
    
    if ( error_msg ) *error_msg = "Invalid database (NULL)";
    return false;
}

//

bool
db_log_one_event(
    db_ref      the_db,
    log_data_t  *the_event,
    const char  **error_msg
)
{
    if ( the_db ) return the_db->driver_callbacks->log_one_event(the_db, the_event, error_msg);
    
    if ( error_msg ) *error_msg = "Invalid database (NULL)";
    return false;
}
