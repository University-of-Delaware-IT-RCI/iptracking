/*
 * iptracking
 * db_instance_mysql.c
 *
 * mysql database driver.
 *
 */

#include <mysql.h>
#include <mysql_version.h>

//

#define DB_INSTANCE_MYSQL_LOG_STMT_NPARAMS 7
#define DB_INSTANCE_MYSQL_LOG_STMT_QUERY_STR "CALL  iptracking.log_one_event(?, ?, ?, ?, ?, ?, ?);"
#define DB_INSTANCE_MYSQL_BLOCKLIST_STMT_QUERY_STR "SELECT ip_entity FROM block_now"

static const char   *db_mysql_log_stmt_query_str = DB_INSTANCE_MYSQL_LOG_STMT_QUERY_STR;
static const int    db_mysql_log_stmt_nparams = DB_INSTANCE_MYSQL_LOG_STMT_NPARAMS;
static const char   *db_mysql_blocklist_stmt_query_str = DB_INSTANCE_MYSQL_BLOCKLIST_STMT_QUERY_STR;

//

typedef struct {
    db_instance_t       base;
    //
    const char          *host;
    const char          *user;
    const char          *passwd;
    const char          *db;
    unsigned int        port;
    const char          *unix_socket;
    //
    bool                is_connected;
    MYSQL               db_handle;
    //
    MYSQL_STMT          *log_statement;
} db_instance_mysql_t;

//

static db_instance_t* __db_instance_mysql_alloc(yaml_document_t *config_doc, yaml_node_t *database_node);
static void __db_instance_mysql_dealloc(db_instance_t *the_db);
static bool __db_instance_mysql_has_valid_configuration(db_instance_t *the_db, const char **error_msg);
static void __db_instance_mysql_summarize_to_log(db_instance_t *the_db);
static bool __db_instance_mysql_open(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_mysql_close(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_mysql_log_one_event(db_instance_t *the_db, log_data_t *the_event, const char **error_msg);
static struct db_blocklist_enum* __db_instance_mysql_blocklist_enum_open(db_instance_t *the_db, const char **error_msg);

//

static db_driver_callbacks_t    db_driver_mysql_callbacks = {
        .driver_name = "mysql",
        
        .alloc = __db_instance_mysql_alloc,
        .dealloc = __db_instance_mysql_dealloc,
        .has_valid_configuration = __db_instance_mysql_has_valid_configuration,
        .summarize_to_log = __db_instance_mysql_summarize_to_log,
        .open = __db_instance_mysql_open,
        .close = __db_instance_mysql_close,
        .log_one_event = __db_instance_mysql_log_one_event,
        .blocklist_enum_open = __db_instance_mysql_blocklist_enum_open,
        
        .blocklist_async_notification_toggle = NULL
    };
    
//

db_instance_t*
__db_instance_mysql_alloc(
    yaml_document_t *config_doc,
    yaml_node_t     *database_node
)
{   
    db_instance_mysql_t         *new_instance = NULL;
    yaml_node_t                 *prop_node;
    size_t                      base_bytes = sizeof(db_instance_mysql_t);
    size_t                      extra_bytes = 0;
    const char                  *v;
    
    const char                  *host = NULL;
    const char                  *user = NULL;
    const char                  *passwd = NULL;
    const char                  *db = NULL;
    unsigned int                port = MYSQL_PORT;
    const char                  *unix_socket = NULL;
    
    /*
     * Check for any recognizable database connection properties items:
     */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "host")) ) {
        host = yaml_helper_get_scalar_value(prop_node);
        if ( host ) extra_bytes += strlen(host) + 1;
    }
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "user")) ) {
        user = yaml_helper_get_scalar_value(prop_node);
        if ( user ) extra_bytes += strlen(user) + 1;
    }
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "passwd")) ) {
        passwd = yaml_helper_get_scalar_value(prop_node);
        if ( passwd ) extra_bytes += strlen(passwd) + 1;
    }
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "db")) ) {
        db = yaml_helper_get_scalar_value(prop_node);
        if ( db ) extra_bytes += strlen(db) + 1;
    }
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "db")) ) {
        uint32_t                port_val;
        
        if ( yaml_helper_get_scalar_uint32_value(prop_node, &port_val) ) port = port_val;
    }
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "unix_socket")) ) {
        unix_socket = yaml_helper_get_scalar_value(prop_node);
        if ( unix_socket ) extra_bytes += strlen(unix_socket) + 1;
    }
    
    /* Ready to allocate: */
    new_instance = (db_instance_mysql_t*)__db_instance_alloc(
                            &db_driver_mysql_callbacks, base_bytes + extra_bytes);
    if ( new_instance ) {
        void        *p = (void*)new_instance + base_bytes;
        char        *e;
        
#define DB_INSTANCE_mysql_P_INC(T,N)     { size_t dp = (sizeof(T) * (N)); p += dp; extra_bytes -= dp; }
        
        /* Fill-in the connection parameters: */
        if ( host ) {
            new_instance->host = (const char*)p;
            e = stpncpy((char*)p, host, extra_bytes);
            DB_INSTANCE_mysql_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->host = NULL;
        }
        if ( user ) {
            new_instance->user = (const char*)p;
            e = stpncpy((char*)p, user, extra_bytes);
            DB_INSTANCE_mysql_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->user = NULL;
        }
        if ( passwd ) {
            new_instance->passwd = (const char*)p;
            e = stpncpy((char*)p, passwd, extra_bytes);
            DB_INSTANCE_mysql_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->passwd = NULL;
        }
        if ( db ) {
            new_instance->passwd = (const char*)p;
            e = stpncpy((char*)p, db, extra_bytes);
            DB_INSTANCE_mysql_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->db = NULL;
        }
        new_instance->port = port;
        if ( unix_socket ) {
            new_instance->unix_socket = (const char*)p;
            e = stpncpy((char*)p, unix_socket, extra_bytes);
            DB_INSTANCE_mysql_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->unix_socket = NULL;
        }
#undef DB_INSTANCE_mysql_P_INC
        
        new_instance->is_connected = false;
    }
    return (db_instance_t*)new_instance;
}

