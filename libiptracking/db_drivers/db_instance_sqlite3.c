/*
 * iptracking
 * db_instance_sqlite3.c
 *
 * SQLite3 database driver.
 *
 */

#include <sqlite3.h>

//

#define DB_INSTANCE_SQLITE3_LOG_STMT_QUERY_STR "INSERT INTO inet_log (dst_ipaddr, src_ipaddr, src_port, log_event, uid, log_date) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)"
#define DB_INSTANCE_SQLITE3_BLOCKLIST_STMT_QUERY_STR "SELECT ip_entity FROM firewall_block_now"

static const char   *db_sqlite3_log_stmt_query_str = DB_INSTANCE_SQLITE3_LOG_STMT_QUERY_STR;
static const char   *db_sqlite3_blocklist_stmt_query_str = DB_INSTANCE_SQLITE3_BLOCKLIST_STMT_QUERY_STR;

//

typedef struct {
    db_instance_t       base;
    //
    const char          *filename;
    int                 flags;
    //
    sqlite3             *db_conn;
    sqlite3_stmt        *db_query;
} db_instance_sqlite3_t;

//

static db_instance_t* __db_instance_sqlite3_alloc(yaml_document_t *config_doc, yaml_node_t *database_node);
static void __db_instance_sqlite3_dealloc(db_instance_t *the_db);
static bool __db_instance_sqlite3_has_valid_configuration(db_instance_t *the_db, const char **error_msg);
static void __db_instance_sqlite3_summarize_to_log(db_instance_t *the_db);
static bool __db_instance_sqlite3_open(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_sqlite3_close(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_sqlite3_log_one_event(db_instance_t *the_db, log_data_t *the_event, const char **error_msg);
static struct db_blocklist_enum* __db_instance_sqlite3_blocklist_enum_open(db_instance_t *the_db, const char **error_msg);

//

static db_driver_callbacks_t    db_driver_sqlite3_callbacks = {
        .driver_name = "sqlite3",
        
        .alloc = __db_instance_sqlite3_alloc,
        .dealloc = __db_instance_sqlite3_dealloc,
        .has_valid_configuration = __db_instance_sqlite3_has_valid_configuration,
        .summarize_to_log = __db_instance_sqlite3_summarize_to_log,
        .open = __db_instance_sqlite3_open,
        .close = __db_instance_sqlite3_close,
        .log_one_event = __db_instance_sqlite3_log_one_event,
        .blocklist_enum_open = __db_instance_sqlite3_blocklist_enum_open,
        
        .blocklist_async_notification_toggle = NULL
    };
    
//

struct db_instance_sqlite3_flag {
    const char      *flag_str;
    int             flag_bit;
} __db_instance_sqlite3_flags[] = {
#ifdef SQLITE_OPEN_URI
        { "URI", SQLITE_OPEN_URI },
#endif
#ifdef SQLITE_OPEN_MEMORY
        { "MEMORY", SQLITE_OPEN_MEMORY },
#endif
#ifdef SQLITE_OPEN_NOMUTEX
        { "NOMUTEX", SQLITE_OPEN_NOMUTEX },
#endif
#ifdef SQLITE_OPEN_FULLMUTEX
        { "FULLMUTEX", SQLITE_OPEN_FULLMUTEX },
#endif
#ifdef SQLITE_OPEN_SHAREDCACHE
        { "SHAREDCACHE", SQLITE_OPEN_SHAREDCACHE },
#endif
#ifdef SQLITE_OPEN_PRIVATECACHE
        { "PRIVATECACHE", SQLITE_OPEN_PRIVATECACHE },
#endif
#ifdef SQLITE_OPEN_NOFOLLOW
        { "NOFOLLOW", SQLITE_OPEN_NOFOLLOW },
#endif
        { NULL, 0 }
    };

db_instance_t*
__db_instance_sqlite3_alloc(
    yaml_document_t *config_doc,
    yaml_node_t     *database_node
)
{
    db_instance_sqlite3_t           *new_instance = NULL;
    yaml_node_t                     *prop_node;
    size_t                          base_bytes = sizeof(db_instance_sqlite3_t);
    size_t                          extra_bytes = 0;
    int                             sqlite_flags = SQLITE_OPEN_READWRITE;
    const char                      *v, *filename = NULL;
    bool                            had_uri = false;
    
    /*
     * Check for any open flags:
     */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "flags")) ) {
        /* prop_node should be a sequence: */
        if ( prop_node->type == YAML_SEQUENCE_NODE ) {
            yaml_node_item_t        *item = prop_node->data.sequence.items.start;
            
            while ( item < prop_node->data.sequence.items.top ) {
                yaml_node_t         *val_node = yaml_document_get_node(config_doc, *item);
                
                if ( val_node ) {
                    if ( (v = yaml_helper_get_scalar_value(val_node)) ) {
                        struct db_instance_sqlite3_flag *flags = __db_instance_sqlite3_flags;
                        while ( flags->flag_str ) {
                            if ( strcasecmp(flags->flag_str, v) == 0 ) {
                                sqlite_flags |= flags->flag_bit;
                                break;
                            }
                            flags++;
                        }
                        if ( ! flags->flag_str ) WARN("Database: unknown flag: %s", v);
                    } else {
                        ERROR("Database: 'flags' values must be scalars");
                        return NULL;
                    }
                }
                item++;
            }
        } else {
            ERROR("Database: value of 'flags' key is not a sequence");
            return NULL;
        }
    }
    
    /* Is a uri property present? */
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "uri")) ) {
        if ( (v = yaml_helper_get_scalar_value(prop_node)) ) {
            filename = v;
            had_uri = true;
            sqlite_flags |= SQLITE_OPEN_URI;
        }
    }
    if ( ! had_uri && (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "filename")) ) {
        filename = yaml_helper_get_scalar_value(prop_node);
    }
    if ( ! filename ) {
        ERROR("Database: no uri or filename provided in configuration");
        return NULL;
    }
    extra_bytes += strlen(filename) + 1;
    
    /* Ready to allocate: */
    new_instance = (db_instance_sqlite3_t*)__db_instance_alloc(
                            &db_driver_sqlite3_callbacks, base_bytes + extra_bytes);
    if ( new_instance ) {
        void        *p = (void*)new_instance + base_bytes;
        
        new_instance->flags = sqlite_flags;
        
#define DB_INSTANCE_SQLITE3_P_INC(T,N)     { size_t dp = (sizeof(T) * (N)); p += dp; extra_bytes -= dp; }
        
        /* Setup filename: */
        new_instance->filename = (const char*)p; DB_INSTANCE_SQLITE3_P_INC(const char*,strlen(filename) + 1);
        memcpy((char*)new_instance->filename, filename, strlen(filename) + 1);

#undef DB_INSTANCE_SQLITE3_P_INC
    }
    return (db_instance_t*)new_instance;
}

