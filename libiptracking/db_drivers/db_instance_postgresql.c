/*
 * iptracking
 * db_instance_postgresql.c
 *
 * PostgreSQL database driver.
 *
 */

#include <libpq-fe.h>
#include <poll.h>

//

#define DB_INSTANCE_POSTGRESQL_LOG_STMT_NAME_STR "log_one_event"
#define DB_INSTANCE_POSTGRESQL_LOG_STMT_NPARAMS 7
#define DB_INSTANCE_POSTGRESQL_LOG_STMT_QUERY_FORMAT "SELECT %s%slog_one_event($1, $2, $3, $4, $5, $6, $7);"
#define DB_INSTANCE_POSTGRESQL_BLOCKLIST_STMT_QUERY_FORMAT "SELECT ip_entity FROM %s%sblock_now"

static const char   *db_postgresql_log_stmt_name = DB_INSTANCE_POSTGRESQL_LOG_STMT_NAME_STR;
static const char   *db_postgresql_log_stmt_query_format = DB_INSTANCE_POSTGRESQL_LOG_STMT_QUERY_FORMAT;
static const int    db_postgresql_log_stmt_nparams = DB_INSTANCE_POSTGRESQL_LOG_STMT_NPARAMS;
static const char   *db_postgresql_blocklist_stmt_query_format = DB_INSTANCE_POSTGRESQL_BLOCKLIST_STMT_QUERY_FORMAT;

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
    const char*         pam_schema;
    const char*         firewall_schema;
    const char*         firewall_notify_channel;
    //
    PGconn              *db_conn;
    //
    bool                is_notify_running;
    pthread_t           notify_thread;
} db_instance_postgresql_t;

//

