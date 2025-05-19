/*
 * iptracking
 * iptracking-daemon.c
 *
 * Main program.
 *
 */

#include "iptracking-daemon.h"
#include "logging.h"
#include "log_queue.h"
#include "yaml-helpers.h"

//

static const char *configuration_filepath_default = CONFIGURATION_FILEPATH_DEFAULT;

//

static uint32_t log_pool_records_min = LOG_POOL_RECORDS_MIN;
static uint32_t log_pool_records_max = LOG_POOL_RECORDS_MAX;
static uint32_t log_pool_records_delta = LOG_POOL_RECORDS_DELTA;

//

static int log_pool_push_wait_seconds_min = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN;
static int log_pool_push_wait_seconds_max = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX;
static int log_pool_push_wait_seconds_dt_thresh = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH;
static int log_pool_push_wait_seconds_dt = LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT;

//

static bool should_create_fifo = false;
static const char *fifo_filepath = FIFO_FILEPATH_DEFAULT;

//

typedef struct {
    log_queue_ref   lq;
} thread_context_t;

//

static unsigned int db_conn_idx = 0;

static const char* db_conn_keys[] = {
        "host",
        "port",
        "user",
        "password",
        "dbname",
        NULL
    };
static const char* db_conn_keywords[] = {
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };
static const char* db_conn_values[] = {
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };
    
//

#define DB_LOG_STMT_NAME_STR "log_one_event"
#define DB_LOG_STMT_NPARAMS 6
#define DB_LOG_STMT_QUERY_STR "SELECT log_one_event($1, $2, $3, $4, $5, $6);"

//

int
db_runloop(
    thread_context_t    *context
)
{
    static const char   *db_log_stmt_name = DB_LOG_STMT_NAME_STR;
    static const char   *db_log_stmt_query = DB_LOG_STMT_QUERY_STR;
    static const int    db_log_stmt_nparams = DB_LOG_STMT_NPARAMS;
    PGconn              *db_conn = NULL;
    PGresult            *db_result;
    ExecStatusType      db_rc;
    bool                is_connecting = true;
    
    while ( is_connecting ) {
        INFO("Database: attempting connection...");
        db_conn = PQconnectdbParams(db_conn_keywords, db_conn_values, 0);
        if ( db_conn ) {
            if ( PQstatus(db_conn) == CONNECTION_OK ) {
                INFO("Database: connection okay");
                
                /* Prepare the logging query: */
                db_result = PQprepare(db_conn, db_log_stmt_name, db_log_stmt_query,
                                    db_log_stmt_nparams, NULL);
                db_rc = PQresultStatus(db_result);
                PQclear(db_result);
                switch ( db_rc ) {
                    case PGRES_COMMAND_OK:
                        /* Log the success and exit the loop: */
                        INFO("Database: logging query prepared");
                        is_connecting = false;
                        continue;
                    default:
                        ERROR("Database: failed to prepare logging query (%d): %s",
                                db_rc, PQerrorMessage(db_conn));
                        break;
                }
                break;
            }
            INFO("Database: connection failure: %s", PQerrorMessage(db_conn));
            PQfinish(db_conn);
            db_conn = NULL;
        }
        // Try again in 5 seconds:
        ERROR("Database: unable to connect to database, will retry");
        sleep(5);
    }
    while ( true ) {
        log_data_t      data;
        
        /* The log_queue_pop() function will block until a record becomes available: */
        if ( log_queue_pop(&context->lq, &data) ) {
            const char*     param_values[DB_LOG_STMT_NPARAMS];
            char            src_port_str[32];
            
            snprintf(src_port_str, sizeof(src_port_str), "%hu", data.src_port);
            
            DEBUG("Database: received data { %s, %s, %s, %s, %hu, %s }",
                data.log_date,
                log_event_to_str(data.event),
                data.uid,
                data.src_ipaddr,
                data.src_port,
                data.dst_ipaddr);
            
            param_values[0] = data.dst_ipaddr;
            param_values[1] = data.src_ipaddr;
            param_values[2] = src_port_str;
            param_values[3] = log_event_to_str(data.event);
            param_values[4] = data.uid;
            param_values[5] = data.log_date;
            db_result = PQexecPrepared(db_conn, db_log_stmt_name, db_log_stmt_nparams,
                                param_values, NULL, NULL, 0);
            db_rc = PQresultStatus(db_result);
            PQclear(db_result);
            switch ( db_rc ) {
                case PGRES_COMMAND_OK:
                case PGRES_TUPLES_OK:
                    DEBUG("Database: data logged");
                    break;
                default:
                    ERROR("Database: unable to log data{ %s, %s, %s, %s, %hu, %s }",
                        data.log_date,
                        log_event_to_str(data.event),
                        data.uid,
                        data.src_ipaddr,
                        data.src_port,
                        data.dst_ipaddr);
                    break;
            }
        }
    }
    PQfinish(db_conn);
    return 0;
}