//

void
__db_instance_sqlite3_dealloc(
    db_instance_t   *the_db
)
{
}

//

bool
__db_instance_sqlite3_has_valid_configuration(
    db_ref      the_db,
    const char  **error_msg
)
{
    /* There really is no mandatory keyword needed... */
    return true;
}

//

void
__db_instance_sqlite3_summarize_to_log(
    db_instance_t   *the_db
)
{
    db_instance_sqlite3_t   *THE_DB = (db_instance_sqlite3_t*)the_db;
    
    INFO("Database: driver_name = %s", THE_DB->base.driver_callbacks->driver_name);
    INFO("Database: filename = %s", THE_DB->filename);
    INFO("Database: flags = %X", THE_DB->flags);
}

//

bool
__db_instance_sqlite3_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_sqlite3_t   *THE_DB = (db_instance_sqlite3_t*)the_db;
    
    if ( ! THE_DB->db_conn ) {
        int                 rc;
        
        DEBUG("Database: connecting to database");
        rc = sqlite3_open_v2(THE_DB->filename, &THE_DB->db_conn, THE_DB->flags, NULL);
        if ( rc != SQLITE_OK ) {
            if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, sqlite3_errstr(rc), -1);
            return false;
        }
        
        if ( (THE_DB->base.options & db_options_no_pam_logging) == db_options_no_pam_logging ) {
            DEBUG("Database: connection okay"); 
        } else {
            DEBUG("Database: connection okay, preparing query");
            rc = sqlite3_prepare_v2(THE_DB->db_conn, db_sqlite3_log_stmt_query_str, -1, &THE_DB->db_query, NULL);
            if ( rc != SQLITE_OK ) {
                if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, sqlite3_errstr(rc), -1);
                return false;
            }
            DEBUG("Database: query prepared, database ready");
        }
    }
    return true;
}

//

bool
__db_instance_sqlite3_close(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_sqlite3_t   *THE_DB = (db_instance_sqlite3_t*)the_db;
    
    if ( THE_DB->db_conn ) {
        int                 rc;
        
        if ( THE_DB->db_query ) {
            DEBUG("Database: closing prepared query");
            rc = sqlite3_finalize(THE_DB->db_query);
            THE_DB->db_query = NULL;
        }
        DEBUG("Database: closing connection");
        sqlite3_close(THE_DB->db_conn);
        THE_DB->db_conn = NULL;
    }
    return true;
}

//