//

void
__db_instance_mysql_dealloc(
    db_instance_t   *the_db
)
{
}

//

bool
__db_instance_mysql_has_valid_configuration(
    db_ref      the_db,
    const char  **error_msg
)
{
    /* There really is no mandatory keyword needed... */
    return true;
}

//

void
__db_instance_mysql_summarize_to_log(
    db_instance_t   *the_db
)
{
    db_instance_mysql_t     *THE_DB = (db_instance_mysql_t*)the_db;
    
    INFO("Database: driver_name = %s", THE_DB->base.driver_callbacks->driver_name);
    INFO("Database: host = %s", THE_DB->host ? THE_DB->host : "<not-set>");
    INFO("Database: user = %s", THE_DB->user ? THE_DB->user : "<not-set>");
    INFO("Database: password = %s", THE_DB->passwd ? "********" : "<not-set>");
    INFO("Database: db = %s", THE_DB->user ? THE_DB->db : "<not-set>");
    INFO("Database: port = %u", THE_DB->port);
    INFO("Database: unix_socket = %s", THE_DB->unix_socket ? THE_DB->unix_socket : "<not-set>");
}

//

bool
__db_instance_mysql_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_mysql_t     *THE_DB = (db_instance_mysql_t*)the_db;
    
    if ( ! THE_DB->is_connected ) {
        int                 rc;
        
        DEBUG("Database: connecting to database");
        if ( ! mysql_init(&THE_DB->db_handle) ) {
            if ( error_msg ) *error_msg = "Unable to initialize MYSQL handle";
            return false;
        }
        if ( ! mysql_real_connect(&THE_DB->db_handle,
                        THE_DB->host,
                        THE_DB->user,
                        THE_DB->passwd,
                        THE_DB->db,
                        THE_DB->port,
                        THE_DB->unix_socket,
                        0)
        ) {
            if ( error_msg ) *error_msg = "Unable to initialize MYSQL handle";
            return false;
        }
        THE_DB->is_connected = true;
        
        if ( (THE_DB->base.options & db_options_no_pam_logging) == db_options_no_pam_logging ) {
            DEBUG("Database: connection okay"); 
        } else {
            DEBUG("Database: connection okay, preparing query");  
            
            // Allocate a prepared statment:
            THE_DB->log_statement = mysql_stmt_init(&THE_DB->db_handle);
            if ( ! THE_DB->log_statement ) {
                if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, mysql_error(&THE_DB->db_handle), -1);
                mysql_close(&THE_DB->db_handle);
                THE_DB->is_connected = false;
                return false;
            }
            
            // Prepare the statement:
            rc = mysql_stmt_prepare(THE_DB->log_statement, db_mysql_log_stmt_query_str, -1);
            if  ( rc != 0 ) {
                if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, mysql_error(&THE_DB->db_handle), -1);
                mysql_stmt_close(THE_DB->log_statement);
                THE_DB->log_statement = NULL;
                mysql_close(&THE_DB->db_handle);
                THE_DB->is_connected = false;
                return false;
            }
            
            // Check the parameter count:
            if ( mysql_stmt_param_count(THE_DB->log_statement) != db_mysql_log_stmt_nparams ) {
                if ( error_msg ) *error_msg = __db_instance_printf_last_error(the_db, "Number of query parameters in prepared statement does not match expected number of parameters");
                mysql_stmt_close(THE_DB->log_statement);
                THE_DB->log_statement = NULL;
                mysql_close(&THE_DB->db_handle);
                THE_DB->is_connected = false;
                return false;
            }
            DEBUG("Database: logging query prepared");
        }
    }
    return true;
}