//

void*
db_thread_entry(
    void    *context
)
{
    thread_context_t    *CONTEXT = (thread_context_t*)context;
    
    while ( true ) {
        db_runloop(CONTEXT);
    }
    return NULL;
}

//

void*
event_thread_entry(
    void    *context
)
{
    thread_context_t    *CONTEXT = (thread_context_t*)context;
    char                read_buffer[sizeof(log_data_t) + 4];
    
    while ( true ) {
        int             fifo_fd = open(fifo_filepath, O_RDONLY);
        
        if ( fifo_fd >= 0 ) {
            char        *p = read_buffer, *p_end;
            size_t      p_len = sizeof(read_buffer), p_read;
            ssize_t     nbytes;
            bool        success = true;
            
            /* Read all data: */
            memset(read_buffer, 0, sizeof(read_buffer));
            DEBUG("Event reader: reading event from named pipe");
            while ( success && p_len && ((nbytes = read(fifo_fd, p, p_len)) != 0) ) {
                if ( nbytes < 0 ) {
                    if ( errno == EAGAIN ) continue;
                    success = false;
                } else {
                    p += nbytes;
                    p_len -= nbytes;
                }
            }
            p_read = (sizeof(read_buffer) - p_len);
            DEBUG("Event reader: read event of size %lld bytes", (long long)p_read);
            if ( success && p_len ) {
                log_data_t  data;
                
                /* Remove leading and trailing whitespace: */
                p = read_buffer;
                p_end = read_buffer + p_read - 1;
                while ( (p < p_end) && *p && isspace(*p) ) p++;
                while ( (p < p_end) && *p_end && isspace(*p_end)) p_end--;
                if ( p < p_end ) {
                    if ( log_data_parse(&data, read_buffer, (p_end - p) + 1) ) {
                        log_queue_push(&CONTEXT->lq, &data);
                    } else {
                        WARN("Event reader: invalid event string: %s", read_buffer);
                    }
                } else {
                    WARN("Event reader: empty event");
                }
            } else {
                WARN((p_len ? "Event reader: read failure" : "Event reader: event overflow"));
            }
            close(fifo_fd);
        } else {
            ERROR("Event reader: unable to open named pipe %s (errno=%d)", fifo_filepath, errno);
            sleep(5);
        }
    }
    return NULL;
}

//