bool
__db_instance_sqlite3_log_one_event(
    db_instance_t   *the_db,
    log_data_t      *the_event,
    const char      **error_msg
)
{
    db_instance_sqlite3_t   *THE_DB = (db_instance_sqlite3_t*)the_db;
    bool                    okay = false;
    
    if ( THE_DB->db_conn && THE_DB->db_query ) {
        /* Bind event data to the query: */
        int                    rc  = sqlite3_bind_text(THE_DB->db_query, 1, the_event->dst_ipaddr, -1, SQLITE_STATIC);
        if ( rc == SQLITE_OK ) rc = sqlite3_bind_text(THE_DB->db_query, 2, the_event->src_ipaddr, -1, SQLITE_STATIC);
        if ( rc == SQLITE_OK ) rc = sqlite3_bind_int(THE_DB->db_query, 3, (int)the_event->src_port);
        if ( rc == SQLITE_OK ) rc = sqlite3_bind_int(THE_DB->db_query, 4, the_event->event);
        if ( rc == SQLITE_OK ) rc = sqlite3_bind_int(THE_DB->db_query, 5, (int)the_event->sshd_pid);
        if ( rc == SQLITE_OK ) rc = sqlite3_bind_text(THE_DB->db_query, 6, the_event->uid, -1, SQLITE_STATIC);
        if ( rc == SQLITE_OK ) rc = sqlite3_bind_text(THE_DB->db_query, 7, the_event->log_date, -1, SQLITE_STATIC);
        
        if ( rc == SQLITE_OK ) {
            rc = sqlite3_step(THE_DB->db_query);
            if ( rc == SQLITE_DONE ) {
                okay = true;
            } else {
                if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, sqlite3_errmsg(THE_DB->db_conn), -1);
            }
        } else {
            if ( error_msg ) *error_msg = __db_instance_set_last_error(the_db, sqlite3_errmsg(THE_DB->db_conn), -1);
        }
        sqlite3_clear_bindings(THE_DB->db_query);
        sqlite3_reset(THE_DB->db_query);
    }
    return okay;
}

//

typedef struct {
    db_blocklist_enum_t     base;
    //
    sqlite3_stmt            *query;
    bool                    is_first, is_done;
} db_instance_sqlite3_blocklist_enum_t;

//

const char*
__db_instance_sqlite3_blocklist_enum_next(
    db_blocklist_enum_ref   the_enum
)
{
    db_instance_sqlite3_blocklist_enum_t    *THE_ENUM = (db_instance_sqlite3_blocklist_enum_t*)the_enum;
    const char                              *result = NULL;
    
    if ( THE_ENUM && ! THE_ENUM->is_done ) {
        int                                 rc;
        
        if ( THE_ENUM->is_first ) {
            rc = SQLITE_ROW;
            THE_ENUM->is_first = false;
        } else {
            rc = sqlite3_step(THE_ENUM->query);
        }
        if ( rc == SQLITE_ROW ) {
            result = (const char*)sqlite3_column_text(THE_ENUM->query, 0);
        } else {
            THE_ENUM->is_done = true;
        }
    }
    return result;
}

//

void
__db_instance_sqlite3_blocklist_enum_close(
    db_blocklist_enum_ref   the_enum
)
{
    db_instance_sqlite3_blocklist_enum_t    *THE_ENUM = (db_instance_sqlite3_blocklist_enum_t*)the_enum;
    
    if ( THE_ENUM ) {
        DEBUG("Database:  blocklist enum:  close enumerator %p", THE_ENUM);
        if ( THE_ENUM->query ) sqlite3_finalize(THE_ENUM->query);
        free((void*)THE_ENUM);
    }
}

//

struct db_blocklist_enum*
__db_instance_sqlite3_blocklist_enum_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_sqlite3_t                   *THE_DB = (db_instance_sqlite3_t*)the_db;
    db_instance_sqlite3_blocklist_enum_t    *new_enum = NULL;
    sqlite3_stmt                            *query = NULL;
    int                                     rc;
    
    if ( THE_DB->db_conn ) {
        rc = sqlite3_prepare_v2(THE_DB->db_conn, db_sqlite3_blocklist_stmt_query_str, -1, &query, NULL);
        if ( rc == SQLITE_OK ) {  
            rc = sqlite3_step(query);
            if ( rc == SQLITE_ROW ) {
                new_enum = (db_instance_sqlite3_blocklist_enum_t*)malloc(sizeof(db_instance_sqlite3_blocklist_enum_t));
                if ( new_enum ) {
                    new_enum->query = query;
                    new_enum->is_done = false;
                    new_enum->is_first = true;
                    
                    new_enum->base.next = __db_instance_sqlite3_blocklist_enum_next;
                    new_enum->base.close = __db_instance_sqlite3_blocklist_enum_close;
                    
                    DEBUG("Database:  blocklist enum:  opened enumerator %p", new_enum);
                }
            } else {
                INFO("Database:  blocklist enum:  no records in block list");
            }
            if ( ! new_enum && query ) sqlite3_finalize(query);
        } else {
            ERROR("Database:  blocklist enum:  failed to execute block list query:  %d", rc);
        }
    }
    return (struct db_blocklist_enum*)new_enum;
}
