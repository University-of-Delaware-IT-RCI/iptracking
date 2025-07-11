/*
 * iptracking
 * db_interface.c
 *
 * Abstract database interface.
 *
 */

#include "db_interface.h"
#include "logging.h"

#include <stdarg.h>

//

typedef struct db_instance* (*db_driver_alloc)(yaml_document_t *config_doc, yaml_node_t *database_node);
typedef void (*db_driver_dealloc)(struct db_instance *the_db);
typedef bool (*db_driver_has_valid_configuration)(struct db_instance *the_db, const char **error_msg);
typedef void (*db_driver_summarize_to_log)(struct db_instance *the_db);
typedef bool (*db_driver_open)(struct db_instance *the_db, const char **error_msg);
typedef bool (*db_driver_close)(struct db_instance *the_db, const char **error_msg);
typedef bool (*db_driver_log_one_event)(struct db_instance *the_db, log_data_t *the_event, const char **error_msg);
typedef struct db_blocklist_enum* (*db_driver_blocklist_enum_open)(struct db_instance *the_db, const char **error_msg);
typedef bool (*db_driver_blocklist_async_notification_toggle)(struct db_instance *the_db, bool start_if_true, const char **error_msg);

typedef struct {
    const char                          *driver_name;
    
    db_driver_alloc                     alloc;
    db_driver_dealloc                   dealloc;
    
    db_driver_has_valid_configuration   has_valid_configuration;
    
    db_driver_summarize_to_log          summarize_to_log;
    
    db_driver_open                      open;
    db_driver_close                     close;
    db_driver_log_one_event             log_one_event;
    db_driver_blocklist_enum_open       blocklist_enum_open;
    
    db_driver_blocklist_async_notification_toggle   blocklist_async_notification_toggle;
} db_driver_callbacks_t;

//

typedef struct db_instance {
    db_driver_callbacks_t           *driver_callbacks;
    unsigned int                    options;
    
    const char                      *last_error;
    
    bool                            blocklist_async_notification_is_running;
    pthread_mutex_t                 blocklist_async_notification_lock;
    db_blocklist_async_notification blocklist_async_notification_callback;
    const void                      *blocklist_async_notification_context;
} db_instance_t;

//

const char*
__db_instance_set_last_error(
    db_instance_t   *the_db,
    const char      *error_str,
    int             error_str_len
)
{
    if ( the_db ) {
        if ( the_db->last_error ) {
            free((void*)the_db->last_error);
            the_db->last_error = NULL;
        }
        if ( error_str_len < 0 ) {
            const char      *p_end;
            
            // Drop leading whitespace:
            while ( *error_str && isspace(*error_str) ) error_str++;
            
            // Get to end of string:
            p_end = error_str;
            while ( *p_end ) p_end++;
            
            // Drop trailing whitespace:
            while ( p_end - 1 > error_str ) {
                if ( ! isspace(*(p_end - 1)) ) break;
                p_end--;
            }
            
            // Calculate the length:
            error_str_len = p_end - error_str;
        }
        the_db->last_error = (const char*)strndup(error_str, error_str_len);
        return the_db->last_error;
    }
    return NULL;
}

//

const char*
__db_instance_printf_last_error(
    db_instance_t   *the_db,
    const char      *format,
    ...
)
{
    if ( the_db ) {
        va_list             argv;
        
        if ( the_db->last_error ) {
            free((void*)the_db->last_error);
            the_db->last_error = NULL;
        }
        
        va_start(argv, format);
        asprintf((char**)&the_db->last_error, format, argv);
        va_end(argv);
        return the_db->last_error;
    }
    return NULL;
}

//

static db_instance_t*
__db_instance_alloc(
    db_driver_callbacks_t   *driver_callbacks,
    size_t                  actual_size
)
{
    db_instance_t       *new_db_instance = NULL;
    
    if ( driver_callbacks ) {
        new_db_instance = (db_instance_t*)malloc(actual_size);
        if ( new_db_instance ) {
            memset(new_db_instance, 0, actual_size);
            new_db_instance->driver_callbacks = driver_callbacks;
            
            if ( driver_callbacks->blocklist_async_notification_toggle ) {
                pthread_mutex_init(&new_db_instance->blocklist_async_notification_lock, NULL);
            }
        }
    }
    return new_db_instance;
}

//

typedef const char* (*db_driver_blocklist_enum_next)(db_blocklist_enum_ref the_enum);
typedef void (*db_driver_blocklist_enum_close)(db_blocklist_enum_ref the_enum);
typedef struct db_blocklist_enum {
    db_ref                          parent_db;
    
    db_driver_blocklist_enum_next   next;
    db_driver_blocklist_enum_close  close;
} db_blocklist_enum_t;

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
#ifdef HAVE_MYSQL
        &db_driver_mysql_callbacks,
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
    yaml_node_t     *database_node,
    unsigned int    options
)
{
    db_driver_callbacks_t   *driver_callbacks = NULL;
    db_ref                  new_db = NULL;
    
    if ( ! db_driver ) {
        yaml_node_t         *prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "driver-name");
        
        if ( ! prop_node ) {
            FATAL("Database: no 'driver-name' present in configuration");
            return NULL;
        }
        db_driver = yaml_helper_get_scalar_value(prop_node);
    }
    
    if ( (driver_callbacks = __db_driver_lookup(db_driver)) != NULL ) {
        new_db = driver_callbacks->alloc(config_doc, database_node);
        if ( new_db ) new_db->options = options;
    }
    return new_db;
}

