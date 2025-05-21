/*
 * iptracking
 * db_instance_postgresql.c
 *
 * PostgreSQL database driver.
 *
 */

#include <libpq-fe.h>

//

#define DB_INSTANCE_POSTGRESQL_LOG_STMT_NAME_STR "log_one_event"
#define DB_INSTANCE_POSTGRESQL_LOG_STMT_NPARAMS 6
#define DB_INSTANCE_POSTGRESQL_LOG_STMT_QUERY_FORMAT "SELECT %s%slog_one_event($1, $2, $3, $4, $5, $6);"

static const char   *db_log_stmt_name = DB_INSTANCE_POSTGRESQL_LOG_STMT_NAME_STR;
static const char   *db_log_stmt_query_format = DB_INSTANCE_POSTGRESQL_LOG_STMT_QUERY_FORMAT;
static const int    db_log_stmt_nparams = DB_INSTANCE_POSTGRESQL_LOG_STMT_NPARAMS;

//

static const char* db_conn_keys[] = {
        "host",
        "hostaddr",
        "port",
        "dbname",
        "user",
        "password",
        "passfile",
        "require_auth",
        "channel_binding",
        "connect_timeout",
        "client_encoding",
        "options",
        "application_name",
        "fallback_application_name",
        "keepalives",
        "keepalives_idle",
        "keepalives_interval",
        "keepalives_count",
        "tcp_user_timeout",
        "sslmode",
        "requiressl",
        "sslnegotiation",
        "sslcompression",
        "sslcert",
        "sslkey",
        "sslpassword",
        "sslcertmode",
        "sslrootcert",
        "sslcrl",
        "sslcrldir",
        "sslsni",
        "requirepeer",
        "ssl_min_protocol_version",
        "ssl_max_protocol_version",
        "krbsrvname",
        "gsslib",
        "gssdelegation",
        "service",
        "target_session_attrs",
        "load_balance_hosts",
        NULL
    };
#define DB_CONN_KEYS_COUNT (sizeof(db_conn_keys) / sizeof(const char*))

//

typedef struct {
    db_instance_t       base;
    //
    const char*         *conn_keys;
    const char*         *conn_values;
    const char*         schema;
    //
    PGconn              *db_conn;
    //
    const char          *last_error;
} db_instance_postgresql_t;

//