bool
config_read_yaml_file(
    const char  *fpath
)
{
    bool        rc = false;
    FILE        *fptr = fopen(fpath, "r");
    
    if ( fptr ) {
        yaml_parser_t   parser;
        
        INFO("Configuration: attempting to parse file: %s", fpath);
        if ( yaml_parser_initialize(&parser) ) {
            yaml_document_t config_doc;
            
            DEBUG("Configuration: parser initialized");
            yaml_parser_set_input_file(&parser, fptr);
            if ( yaml_parser_load(&parser, &config_doc) ) {
                yaml_node_t *root_node = yaml_document_get_root_node(&config_doc);
                
                DEBUG("Configuration: document loaded");
                if ( root_node && (root_node->type == YAML_MAPPING_NODE) ) {
                    yaml_node_t     *node;
                    const char      *v;
                    
                    rc = true;
                    while ( 1 ) {
                        /*
                         * Check for any database config items:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, 0, "database")) ) {
                            const char*     *keys = db_conn_keys;
                            yaml_node_t     *dbnode;
                            
                            while ( *keys ) {
                                if ( (dbnode = yaml_helper_doc_node_at_path(&config_doc, node, 0, *keys)) ) {
                                    v = yaml_helper_get_scalar_value(dbnode);
                                    if ( v ) {
                                        db_conn_keywords[db_conn_idx] = *keys;
                                        db_conn_values[db_conn_idx++] = v;
                                    }
                                }
                                keys++;
                            }
                        }
                        /*
                         * Check for the fifo file path:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, 0, "fifo-file")) ) {
                            const char  *s = yaml_helper_get_scalar_value(node);
                            
                            if ( ! s ) {
                                ERROR("Configuration: invalid fifo-file value");
                                rc = false;
                                break;
                            }
                            fifo_filepath = s;
                        }
                        /*
                         * Check for any log-pool config items:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, 0, "log-pool.records")) ) {
                            yaml_node_t     *val_node;
                            
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "min")) ) {
                                if ( ! yaml_helper_get_scalar_uint32_value(val_node, &log_pool_records_min) ) {
                                    ERROR("Configuration: invalid log-pool.records.min value");
                                    rc = false;
                                    break;
                                }
                            }
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "max")) ) {
                                if ( ! yaml_helper_get_scalar_uint32_value(val_node, &log_pool_records_max) ) {
                                    ERROR("Configuration: invalid log-pool.records.max value");
                                    rc = false;
                                    break;
                                }
                            }
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "delta")) ) {
                                if ( ! yaml_helper_get_scalar_uint32_value(val_node, &log_pool_records_delta) ) {
                                    ERROR("Configuration: invalid log-pool.records.delta value");
                                    rc = false;
                                    break;
                                }
                            }
                        }
                        /*
                         * Check for any wait time config items:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, 0, "log-pool.push-wait-seconds")) ) {
                            yaml_node_t     *val_node;
                            
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "min")) ) {
                                if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_min) ) {
                                    ERROR("Configuration: invalid log-pool.push-wait-seconds.min value");
                                    rc = false;
                                    break;
                                }
                            }
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "max")) ) {
                                if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_max) ) {
                                    ERROR("Configuration: invalid log-pool.push-wait-seconds.max value");
                                    rc = false;
                                    break;
                                }
                            }
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "delta")) ) {
                                if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_dt) ) {
                                    ERROR("Configuration: invalid log-pool.push-wait-seconds.delta value");
                                    rc = false;
                                    break;
                                }
                            }
                            if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, node, 0, "grow-threshold")) ) {
                                if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_dt_thresh) ) {
                                    ERROR("Configuration: invalid log-pool.push-wait-seconds.grow-threshold value");
                                    rc = false;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }  else {
                    errno = EINVAL;
                    FATAL("Configuration: empty YAML document");
                }
            }  else {
                errno = EINVAL;
                FATAL("Configuration: failed to load document: (err=%d, offset=%lld) %s", parser.error, parser.problem_offset, parser.problem);
            }
            yaml_parser_delete(&parser);
        } else {
            errno = ENOMEM;
            FATAL("Configuration: failed to initialize YAML parser");
        }
        fclose(fptr);
    } else {
        FATAL("Configuration: failed to open file: %s", fpath);
    }
    return rc;
}

//

bool
config_validate()
{
    int         idx;
    struct stat finfo;
    
    /* At the very least the database name is needed: */
    idx = 0;
    while ( idx < db_conn_idx ) {
        if ( strcmp(db_conn_keywords[idx], "dbname") == 0 ) break;
        idx++;
    }
    if ( idx == db_conn_idx ) {
        ERROR("Configuration: lacks a database.dbname value");
        return false;
    }
    
    /* Ensure record count min ≤ max: */
    if ( (log_pool_records_max != 0) && (log_pool_records_min > log_pool_records_max) ) {
        ERROR("Configuration: log-pool.records.min > log-pool.records.max");
        return false;
    }
    
    /* Ensure record push wait min ≤ max: */
    if ( (log_pool_push_wait_seconds_max != 0) && (log_pool_push_wait_seconds_min > log_pool_push_wait_seconds_max) ) {
        ERROR("Configuration: log-pool.push-wait-seconds.min > log-pool.push-wait-seconds.max");
        return false;
    }
    
    /* Check access and file type for the fifo: */
    if ( stat(fifo_filepath, &finfo) != 0 ) {
        if ( ! should_create_fifo ) {
            ERROR("Configuration: unable to stat() named pipe %s", fifo_filepath);
            return false;
        }
        if ( mkfifo(fifo_filepath, 0600) != 0 ) {
            ERROR("Configuration: unable to create named pipe %s: errno=%d", fifo_filepath, errno);
            return false;
        }
        WARN("Configuration: created named pipe %s", fifo_filepath);
    } else {
        if ( (finfo.st_mode & S_IFMT) != S_IFIFO ) {
            ERROR("Configuration: %s is not a named pipe", fifo_filepath);
            return false;
        }
        if ( access(fifo_filepath, R_OK) != 0 ) {
            ERROR("Configuration: no read access to named pipe %s", fifo_filepath);
            return false;
        }
    }
    
    INFO("                                  fifo-file = %s", fifo_filepath);
    
    INFO("                       log-pool.records.min = %lu", log_pool_records_min);
    INFO("                       log-pool.records.max = %lu", log_pool_records_max);
    INFO("                     log-pool.records.delta = %lu", log_pool_records_delta);
    
    INFO("             log-pool.push-wait-seconds.min = %lus", log_pool_push_wait_seconds_min);
    INFO("             log-pool.push-wait-seconds.max = %lus", log_pool_push_wait_seconds_max);
    INFO("           log-pool.push-wait-seconds.delta = %lus", log_pool_push_wait_seconds_dt);
    INFO("  log-pool.push-wait-seconds.grow-threshold = %lu", log_pool_push_wait_seconds_dt_thresh);
    
    return true;
}

//

static struct option cli_options[] = {
                   { "help",    no_argument,       0,  'h' },
                   { "version", no_argument,       0,  'V' },
                   { "verbose", no_argument,       0,  'v' },
                   { "quiet",   no_argument,       0,  'q' },
                   { "config",  required_argument, 0,  'c' },
                   { "mkfifo",  no_argument,       0,  'm' },
                   { NULL,      0,                 0,   0  }
               };
static const char *cli_options_str = "hVvqc:m";

//

void
usage(
    const char  *exe
)
{
    printf(
        "usage:\n\n"
        "    %s {options}\n\n"
        "  options:\n\n"
        "    -h/--help                  Show this information\n"
        "    -V/--version               Display daemon version\n"
        "    -v/--verbose               Increase level of printing\n"
        "    -q/--quiet                 Decrease level of printing\n"
        "    -c/--config <filepath>     Read configuration directives from the YAML file\n"
        "                               at <filepath> (default: %s)\n"
        "    -m/--mkfifo                Create the named pipe if it does not exist\n"
        "\n"
        "  notes:\n\n"
        "    - will read events from named pipe %s\n"
        "\n"
        "(v" IPTRACKING_VERSION_STR " built with " CC_VENDOR " %lu on " __DATE__ " " __TIME__ ")\n",
        exe,
        configuration_filepath_default,
        fifo_filepath,
        (unsigned long)CC_VERSION);
}

//

int
main(
    int             argc,
    char* const*    argv
)
{
    pthread_t           db_thread, event_thread;
    thread_context_t    tc;
    int                 opt_ch, verbose = 0, quiet = 0;
    const char          *config_filepath = configuration_filepath_default;
    log_queue_params_t  lq_params;
    
    /* Parse all CLI arguments: */
    while ( (opt_ch = getopt_long(argc, argv, cli_options_str, cli_options, NULL)) != -1 ) {
        switch ( opt_ch ) {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 'V':
                printf(IPTRACKING_VERSION_STR "\n");
                exit(0);
            case 'v':
                verbose++;
                break;
            case 'q':
                quiet++;
                break;
            case 'c':
                config_filepath = optarg;
                break;
            case 'm':
                should_create_fifo = true;
                break;
        }
    }
    
    /* Set logging level based on verbose/quiet options: */
    logging_set_level(logging_get_level() + verbose - quiet);
    
    /* Load configuration: */
    if ( ! config_read_yaml_file(config_filepath) ) exit(EINVAL);
    
    /* Validate configuration: */
    if ( ! config_validate() ) exit(EINVAL);
    
    /* Initialize the log queue parameters: */
    lq_params.records.min = log_pool_records_min;
    lq_params.records.max = log_pool_records_max;
    lq_params.records.delta = log_pool_records_delta;
    
    lq_params.push_wait_seconds.min = log_pool_push_wait_seconds_min;
    lq_params.push_wait_seconds.max = log_pool_push_wait_seconds_max;
    lq_params.push_wait_seconds.delta = log_pool_push_wait_seconds_dt;
    lq_params.push_wait_seconds.grow_threshold = log_pool_push_wait_seconds_dt_thresh;
    
    /* Create the log queue: */
    tc.lq = log_queue_create(&lq_params);
    
    /* Spawn the database consumer thread: */
    pthread_create(&db_thread, NULL, db_thread_entry, (void*)&tc);
    
    /* Spawn the event consumer thread: */
    pthread_create(&event_thread, NULL, event_thread_entry, (void*)&tc);
    
    /* Wait for termination: */
    pthread_join(db_thread, NULL);
    pthread_join(event_thread, NULL);
    
    return 0;
}