static db_instance_t* __db_instance_postgresql_alloc(yaml_document_t *config_doc, yaml_node_t *database_node);
static void __db_instance_postgresql_dealloc(db_instance_t *the_db);
static bool __db_instance_postgresql_has_valid_configuration(db_instance_t *the_db, const char **error_msg);
static void __db_instance_postgresql_summarize_to_log(db_instance_t *the_db);
static bool __db_instance_postgresql_open(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_postgresql_close(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_postgresql_log_one_event(db_instance_t *the_db, log_data_t *the_event, const char **error_msg);
static struct db_blocklist_enum* __db_instance_postgresql_blocklist_enum_open(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_postgresql_blocklist_async_notification_toggle(struct db_instance *the_db, bool start_if_true, const char **error_msg);

//

static db_driver_callbacks_t    db_driver_postgresql_callbacks = {
        .driver_name = "postgresql",
        
        .alloc = __db_instance_postgresql_alloc,
        .dealloc = __db_instance_postgresql_dealloc,
        .has_valid_configuration = __db_instance_postgresql_has_valid_configuration,
        .summarize_to_log = __db_instance_postgresql_summarize_to_log,
        .open = __db_instance_postgresql_open,
        .close = __db_instance_postgresql_close,
        .log_one_event = __db_instance_postgresql_log_one_event,
        .blocklist_enum_open = __db_instance_postgresql_blocklist_enum_open,
        
        .blocklist_async_notification_toggle = __db_instance_postgresql_blocklist_async_notification_toggle
    };
    
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
    const char  *pam_schema = NULL, *firewall_schema = NULL, *firewall_notify_channel = NULL;
    
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
        
    /* pam_schema? */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "pamd.schema")) ) {
        v = yaml_helper_get_scalar_value(prop_node);
        if ( v ) {
            pam_schema = v;
            extra_bytes += strlen(v) + 1;
        } else {
            pam_schema = NULL;
        }
    }
        
    /* firewall_schema? */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "firewalld.schema")) ) {
        v = yaml_helper_get_scalar_value(prop_node);
        if ( v ) {
            firewall_schema = v;
            extra_bytes += strlen(v) + 1;
        } else {
            firewall_schema = NULL;
        }
    }
        
    /* firewall_notify_channel? */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "firewalld.notify-channel")) ) {
        v = yaml_helper_get_scalar_value(prop_node);
        if ( v ) {
            firewall_notify_channel = v;
            extra_bytes += strlen(v) + 1;
        } else {
            firewall_notify_channel = NULL;
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
        
        /* Fill-in the pam_schema if present: */
        if ( pam_schema ) {
            char    *e = stpncpy(p, pam_schema, extra_bytes);
            
            new_instance->pam_schema = (const char*)p;
            DB_INSTANCE_POSTGRESQL_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->pam_schema = NULL;
        }
        
        /* Fill-in the firewall_schema if present: */
        if ( firewall_schema ) {
            char    *e = stpncpy(p, firewall_schema, extra_bytes);
            
            new_instance->firewall_schema = (const char*)p;
            DB_INSTANCE_POSTGRESQL_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->firewall_schema = NULL;
        }
        
        /* Fill-in the firewall_notify_channel if present: */
        if ( firewall_schema ) {
            char    *e = stpncpy(p, firewall_notify_channel, extra_bytes);
            
            new_instance->firewall_notify_channel = (const char*)p;
            DB_INSTANCE_POSTGRESQL_P_INC(char, (e - (char*)p + 1));
        } else {
            new_instance->firewall_notify_channel = NULL;
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
    if ( THE_DB->pam_schema ) INFO("Database: pam schema = %s", THE_DB->pam_schema);
    if ( THE_DB->firewall_schema ) INFO("Database: firewall schema = %s", THE_DB->firewall_schema);
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
        } else if ( (THE_DB->base.options & db_options_no_pam_logging) == db_options_no_pam_logging ) {
            DEBUG("Database: connection okay");  
        } else {
            const char          *db_log_stmt_query = NULL;
            int                 db_log_stmt_query_len;
            const char          *schema = (THE_DB->pam_schema && *THE_DB->pam_schema) ? 
                                                    THE_DB->pam_schema : NULL;
            
            DEBUG("Database: connection okay, preparing query");    
            /* Prepare the query with the schema et al.: */
            db_log_stmt_query_len = asprintf(&db_log_stmt_query, db_postgresql_log_stmt_query_format,
                                                schema ? schema : "",
                                                schema ? "." : "");
            if ( db_log_stmt_query_len && db_log_stmt_query ) {
                PGresult            *db_result;
                ExecStatusType      db_rc;
                
                /* Send the query to the server for preparation: */
                db_result = PQprepare(THE_DB->db_conn, db_postgresql_log_stmt_name, db_log_stmt_query,
                                    db_postgresql_log_stmt_nparams, NULL);
                db_rc = PQresultStatus(db_result);
                PQclear(db_result);
                free((void*)db_log_stmt_query);
                if ( db_rc == PGRES_COMMAND_OK ) {
                    DEBUG("Database: logging query prepared");
                } else {
                    if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, PQerrorMessage(THE_DB->db_conn), -1);
                    return false;
                }
            } else {
                if ( error_msg ) *error_msg = "failed to generate prepared query statement";
            }
        }
        if ( THE_DB->base.blocklist_async_notification_callback ) {
            __db_instance_postgresql_blocklist_async_notification_toggle(the_db, true, error_msg);
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
        if ( THE_DB->is_notify_running )
            __db_instance_postgresql_blocklist_async_notification_toggle(the_db, false, error_msg);
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
        char            src_port_str[32], sshd_pid_str[32];
        PGresult        *db_result;
        ExecStatusType  db_rc;
        
        snprintf(src_port_str, sizeof(src_port_str), "%hu", the_event->src_port);
        snprintf(sshd_pid_str, sizeof(sshd_pid_str), "%ld", (long int)the_event->sshd_pid);
        
        param_values[0] = the_event->dst_ipaddr;
        param_values[1] = the_event->src_ipaddr;
        param_values[2] = src_port_str;
        param_values[3] = log_event_to_str(the_event->event);
        param_values[4] = sshd_pid_str;
        param_values[5] = the_event->uid;
        param_values[6] = the_event->log_date;
        db_result = PQexecPrepared(THE_DB->db_conn, db_postgresql_log_stmt_name, db_postgresql_log_stmt_nparams,
                            param_values, NULL, NULL, 0);
        db_rc = PQresultStatus(db_result);
        PQclear(db_result);
        switch ( db_rc ) {
            case PGRES_COMMAND_OK:
            case PGRES_TUPLES_OK:
                DEBUG("Database: logged { %s, %s, %s, %ld, %s, %hu, %s }",
                    the_event->log_date,
                    log_event_to_str(the_event->event),
                    the_event->uid,
                    (long int)the_event->sshd_pid,
                    the_event->src_ipaddr,
                    the_event->src_port,
                    the_event->dst_ipaddr);
                return true;
            default: {
                if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, PQerrorMessage(THE_DB->db_conn), -1);
                break;
            }
        }
    }
    return false;
}

//

typedef struct {
    db_blocklist_enum_t     base;
    //
    PGresult                *query;
    int                     i, i_max;
} db_instance_postgresql_blocklist_enum_t;

//

