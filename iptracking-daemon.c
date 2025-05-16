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

static bool should_create_pipe = false;
static const char *pipe_filepath = PIPE_FILEPATH_DEFAULT;

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

int
db_runloop(
    thread_context_t    *context
)
{
    PGconn  *db_conn = NULL;
    
    while ( true ) {
        INFO("Database: attempting connection...");
        db_conn = PQconnectdbParams(db_conn_keywords, db_conn_values, 0);
        if ( db_conn ) {
            if ( PQstatus(db_conn) == CONNECTION_OK ) {
                INFO("Database: connection okay");
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
            INFO("Received data: { %s, %s, %s, %s, %hu, %s }",
                data.log_date,
                log_event_str(data.event),
                data.uid,
                data.src_ipaddr,
                data.src_port,
                data.dst_ipaddr);
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
    
    db_runloop(CONTEXT);
    return NULL;
}

//

void*
log_queue_thread_entry(
    void    *context
)
{
    thread_context_t    *CONTEXT = (thread_context_t*)context;
    
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
                         * Check for the pipe file path:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, 0, "pipe-file")) ) {
                            const char  *s = yaml_helper_get_scalar_value(node);
                            
                            if ( ! s ) {
                                ERROR("Configuration: invalid pipe-file value");
                                rc = false;
                                break;
                            }
                            pipe_filepath = s;
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
    
    /* Check access and file type for the pipe: */
    if ( stat(pipe_filepath, &finfo) != 0 ) {
        if ( ! should_create_pipe ) {
            ERROR("Configuration: unable to stat() named pipe %s", pipe_filepath);
            return false;
        }
        if ( mkfifo(pipe_filepath, 0600) != 0 ) {
            ERROR("Configuration: unable to create named pipe %s: errno=%d", pipe_filepath, errno);
            return false;
        }
        WARN("Configuration: created named pipe %s", pipe_filepath);
    } else {
        if ( (finfo.st_mode & S_IFMT) != S_IFIFO ) {
            ERROR("Configuration: %s is not a named pipe", pipe_filepath);
            return false;
        }
        if ( access(pipe_filepath, R_OK) != 0 ) {
            ERROR("Configuration: no read access to named pipe %s", pipe_filepath);
            return false;
        }
    }
    
    INFO("                                  pipe-file = %s", pipe_filepath);
    
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
        pipe_filepath,
        (unsigned long)CC_VERSION);
}

//

int
main(
    int             argc,
    char* const*    argv
)
{
    pthread_t           db_thread, log_thread;
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
                should_create_pipe = true;
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
    
    if ( tc.lq  ) {
        log_data_t      data;
        
        log_data_parse_cstr(&data, "128.175.4.164,86.86.60.34,43567,1,frey,2025-05-15 14:11:00");
        log_queue_push(&tc.lq, &data);
        
        data.src_port = 33427;
        strncpy(&data.log_date[0], "2025-05-15 14:12:33-0400", sizeof(data.log_date));
        log_queue_push(&tc.lq, &data);
        
        strncpy(&data.src_ipaddr[0], "128.175.132.65", sizeof(data.src_ipaddr));
        data.src_port = 24006;
        strncpy(&data.uid[0], "hpcguest1546", sizeof(data.uid));
        strncpy(&data.log_date[0], "2025-05-15 14:14:04-0400", sizeof(data.log_date));
        log_queue_push(&tc.lq, &data);
        
        data.src_port = 24031;
        strncpy(&data.log_date[0], "2025-05-15 14:15:55-0400", sizeof(data.log_date));
        log_queue_push(&tc.lq, &data);
        
        sleep(2);
        data.src_port++;
        log_queue_push(&tc.lq, &data);
        
        sleep(2);
        data.src_port++;
        log_queue_push(&tc.lq, &data);
        
        sleep(2);
        data.src_port++;
        log_queue_push(&tc.lq, &data);
        
        sleep(300);
    }
    
    //db_runloop();
    
    return 0;
}