//

bool
__db_instance_mysql_close(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_mysql_t    *THE_DB = (db_instance_mysql_t*)the_db;
    
    if ( THE_DB->is_connected ) {
        DEBUG("Database: closing connection");        
        if ( THE_DB->log_statement ) mysql_stmt_close(THE_DB->log_statement);
        THE_DB->log_statement = NULL;
        mysql_close(&THE_DB->db_handle);
        THE_DB->is_connected = false;
    }
    return true;
}

//

bool
__db_instance_mysql_log_one_event(
    db_instance_t   *the_db,
    log_data_t      *the_event,
    const char      **error_msg
)
{
    db_instance_mysql_t    *THE_DB = (db_instance_mysql_t*)the_db;
    
    while ( THE_DB->is_connected ) {
        MYSQL_BIND          param_values[DB_INSTANCE_MYSQL_LOG_STMT_NPARAMS];
        unsigned long       param_lengths[DB_INSTANCE_MYSQL_LOG_STMT_NPARAMS];
        char                src_port_str[32], sshd_pid_str[32];
        const char          *event_str;
        
        snprintf(src_port_str, sizeof(src_port_str), "%hu", the_event->src_port);
        snprintf(sshd_pid_str, sizeof(sshd_pid_str), "%ld", (long int)the_event->sshd_pid);
        event_str = log_event_to_str(the_event->event);
        
        // Reset the prepared statement state:
        if ( mysql_stmt_reset(THE_DB->log_statement) != 0 ) {
            if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, mysql_stmt_error(THE_DB->log_statement), -1);
            break;
        }
        
        // Bind parameters to the query:
        memset(param_values, 0, sizeof(param_values));
#define __BIND_STRING(IDX, S) \
        param_values[(IDX)].buffer_type = MYSQL_TYPE_STRING; \
        param_values[(IDX)].buffer = (char*)(S); \
        param_lengths[(IDX)] = strlen((S)); \
        param_values[(IDX)].length = &param_lengths[(IDX)]; \
        param_values[(IDX)].buffer_length = param_lengths[(IDX)] + 1;
        
        __BIND_STRING(0, the_event->dst_ipaddr);
        __BIND_STRING(1, the_event->src_ipaddr);
        __BIND_STRING(2, src_port_str);
        __BIND_STRING(3, event_str);
        __BIND_STRING(4, sshd_pid_str);
        __BIND_STRING(5, the_event->uid);
        __BIND_STRING(6, the_event->log_date);
        
#undef __BIND_STRING

        if ( mysql_stmt_bind_param(THE_DB->log_statement, param_values) != 0 ) {
            if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, mysql_stmt_error(THE_DB->log_statement), -1);
            break;
        }
        if ( mysql_stmt_execute(THE_DB->log_statement) != 0 ) {
            if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, mysql_stmt_error(THE_DB->log_statement), -1);
            break;
        }
        if ( mysql_stmt_reset(THE_DB->log_statement) != 0 ) {
            if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, mysql_stmt_error(THE_DB->log_statement), -1);
            break;
        }
        return true;
    }
    return false;
}