const char*
__db_instance_postgresql_blocklist_enum_next(
    db_blocklist_enum_ref   the_enum
)
{
    db_instance_postgresql_blocklist_enum_t *THE_ENUM = (db_instance_postgresql_blocklist_enum_t*)the_enum;
    
    return (THE_ENUM && (THE_ENUM->i < THE_ENUM->i_max)) ? PQgetvalue(THE_ENUM->query, THE_ENUM->i++, 0) : NULL;
}

//

void
__db_instance_postgresql_blocklist_enum_close(
    db_blocklist_enum_ref   the_enum
)
{
    db_instance_postgresql_blocklist_enum_t *THE_ENUM = (db_instance_postgresql_blocklist_enum_t*)the_enum;
    
    if ( THE_ENUM ) {
        DEBUG("Database:  blocklist enum:  close enumerator %p", THE_ENUM);
        if ( THE_ENUM->query ) PQclear(THE_ENUM->query);
        free((void*)THE_ENUM);
    }
}

//

struct db_blocklist_enum*
__db_instance_postgresql_blocklist_enum_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_postgresql_t                *THE_DB = (db_instance_postgresql_t*)the_db;
    db_instance_postgresql_blocklist_enum_t *new_enum = NULL;
    PGresult                                *qres = NULL;
    int                                     nrows;
    
    if ( THE_DB->db_conn ) {
        const char          *db_blocklist_stmt_query = NULL;
        int                 db_blocklist_stmt_query_len;
        const char          *schema= (THE_DB->firewall_schema && *THE_DB->firewall_schema) ? 
                                                THE_DB->firewall_schema : NULL;
         
        /* Prepare the query with the schema et al.: */
        db_blocklist_stmt_query_len = asprintf(&db_blocklist_stmt_query, db_postgresql_blocklist_stmt_query_format,
                                            schema ? schema : "",
                                            schema ? "." : "");
        if ( db_blocklist_stmt_query_len && db_blocklist_stmt_query ) {
            qres = PQexec(THE_DB->db_conn, db_blocklist_stmt_query);
            free((void*)db_blocklist_stmt_query);
            if ( qres && (PQresultStatus(qres) == PGRES_TUPLES_OK) ) {
                if ( (nrows = PQntuples(qres)) > 0 ) {
                    new_enum = (db_instance_postgresql_blocklist_enum_t*)malloc(sizeof(db_instance_postgresql_blocklist_enum_t));
                    if ( new_enum ) {
                        new_enum->query = qres;
                        new_enum->i = 0;
                        new_enum->i_max = nrows;
                        
                        new_enum->base.next = __db_instance_postgresql_blocklist_enum_next;
                        new_enum->base.close = __db_instance_postgresql_blocklist_enum_close;
                        
                        DEBUG("Database:  blocklist enum:  opened enumerator %p (%d rows)", new_enum, nrows);
                    } else {
                        ERROR("Database:  blocklist enum:  failed to allocate enumerator");
                    }
                } else {
                    INFO("Database:  blocklist enum:  no records in block list");
                }
            } else if ( qres ) {
                ERROR("Database:  blocklist enum:  failed to execute block list query:  %s", PQresultErrorMessage(qres));
            } else {
                ERROR("Database:  blocklist enum:  failed to execute block list query:  %s", PQerrorMessage(THE_DB->db_conn));
            }
            if ( ! new_enum && qres ) PQclear(qres);
        }
    }
    return (struct db_blocklist_enum*)new_enum;
}

//