static db_instance_t* __db_instance_postgresql_alloc(yaml_document_t *config_doc, yaml_node_t *database_node);
static void __db_instance_postgresql_dealloc(db_instance_t *the_db);
static bool __db_instance_postgresql_has_valid_configuration(db_instance_t *the_db, const char **error_msg);
static void __db_instance_postgresql_summarize_to_log(db_instance_t *the_db);
static bool __db_instance_postgresql_open(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_postgresql_close(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_postgresql_log_one_event(db_instance_t *the_db, log_data_t *the_event, const char **error_msg);

//

static db_driver_callbacks_t    db_driver_postgresql_callbacks = {
        .driver_name = "postgresql",
        
        .alloc = __db_instance_postgresql_alloc,
        .dealloc = __db_instance_postgresql_dealloc,
        .has_valid_configuration = __db_instance_postgresql_has_valid_configuration,
        .summarize_to_log = __db_instance_postgresql_summarize_to_log,
        .open = __db_instance_postgresql_open,
        .close = __db_instance_postgresql_close,
        .log_one_event = __db_instance_postgresql_log_one_event
    };
    
//

const char*
__db_instance_postgresql_copy_error(
    db_instance_postgresql_t    *the_db,
    const char                  *error_msg
)
{
    if ( the_db->last_error ) {
        free((void*)the_db->last_error);
        the_db->last_error = NULL;
    }
    if ( error_msg && *error_msg ) {
        char        *ps = (char*)error_msg;
        char        *pe;
        
        /* Skip past leading whitespace: */
        while ( *ps && isspace(*ps) ) ps++;
        
        /* From that point, move ahead to the NUL terminator: */
        pe = ps;
        while ( *pe ) pe++;
        
        /* If we didn't go anywhere, the string is empty: */
        if ( pe > ps ) {
            /* Step back to character previous to NUL terminator: */
            pe--;
            while ( (pe > ps) && isspace(*pe) ) pe--;
            
            /* If we still have a string, copy it: */
            if ( pe > ps ) the_db->last_error = strndup(ps, (pe - ps + 1));
        }
    }
    return the_db->last_error;
}

//

db_instance_t*
__db_instance_postgresql_alloc(
    yaml_document_t *config_doc,
    yaml_node_t     *database_node
)
{
    const char* db_conn_keywords[DB_CONN_KEYS_COUNT];
    const char* db_conn_values[DB_CONN_KEYS_COUNT];
    int         db_conn_idx = 0;
    const char  *schema = NULL;
    
    db_instance_postgresql_t    *new_instance = NULL;
    const char*                 *keys = db_conn_keys;
    yaml_node_t                 *prop_node;
    size_t                      base_bytes = sizeof(db_instance_postgresql_t);
    size_t                      extra_bytes = 0;
    const char                  *v;
    
    /*
     * Check for any recognizable database connection properties items:
     */
    while ( *keys ) {
        if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, *keys)) ) {
            v = yaml_helper_get_scalar_value(prop_node);
            if ( v ) {
                db_conn_keywords[db_conn_idx] = *keys;
                db_conn_values[db_conn_idx++] = v;
                extra_bytes += 2 * sizeof(const char**) + strlen(*keys) + strlen(v) + 2;
            }
        }
        keys++;
    }
    /* Add the NULL list terminators: */
    extra_bytes += 2 * sizeof(const char**);
        
    /* schema? */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "schema")) ) {
        v = yaml_helper_get_scalar_value(prop_node);
        if ( v ) {
            schema = v;
            extra_bytes += strlen(v) + 1;
        } else {
            schema = NULL;
        }
    }
    
    /* Ready to allocate: */
    new_instance = (db_instance_postgresql_t*)__db_instance_alloc(
                            &db_driver_postgresql_callbacks, base_bytes + extra_bytes);
    if ( new_instance ) {
        void        *p = (void*)new_instance + base_bytes;
        
#define DB_INSTANCE_POSTGRESQL_P_INC(T,N)     { size_t dp = (sizeof(T) * (N)); p += dp; extra_bytes -= dp; }
        
        /* Key and value lists setup: */
        new_instance->conn_keys = (const char **)p; DB_INSTANCE_POSTGRESQL_P_INC(const char**,db_conn_idx + 1);
        new_instance->conn_values = (const char **)p; DB_INSTANCE_POSTGRESQL_P_INC(const char**,db_conn_idx + 1);
        
        /* Fill-in the key and value lists: */
        new_instance->conn_keys[db_conn_idx] = NULL;
        new_instance->conn_values[db_conn_idx] = NULL;
        while ( db_conn_idx > 0 ) {
            char    *e;
            
            db_conn_idx--;
            
            new_instance->conn_keys[db_conn_idx] = (const char*)p;
            e = stpncpy((char*)p, db_conn_keywords[db_conn_idx], extra_bytes);
            DB_INSTANCE_POSTGRESQL_P_INC(char, (e - (char*)p + 1));
            
            new_instance->conn_values[db_conn_idx] = (const char*)p;
            e = stpncpy((char*)p, db_conn_values[db_conn_idx], extra_bytes);
            DB_INSTANCE_POSTGRESQL_P_INC(char, (e - (char*)p + 1));
        }
        
        /* Fill-in the schema if present: */
        if ( schema ) {
            char    *e = stpncpy(p, schema, extra_bytes);
            
            new_instance->schema = (const char*)p;
            DB_INSTANCE_POSTGRESQL_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->schema = NULL;
        }

#undef DB_INSTANCE_POSTGRESQL_P_INC
    }
    return (db_instance_t*)new_instance;
}

//

void
__db_instance_postgresql_dealloc(
    db_instance_t   *the_db
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;
    
    if ( THE_DB->last_error ) free((void*)THE_DB->last_error);
}

//

bool
__db_instance_postgresql_has_valid_configuration(
    db_ref      the_db,
    const char  **error_msg
)
{
    /* There really is no mandatory keyword needed... */
    return true;
}

//