//

typedef struct {
    db_blocklist_enum_t     base;
    //
    MYSQL_STMT              *query;
    
    MYSQL_BIND              bind[1];
    char                    bind_str[256];
    unsigned long           bind_length;
    my_bool                 bind_is_null, bind_error;
  
    bool                    is_done;
} db_instance_mysql_blocklist_enum_t;

//

const char*
__db_instance_mysql_blocklist_enum_next(
    db_blocklist_enum_ref   the_enum
)
{
    db_instance_mysql_blocklist_enum_t  *THE_ENUM = (db_instance_mysql_blocklist_enum_t*)the_enum;
    int                                 rc;
    
    if ( THE_ENUM && ! THE_ENUM->is_done ) {
        rc = mysql_stmt_fetch(THE_ENUM->query);
        if ( rc == 1 || rc == MYSQL_NO_DATA ) {
            THE_ENUM->is_done = true;
        } else {
            if ( ! THE_ENUM->bind_is_null && THE_ENUM->bind_length ) return (const char*)THE_ENUM->bind_str;
        }
    }
    return NULL;
}

//

void
__db_instance_mysql_blocklist_enum_close(
    db_blocklist_enum_ref   the_enum
)
{
    db_instance_mysql_blocklist_enum_t  *THE_ENUM = (db_instance_mysql_blocklist_enum_t*)the_enum;
    
    if ( THE_ENUM ) {
        DEBUG("Database:  blocklist enum:  close enumerator %p", THE_ENUM);
        if ( THE_ENUM->query ) mysql_stmt_close(THE_ENUM->query);
        free((void*)THE_ENUM);
    }
}

//

struct db_blocklist_enum*
__db_instance_mysql_blocklist_enum_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_mysql_t                 *THE_DB = (db_instance_mysql_t*)the_db;
    db_instance_mysql_blocklist_enum_t  *new_enum = NULL;
    MYSQL_STMT                          *query = NULL;
    int                                 rc;
    
    if ( THE_DB->is_connected ) {
        // Allocate a prepared statment:
        query = mysql_stmt_init(&THE_DB->db_handle);
        if ( query ) {
            rc = mysql_stmt_prepare(query, db_mysql_blocklist_stmt_query_str, -1);
            if ( rc == 0 ) {
                new_enum = (db_instance_mysql_blocklist_enum_t*)malloc(sizeof(db_instance_mysql_blocklist_enum_t));
                if ( new_enum ) {
                    new_enum->bind[0].buffer_type = MYSQL_TYPE_STRING;
                    new_enum->bind[0].buffer = (char*)new_enum->bind_str;
                    new_enum->bind[0].buffer_length = sizeof(new_enum->bind_str);
                    new_enum->bind[0].is_null = &new_enum->bind_is_null;
                    new_enum->bind[0].length = &new_enum->bind_length;
                    new_enum->bind[0].error = &new_enum->bind_error;
                    rc = mysql_stmt_bind_result(query, &new_enum->bind[0]);
                    if ( rc == 0 ) {
                        rc = mysql_stmt_execute(query);
                        if ( rc == 0 ) {
                            new_enum->query = query;
                            new_enum->is_done = false;
                            
                            new_enum->base.next = __db_instance_mysql_blocklist_enum_next;
                            new_enum->base.close = __db_instance_mysql_blocklist_enum_close;
                    
                            DEBUG("Database:  blocklist enum:  opened enumerator %p", new_enum);
                        } else {
                            ERROR("Database:  blocklist enum:  failed to execute block list query:  %d", rc);
                            free((void*)new_enum);
                            new_enum = NULL;
                        }
                    } else {
                        ERROR("Database:  blocklist enum:  failed to bind result struct to block list query:  %d", rc);
                        free((void*)new_enum);
                        new_enum = NULL;
                    }
                }
            } else {
                ERROR("Database:  blocklist enum:  failed to prepare block list query:  %d", rc);
            }
            if ( ! new_enum && query ) mysql_stmt_close(query);
        } else {
            ERROR("Database:  blocklist enum:  failed to allocate block list query");
        }
    }
    return (struct db_blocklist_enum*)new_enum;
}