void*
__db_instance_postgresql_blocklist_async_notification_thread(
    void        *the_db
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;
    int                         rc, pgfd;
    
    
    INFO("Database:  notification listener thread:  waiting for Postgres connection...");
    while ( ! THE_DB->db_conn ) sleep(1);
    
    pgfd = PQsocket(THE_DB->db_conn);
    if ( pgfd >= 0 ) {
        const char              *db_notify_stmt_query = NULL;
        int                     db_notify_stmt_query_len;
        PGresult                *qres;
        
        INFO("Database:  notification listener thread:  Postgres socket: fd = %d", pgfd);
        
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &rc);
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &rc);
        
        // Start listening for notifications:
        INFO("Database:  notification listener thread:  exec LISTEN query");
        
        // Prep the query:
        if ( THE_DB->firewall_notify_channel ) {
            db_notify_stmt_query_len = asprintf(&db_notify_stmt_query, "UNLISTEN %s", THE_DB->firewall_notify_channel);
            if ( db_notify_stmt_query_len && db_notify_stmt_query ) {
                // Use the query string minus the leading "UN":
                qres = PQexec(THE_DB->db_conn, db_notify_stmt_query + 2);
            } else {
                // Fall back to generic notification listening:
                qres = PQexec(THE_DB->db_conn, "LISTEN");
            }
        } else {
            qres = PQexec(THE_DB->db_conn, "LISTEN");
        }
        if ( qres ) {
            ExecStatusType      qres_status = PQresultStatus(qres);
            
            PQclear(qres);
            if ( qres_status == PGRES_COMMAND_OK ) {
                DEBUG("Database:  notification listener thread:  entering runloop");
                while ( THE_DB->is_notify_running ) {
                    struct pollfd       fds = { .fd = pgfd, .events = POLLIN, .revents = 0 };
                    
                    rc = poll(&fds, 1, 60);
                    if ( rc > 0 ) {
                        PGnotify    *pgnotify;
                        int         nnotify = 0;
                        
                        // It must be data on the socket...
                        DEBUG("Database:  notification listener thread:  data waiting on socket");
                        PQconsumeInput(THE_DB->db_conn);
                        while ((pgnotify = PQnotifies(THE_DB->db_conn)) != NULL) {
                            PQfreemem(pgnotify);
                            nnotify++;
                        }
                        if ( nnotify > 0 ) {
                            db_blocklist_enum_ref   eblocklist;
                            
                            INFO("Database:  notification listener thread:  %d notification(s) waiting", nnotify);
                            pthread_mutex_lock(&THE_DB->base.blocklist_async_notification_lock);
                            eblocklist = db_blocklist_enum_open((db_ref)the_db, NULL);
                            if ( eblocklist ) {
                                INFO("Database:  notification listener thread:  dispatching block list to callback");
                                THE_DB->base.blocklist_async_notification_callback(
                                        eblocklist,
                                        THE_DB->base.blocklist_async_notification_context);
                                db_blocklist_enum_close(eblocklist);
                            }
                            pthread_mutex_unlock(&THE_DB->base.blocklist_async_notification_lock);
                        }
                    }
                }
                DEBUG("Database:  notification listener thread:  exited runloop");
                
                // Stop listening for notifications:
                INFO("Database:  notification listener thread:  exec UNLISTEN query");
                if ( db_notify_stmt_query ) {
                    qres = PQexec(THE_DB->db_conn, db_notify_stmt_query);
                } else {
                    qres = PQexec(THE_DB->db_conn, "UNLISTEN");
                }
                if ( qres ) PQclear(qres);
            } else {
                ERROR("Database:  notification listener thread:  exec LISTEN query failed (status = %d)", qres_status);
            }
        } else {
            ERROR("Database:  notification listener thread:  exec LISTEN query failed");
        }
    } else {
        ERROR("Database:  Postgres socket unavailable: fd = %d", pgfd);
    }
    return NULL;
}

bool
__db_instance_postgresql_blocklist_async_notification_toggle(
    db_instance_t   *the_db,
    bool            start_if_true,
    const char      **error_msg
)
{
    db_instance_postgresql_t    *THE_DB = (db_instance_postgresql_t*)the_db;    
    int                         rc;
    bool                        out_result = false;
    
    if ( start_if_true ) {
        if ( ! THE_DB->is_notify_running ) {
            
            DEBUG("Database:  spawning notification listener thread");
            THE_DB->is_notify_running = true;
            rc = pthread_create(
                    &THE_DB->notify_thread,
                    NULL,
                    __db_instance_postgresql_blocklist_async_notification_thread,
                    (void*)the_db);
            if ( rc == 0 ) {
                INFO("Database:  spawned notification listener thread");
                out_result = true;
            } else {
                THE_DB->is_notify_running = false;
                ERROR("Database:  failed to spawn notification listener thread (rc = %d)", rc);
            }
        } else {
            DEBUG("Database:  notification listener thread already running");
        }
    } else if (THE_DB->is_notify_running) {
        void        *thread_result;
        
        THE_DB->is_notify_running = false;
        rc = pthread_cancel(THE_DB->notify_thread);
        if ( rc == 0 ) {
            rc = pthread_join(THE_DB->notify_thread, &thread_result);
            if ( rc == 0 ) {
                out_result = true;
            } else {
                ERROR("Database:  error during notification listener thread join (errno = %d)", rc);
            }
        } else {
            THE_DB->is_notify_running = false;
            ERROR("Database:  error during notification listener thread cancel (errno = %d)", rc);
        }
    } else {
        DEBUG("Database:  notification listener thread already not running");
    }
    return out_result;
}
