/*
 * iptracking
 * db_instance_csvfile.c
 *
 * CSV file database driver.
 *
 */

//

typedef struct {
    db_instance_t       base;
    //
    const char          *filename;
    FILE                *fptr;
    const char          *delimiter;
    //
    char                error_buffer[100];
} db_instance_csvfile_t;

//

static db_instance_t* __db_instance_csvfile_alloc(yaml_document_t *config_doc, yaml_node_t *database_node);
static void __db_instance_csvfile_dealloc(db_instance_t *the_db);
static bool __db_instance_csvfile_has_valid_configuration(db_instance_t *the_db, const char **error_msg);
static void __db_instance_csvfile_summarize_to_log(db_instance_t *the_db);
static bool __db_instance_csvfile_open(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_csvfile_close(db_instance_t *the_db, const char **error_msg);
static bool __db_instance_csvfile_log_one_event(db_instance_t *the_db, log_data_t *the_event, const char **error_msg);

//

static db_driver_callbacks_t    db_driver_csvfile_callbacks = {
        .driver_name = "csvfile",
        
        .alloc = __db_instance_csvfile_alloc,
        .dealloc = __db_instance_csvfile_dealloc,
        .has_valid_configuration = __db_instance_csvfile_has_valid_configuration,
        .summarize_to_log = __db_instance_csvfile_summarize_to_log,
        .open = __db_instance_csvfile_open,
        .close = __db_instance_csvfile_close,
        .log_one_event = __db_instance_csvfile_log_one_event
    };
    
//

db_instance_t*
__db_instance_csvfile_alloc(
    yaml_document_t *config_doc,
    yaml_node_t     *database_node
)
{
    db_instance_csvfile_t          *new_instance = NULL;
    yaml_node_t                     *prop_node;
    size_t                          base_bytes = sizeof(db_instance_csvfile_t);
    size_t                          extra_bytes = 0;
    const char                      *filename = NULL, *delimiter = ",";
    
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "delimiter")) ) {
        delimiter = yaml_helper_get_scalar_value(prop_node);
    }
    if ( ! delimiter || ! *delimiter ) {
        ERROR("Database: empty delimiter is not allowed");
        return NULL;
    }
    extra_bytes += strlen(delimiter) + 1;
    
    if ( (prop_node = yaml_helper_doc_node_at_path(config_doc, database_node, "filename")) ) {
        filename = yaml_helper_get_scalar_value(prop_node);
    }
    if ( ! filename ) {
        ERROR("Database: no filename provided in configuration");
        return NULL;
    }
    extra_bytes += strlen(filename) + 1;
    
    /* Ready to allocate: */
    new_instance = (db_instance_csvfile_t*)__db_instance_alloc(
                            &db_driver_csvfile_callbacks, base_bytes + extra_bytes);
    if ( new_instance ) {
        void        *p = (void*)new_instance + base_bytes;
        
#define DB_INSTANCE_CSVFILE_P_INC(T,N)     { size_t dp = (sizeof(T) * (N)); p += dp; extra_bytes -= dp; }
        
        /* Setup delimiter: */
        new_instance->delimiter = (const char*)p; DB_INSTANCE_CSVFILE_P_INC(char, strlen(delimiter) + 1);
        memcpy((char*)new_instance->delimiter, delimiter, strlen(delimiter) + 1);
        
        /* Setup filename: */
        new_instance->filename = (const char*)p; DB_INSTANCE_CSVFILE_P_INC(char, strlen(filename) + 1);
        memcpy((char*)new_instance->filename, filename, strlen(filename) + 1);

#undef DB_INSTANCE_CSVFILE_P_INC
    }
    return (db_instance_t*)new_instance;
}

//

void
__db_instance_csvfile_dealloc(
    db_instance_t   *the_db
)
{
}

//

bool
__db_instance_csvfile_has_valid_configuration(
    db_ref      the_db,
    const char  **error_msg
)
{
    /* There really is no mandatory keyword needed... */
    return true;
}

//

void
__db_instance_csvfile_summarize_to_log(
    db_instance_t   *the_db
)
{
    db_instance_csvfile_t   *THE_DB = (db_instance_csvfile_t*)the_db;
    
    INFO("Database: driver_name = %s", THE_DB->base.driver_callbacks->driver_name);
    INFO("Database: filename = %s", THE_DB->filename);
    INFO("Database: delimiter = %s", THE_DB->delimiter ? THE_DB->delimiter : ",");
}

//

bool
__db_instance_csvfile_open(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_csvfile_t   *THE_DB = (db_instance_csvfile_t*)the_db;
    
    if ( ! THE_DB->fptr ) {
        int                 rc;
        
        DEBUG("Database: connecting to file '%s'", THE_DB->filename);
        THE_DB->fptr = fopen(THE_DB->filename, "a");
        if ( ! THE_DB->fptr ) {
            if ( error_msg ) {
                strerror_r(errno, THE_DB->error_buffer, sizeof(THE_DB->error_buffer));
                *error_msg = THE_DB->error_buffer;
            }
            return false;
        }
        
        DEBUG("Database: file open, database interface ready");
    }
    return true;
}

//

bool
__db_instance_csvfile_close(
    db_instance_t   *the_db,
    const char      **error_msg
)
{
    db_instance_csvfile_t   *THE_DB = (db_instance_csvfile_t*)the_db;
    
    if ( THE_DB->fptr ) {
        fclose(THE_DB->fptr);
        THE_DB->fptr = NULL;
    }
    return true;
}

//

bool
__db_instance_csvfile_log_one_event(
    db_instance_t   *the_db,
    log_data_t      *the_event,
    const char      **error_msg
)
{
    db_instance_csvfile_t  *THE_DB = (db_instance_csvfile_t*)the_db;
    bool                    okay = false;
    
    if ( THE_DB->fptr ) {
        int         rc;
        
        rc = fprintf(THE_DB->fptr, "%2$s%1$s%3$s%1$s%4$d%1$s%5$s%1$s%6$s%1$s%7$s\n",
                THE_DB->delimiter ? THE_DB->delimiter : ",",
                the_event->dst_ipaddr,
                the_event->src_ipaddr,
                the_event->src_port,
                log_event_to_str(the_event->event),
                the_event->uid,
                the_event->log_date);
        if ( rc <= 0 ) {
            if ( error_msg ) {
                strerror_r(rc, THE_DB->error_buffer, sizeof(THE_DB->error_buffer));
                *error_msg = THE_DB->error_buffer;
            }
        } else {
            fflush(THE_DB->fptr);
            okay = true;
        }
    }
    return okay;
}