//

void
db_dealloc(
    db_ref  the_db
)
{
    if ( the_db ) {
        the_db->driver_callbacks->close(the_db, NULL);

        if ( the_db->driver_callbacks->blocklist_async_notification_toggle ) {
            pthread_mutex_destroy(&the_db->blocklist_async_notification_lock);
        }

        the_db->driver_callbacks->dealloc(the_db);
        
        if ( the_db->last_error ) free((void*)the_db->last_error);
        free((void*)the_db);
    }
}

//

const char*
db_get_last_error(
    db_ref  the_db
)
{
    return the_db ? the_db->last_error : NULL;
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
    db_ref          the_db,
    const char      **error_msg
)
{
    if ( the_db ) {
        return the_db->driver_callbacks->open(the_db, error_msg);
    }
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
    if ( the_db ) {
        if ( DB_OPTIONS_NOTSET(the_db->options, db_options_no_pam_logging) ) {
            return the_db->driver_callbacks->log_one_event(the_db, the_event, error_msg);
        }
        if ( error_msg ) *error_msg = "PAM functions not enabled on database";
    }
    if ( error_msg ) *error_msg = "Invalid database (NULL)";
    return false;
}

//

db_blocklist_enum_ref
db_blocklist_enum_open(
    db_ref      the_db,
    const char  **error_msg
)
{
    db_blocklist_enum_t     *new_enum = NULL;
    
    if ( the_db ) {
        if ( DB_OPTIONS_NOTSET(the_db->options, db_options_no_firewall) ) {
            if ( the_db->driver_callbacks->blocklist_enum_open ) {
                new_enum =  the_db->driver_callbacks->blocklist_enum_open(the_db, error_msg);
                if ( new_enum ) {
                    new_enum->parent_db = the_db;
                } else if ( error_msg ) {
                    *error_msg = "Failed to allocate enumerator";
                }
            } else if ( error_msg ) {
                *error_msg = "No enumerator open callback";
            }
        } else if ( error_msg ) {
            *error_msg = "Firewall functionality not enabled";
        }
    } else if ( error_msg ) {
        *error_msg = "Invalid database (NULL)";
    }
    return (db_blocklist_enum_ref)new_enum;
}

//

const char*
db_blocklist_enum_next(
    db_blocklist_enum_ref   the_enum
)
{
    if ( the_enum ) return the_enum->next(the_enum);
    return NULL;
}

//

void
db_blocklist_enum_close(
    db_blocklist_enum_ref   the_enum
)
{
    if ( the_enum ) the_enum->close(the_enum);
}

//

bool
db_has_blocklist_async_notification(
    db_ref      the_db,
    const char  **error_msg
)
{
    if ( the_db ) {
        if ( DB_OPTIONS_NOTSET(the_db->options, db_options_no_firewall) ) {
            if ( the_db->driver_callbacks->blocklist_async_notification_toggle ) {
                return true;
            } else if ( error_msg ) {
                *error_msg = "No async notification callback";
            }
        } else if ( error_msg ) {
            *error_msg = "Firewall functionality not enabled";
        }
    } else if ( error_msg ) {
        *error_msg = "Invalid database (NULL)";
    }
    return false;
}

//

bool
db_blocklist_async_notification_register(
    db_ref                          the_db,
    db_blocklist_async_notification the_notify,
    const void                      *context,
    const char                      **error_msg
)
{
    bool                            out_result = false;
    
    if ( the_db ) {
        if ( DB_OPTIONS_NOTSET(the_db->options, db_options_no_firewall) ) {
            if ( the_db->driver_callbacks->blocklist_async_notification_toggle ) {
                bool                        need_start;
                
                pthread_mutex_lock(&the_db->blocklist_async_notification_lock);
                need_start = (the_db->blocklist_async_notification_callback == NULL);
                if ( (the_db->blocklist_async_notification_callback = the_notify) == NULL ) {
                    the_db->blocklist_async_notification_context = NULL;
                    out_result = the_db->driver_callbacks->blocklist_async_notification_toggle(the_db, false, error_msg);
                } else {
                    the_db->blocklist_async_notification_context = context;
                    out_result = need_start ? the_db->driver_callbacks->blocklist_async_notification_toggle(the_db, true,  error_msg) : true;
                }
                pthread_mutex_unlock(&the_db->blocklist_async_notification_lock);
            } else if ( error_msg ) {
                *error_msg = "No async notification callback";
            }
        } else if ( error_msg ) {
            *error_msg = "Firewall functionality not enabled";
        }
    } else if ( error_msg ) {
        *error_msg = "Invalid database (NULL)";
    }
    return out_result;
}