void
__db_instance_postgresql_summarize_to_log(
    db_instance_t   *the_db
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;
    const char*                 *conn_keys = THE_DB->conn_keys;
    const char*                 *conn_values = THE_DB->conn_values;
    
    INFO("Database: driver_name = %s", THE_DB->base.driver_callbacks->driver_name);
    if ( conn_keys ) {
        while ( *conn_keys ) {
            if ( strcmp(*conn_keys, "password") ) {
                INFO("Database: %s = %s", *conn_keys++, *conn_values++);
            } else {
                INFO("Database: %s = %s", *conn_keys++, "********");
                *conn_values++;
            }
        }
    }
    if ( THE_DB->schema ) INFO("Database: schema = %s", THE_DB->schema);
}

//

bool
__db_instance_postgresql_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;
    
    if ( ! THE_DB->db_conn ) {
        DEBUG("Database: connecting to database");
        THE_DB->db_conn = PQconnectdbParams(THE_DB->conn_keys, THE_DB->conn_values, 0);
        if ( ! THE_DB->db_conn ) {
            if ( error_msg ) *error_msg = "General connection failure";
            return false;
        } else {
            const char          *db_log_stmt_query = NULL;
            int                 db_log_stmt_query_len;
            
            DEBUG("Database: connection okay, preparing query");    
            /* Prepare the query with the schema et al.: */
            db_log_stmt_query_len = asprintf(&db_log_stmt_query, db_log_stmt_query_format,
                                                (THE_DB->schema && *THE_DB->schema) ? THE_DB->schema : "",
                                                (THE_DB->schema && *THE_DB->schema) ? "." : "");
            if ( db_log_stmt_query_len && db_log_stmt_query ) {
                PGresult            *db_result;
                ExecStatusType      db_rc;
                
                /* Send the query to the server for preparation: */
                db_result = PQprepare(THE_DB->db_conn, db_log_stmt_name, db_log_stmt_query,
                                    db_log_stmt_nparams, NULL);
                db_rc = PQresultStatus(db_result);
                PQclear(db_result);
                free((void*)db_log_stmt_query);
                if ( db_rc == PGRES_COMMAND_OK ) {
                    DEBUG("Database: logging query prepared");
                } else {
                    if ( error_msg ) *error_msg = __db_instance_postgresql_copy_error(THE_DB, PQerrorMessage(THE_DB->db_conn));;
                    return false;
                }
            } else {
                if ( error_msg ) *error_msg = "failed to generate prepared query statement";
            }
        }
    }
    return true;
}

//

bool
__db_instance_postgresql_close(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;
    
    if ( THE_DB->db_conn ) {
        DEBUG("Database: closing connection");
        PQfinish(THE_DB->db_conn);
        THE_DB->db_conn = NULL;
    }
    return true;
}

//

bool
__db_instance_postgresql_log_one_event(
    db_instance_t   *the_db,
    log_data_t      *the_event,
    const char      **error_msg
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;
    
    if ( THE_DB->db_conn ) {
        const char*     param_values[DB_INSTANCE_POSTGRESQL_LOG_STMT_NPARAMS];
        char            src_port_str[32];
        PGresult        *db_result;
        ExecStatusType  db_rc;
        
        snprintf(src_port_str, sizeof(src_port_str), "%hu", the_event->src_port);
        
        param_values[0] = the_event->dst_ipaddr;
        param_values[1] = the_event->src_ipaddr;
        param_values[2] = src_port_str;
        param_values[3] = log_event_to_str(the_event->event);
        param_values[4] = the_event->uid;
        param_values[5] = the_event->log_date;
        db_result = PQexecPrepared(THE_DB->db_conn, db_log_stmt_name, db_log_stmt_nparams,
                            param_values, NULL, NULL, 0);
        db_rc = PQresultStatus(db_result);
        PQclear(db_result);
        switch ( db_rc ) {
            case PGRES_COMMAND_OK:
            case PGRES_TUPLES_OK:
                DEBUG("Database: logged { %s, %s, %s, %s, %hu, %s }",
                    the_event->log_date,
                    log_event_to_str(the_event->event),
                    the_event->uid,
                    the_event->src_ipaddr,
                    the_event->src_port,
                    the_event->dst_ipaddr);
                return true;
            default: {
                if ( error_msg ) *error_msg = __db_instance_postgresql_copy_error(THE_DB, PQerrorMessage(THE_DB->db_conn));
                break;
            }
        }
    }
    return false;
}
