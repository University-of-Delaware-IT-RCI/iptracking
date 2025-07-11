/*
 * iptracking
 * iptracking-pamd.c
 *
 * Main program.
 *
 */

#include "iptracking.h"
#include "logging.h"
#include "log_queue.h"
#include "db_interface.h"
#include "yaml_helpers.h"

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

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

static bool is_running = true;
static const char *socket_filepath = SOCKET_FILEPATH_DEFAULT;
static int socket_backlog = SOCKET_DEFAULT_BACKLOG;
static int socket_poll_interval = SOCKET_DEFAULT_POLL_INTERVAL;

//

typedef struct {
    log_queue_ref   lq;
    db_ref          db;
} thread_context_t;

//

int
db_runloop(
    thread_context_t    *context
)
{
    bool                is_connecting = true;
    const char          *error_msg = NULL;
    
    while ( is_running && ! db_open(context->db, &error_msg) ) {
        /* Try again in 5 seconds: */
        ERROR("Database: unable to connect to database, will retry: %s",
            error_msg ? error_msg : "unknown");
        sleep(5);
    }
    while ( is_running ) {
        log_data_t      data;
        
        /* The log_queue_pop() function will block until a record becomes available: */
        if ( log_queue_pop(&context->lq, &data) ) {
            if ( db_log_one_event(context->db, &data, &error_msg) ) {
                DEBUG("Database: logged data { %s, %s, %s, %ld, %s, %hu, %s }",
                    data.log_date,
                    log_event_to_str(data.event),
                    data.uid,
                    (long int)data.sshd_pid,
                    data.src_ipaddr,
                    data.src_port,
                    data.dst_ipaddr);
            } else {
                ERROR("Database: unable to log data { %s, %s, %s, %ld, %s, %hu, %s }: %s",
                    data.log_date,
                    log_event_to_str(data.event),
                    data.uid,
                   (long int) data.sshd_pid,
                    data.src_ipaddr,
                    data.src_port,
                    data.dst_ipaddr,
                    error_msg ? error_msg : "unknown");
            }
        }
    }
    db_close(context->db, NULL);
    return 0;
}

//

void*
db_thread_entry(
    void    *context
)
{
    thread_context_t    *CONTEXT = (thread_context_t*)context;
    
    while ( is_running )  db_runloop(CONTEXT);
    INFO("Database: exiting runloop");
    return NULL;
}

//

void*
event_thread_entry(
    void    *context
)
{
    thread_context_t    *CONTEXT = (thread_context_t*)context;
    struct sockaddr_un  server_addr;
    log_data_t          data_buffer;
    int                 on = 1, off = 0;
    
    if ( strlen(socket_filepath) >= sizeof(server_addr.sun_path) ) {
        FATAL("Event reader: socket file path is too long (%d >= %d)",
                    strlen(socket_filepath), sizeof(server_addr.sun_path));
    }
    
    while ( is_running ) {
        int             rc, server_fd;
        
        /* Get the socket open: */
        if ( (server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
            ERROR("Event reader: unable to create Unix socket (errno=%d)", errno);
            sleep(5);
            continue;
        }
        DEBUG("Event reader: socket %d created", server_fd);
        if ( setsockopt(server_fd, SOL_SOCKET,  SO_REUSEADDR, (char*)&on, sizeof(on)) < 0 ) {
            WARN("Event reader: unable to set REUSEADDR on socket (errno=%d)", errno);
        }
        DEBUG("Event reader: REUSEADDR set on socket %d", server_fd);
        if ( fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0 ) {
            ERROR("Event reader: unable to set O_NONBLOCK on socket (errno=%d)", errno);
            sleep(5);
            continue;
        }
        DEBUG("Event reader: O_NONBLOCK set on socket %d", server_fd);
        
        /* Bind the socket to the file system: */
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sun_family = AF_UNIX;
        strncpy(server_addr.sun_path, socket_filepath, sizeof(server_addr.sun_path));
        if ( bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            ERROR("Event reader: unable to bind Unix socket to file system (errno=%d)", errno);
            close(server_fd);
            sleep(5);
            continue;
        }
        DEBUG("Event reader: socket %d bound to %s", server_fd, socket_filepath);
        
        /* Start listening for connections: */
        if ( listen(server_fd, socket_backlog) == -1 ) {
            ERROR("Event reader: unable to listen on Unix socket (errno=%d)", errno);
            close(server_fd);
            sleep(5);
            continue;
        }
        DEBUG("Event reader: socket %d listening...", server_fd);
        
        while ( is_running && (server_fd >= 0) ) {
            struct pollfd   server_fds;
            int             client_fd;
            bool            is_polling = true, is_accepting = true;
            
            /* Poll for connections: */
            server_fds.fd = server_fd;
            server_fds.events = POLLIN;
            while ( is_running && is_polling ) {
                switch ( poll(&server_fds, 1, socket_poll_interval) ) {
                    case -1: {
                        switch ( errno ) {
                            case EINTR:
                                break;
                            default:
                                DEBUG("Event reader: poll socket %d failed (errno=%d)", server_fd, errno);
                                is_accepting = false;
                                is_polling = false;
                                break;
                        }
                    }
                    case 0:
                        break;
                    default:
                        if ( (server_fds.revents | POLLIN) == POLLIN ) {
                            is_polling = false;
                            DEBUG("Event reader: connection ready on socket %d", server_fd);
                        }
                        break;
                }
            }
            if ( is_running && is_accepting ) {
                /* Accept the connection: */
                client_fd = accept(server_fd, NULL, NULL);
                if ( client_fd < 0 ) {
                    switch ( errno ) {
                        case ECONNABORTED:
                        case EINTR:
                            /* There are okay, just keep going */
                            break;
                        default:
                            /* All other errors are fatal: */
                            ERROR("Event reader: non-trivial failure during accept (errno=%d)", errno);
                            close(server_fd);
                            server_fd = -1;
                            break;
                    }
                } else {
                    ssize_t nbytes;
                    
                    DEBUG("Event reader: accepted connection");
                    nbytes = recv(client_fd, &data_buffer, sizeof(data_buffer), MSG_WAITALL);
                    DEBUG("Event reader: read %lld bytes", (long long)nbytes);
                    if ( nbytes == sizeof(data_buffer) ) {
                        if ( log_data_is_valid(&data_buffer) ) {
                            log_queue_push(&CONTEXT->lq, &data_buffer);
                        } else {
                            ERROR("Event reader: invalid event read from client");
                        }
                    } else if ( nbytes < 0 ) {
                        ERROR("Event reader: error while reading event from client (errno=%d)", errno);
                    } else {
                        ERROR("Event reader: event was not correct byte size, discarding");
                    }
                    close(client_fd);
                }
            }
            if ( ! is_accepting ) break;
        }
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
    }
    INFO("Event reader: exiting runloop");
    return NULL;
}

//

bool
config_read_yaml_file(
    const char  *fpath,
    db_ref      *event_db
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
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, "database")) ) {
                            *event_db = db_alloc(NULL, &config_doc, node, db_options_no_firewall);
                        }
                        
                        /*
                         * Check for the pamd config sub-dict:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, "pamd")) ) {
                            yaml_node_t     *pam_node;
                            
                            /*
                             * Check for the socket file path:
                             */
                            if ( (pam_node = yaml_helper_doc_node_at_path(&config_doc, node, "socket-file")) ) {
                                const char  *s = yaml_helper_get_scalar_value(pam_node);
                                
                                if ( ! s ) {
                                    ERROR("Configuration: invalid socket-file value");
                                    rc = false;
                                    break;
                                }
                                socket_filepath = s;
                            }
                            /*
                             * Check for any log-pool config items:
                             */
                            if ( (pam_node = yaml_helper_doc_node_at_path(&config_doc, node, "log-pool.records")) ) {
                                yaml_node_t     *val_node;
                                
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "min")) ) {
                                    if ( ! yaml_helper_get_scalar_uint32_value(val_node, &log_pool_records_min) ) {
                                        ERROR("Configuration: invalid log-pool.records.min value");
                                        rc = false;
                                        break;
                                    }
                                }
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "max")) ) {
                                    if ( ! yaml_helper_get_scalar_uint32_value(val_node, &log_pool_records_max) ) {
                                        ERROR("Configuration: invalid log-pool.records.max value");
                                        rc = false;
                                        break;
                                    }
                                }
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "delta")) ) {
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
                            if ( (pam_node = yaml_helper_doc_node_at_path(&config_doc, node, "log-pool.push-wait-seconds")) ) {
                                yaml_node_t     *val_node;
                                
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "min")) ) {
                                    if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_min) ) {
                                        ERROR("Configuration: invalid log-pool.push-wait-seconds.min value");
                                        rc = false;
                                        break;
                                    }
                                }
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "max")) ) {
                                    if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_max) ) {
                                        ERROR("Configuration: invalid log-pool.push-wait-seconds.max value");
                                        rc = false;
                                        break;
                                    }
                                }
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "delta")) ) {
                                    if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_dt) ) {
                                        ERROR("Configuration: invalid log-pool.push-wait-seconds.delta value");
                                        rc = false;
                                        break;
                                    }
                                }
                                if ( (val_node = yaml_helper_doc_node_at_path(&config_doc, pam_node, "grow-threshold")) ) {
                                    if ( ! yaml_helper_get_scalar_int_value(val_node, &log_pool_push_wait_seconds_dt_thresh) ) {
                                        ERROR("Configuration: invalid log-pool.push-wait-seconds.grow-threshold value");
                                        rc = false;
                                        break;
                                    }
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
config_validate(
    db_ref          event_db
)
{
    const char      *error_msg = NULL;
    struct stat     finfo;
    
    /* Check the database config: */
    if ( ! event_db ) {
        ERROR("Configuration: lacks a database configuration");
        return false;
    }
    if ( ! db_has_valid_configuration(event_db, &error_msg) ) {
        ERROR("Configuration: database configuration is invalid: %s", error_msg ? error_msg : "unknown");
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
    
    /* The socket file cannot exist: */
    if ( stat(socket_filepath, &finfo) == 0 ) {
        int     rc = unlink(socket_filepath);
        
        if ( rc != 0 ) {
            ERROR("Configuration: socket file %s exists and could not be removed (errno=%d)", socket_filepath, errno);
            return false;
        }
    }
    
    INFO("                                socket-file = %s", socket_filepath);
    INFO("                                    backlog = %d", socket_backlog);
    INFO("                           polling-interval = %d", socket_poll_interval);
    
    INFO("                       log-pool.records.min = %lu", log_pool_records_min);
    INFO("                       log-pool.records.max = %lu", log_pool_records_max);
    INFO("                     log-pool.records.delta = %lu", log_pool_records_delta);
    
    INFO("             log-pool.push-wait-seconds.min = %lus", log_pool_push_wait_seconds_min);
    INFO("             log-pool.push-wait-seconds.max = %lus", log_pool_push_wait_seconds_max);
    INFO("           log-pool.push-wait-seconds.delta = %lus", log_pool_push_wait_seconds_dt);
    INFO("  log-pool.push-wait-seconds.grow-threshold = %lu", log_pool_push_wait_seconds_dt_thresh);
    
    db_summarize_to_log(event_db);
    
    return true;
}

//

static struct option cli_options[] = {
                   { "help",            no_argument,       0,  'h' },
                   { "version",         no_argument,       0,  'V' },
                   { "verbose",         no_argument,       0,  'v' },
                   { "quiet",           no_argument,       0,  'q' },
                   { "config",          required_argument, 0,  'c' },
                   { "backlog",         required_argument, 0,  'b' },
                   { "poll-interval",   required_argument, 0,  'i' },
                   { NULL,              0,                 0,   0  }
               };
static const char *cli_options_str = "hVvqc:b:i:";

//

void
usage(
    const char  *exe
)
{
    db_driver_iterator_t    driver_iter = NULL;
    const char              *driver_name;
    
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
        "    -b/--backlog <int>         The socket listen backlog (see 'man 3 listen)\n"
        "                               (default: %d, maximum: %d)\n"
        "    -i/--poll-interval <int>   The number of seconds the daemon will block waiting\n"
        "                               on socket connections (default: %d)\n"
        "\n"
        "  defaults:\n\n"
        "    - will read events from socket file %s\n"
        "\n"
        "  database drivers:\n\n",
        exe,
        configuration_filepath_default,
        socket_backlog,
        (int)SOMAXCONN,
        socket_poll_interval,
        socket_filepath);
    while ( (driver_name = db_driver_enumerate_drivers(&driver_iter)) ) printf("    - %s\n", driver_name);
    printf(
        "\n"
        "(v" IPTRACKING_VERSION_STR " built with " CC_VENDOR " %lu on " __DATE__ " " __TIME__ ")\n",
        (unsigned long)CC_VERSION);
}

//

static pthread_mutex_t shutdown_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shutdown_cond = PTHREAD_COND_INITIALIZER;

void*
shutdown_thread_entry(
    void    *context
)
{
    thread_context_t    *CONTEXT = (thread_context_t*)context;
    
    pthread_mutex_lock(&shutdown_mutex);
    INFO("Shutdown: awaiting signal...");
    pthread_cond_wait(&shutdown_cond, &shutdown_mutex);
    INFO("Shutdown: ...received signal.");
    is_running = false;
    log_queue_interrupt_pop(&CONTEXT->lq);
    pthread_mutex_unlock(&shutdown_mutex);
    return NULL;
}

//

void
handle_termination(
    int     signum
)
{
    pthread_mutex_lock(&shutdown_mutex);
    pthread_cond_broadcast(&shutdown_cond);
    pthread_mutex_unlock(&shutdown_mutex);
}

//

int
main(
    int             argc,
    char* const*    argv
)
{
    pthread_t           db_thread, event_thread, shutdown_thread;
    thread_context_t    tc;
    int                 opt_ch, verbose = 0, quiet = 0;
    const char          *config_filepath = configuration_filepath_default;
    struct sigaction    signal_spec;
    log_queue_params_t  lq_params;
    
    /* Block all "other" permissions: */
    umask(007);
    
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
                /* Set logging level based on verbose/quiet options: */
                logging_set_level(logging_get_level() + ++verbose - quiet);
                break;
            case 'q':
                /* Set logging level based on verbose/quiet options: */
                logging_set_level(logging_get_level() + verbose - ++quiet);
                break;
            case 'c':
                config_filepath = optarg;
                break;
        }
    }
    
    /* Load configuration: */
    if ( ! config_read_yaml_file(config_filepath, &tc.db) ) exit(EINVAL);
    
    /* Overrides from CLI: */
    optind = 1;
    while ( (opt_ch = getopt_long(argc, argv, cli_options_str, cli_options, NULL)) != -1 ) {
        switch ( opt_ch ) {
            case 'b': {
                char    *endptr;
                long    ival = strtol(optarg, &endptr, 0);
                
                if ( (endptr == optarg) || (ival < 0) || (ival > SOMAXCONN) ) {
                    ERROR("Invalid backlog value: %s", optarg);
                    exit(EINVAL);
                }
                socket_backlog = ival;
                break;
            }
            case 'i': {
                char    *endptr;
                long    ival = strtol(optarg, &endptr, 0);
                
                if ( (endptr == optarg) || (ival < 0) || (ival > INT_MAX) ) {
                    ERROR("Invalid polling interval value: %s", optarg);
                    exit(EINVAL);
                }
                socket_poll_interval = ival;
                break;
            }
        }
    }
    
    /* Validate configuration: */
    if ( ! config_validate(tc.db) ) exit(EINVAL);
    
    /* Initialize the log queue parameters: */
    lq_params.records.min = log_pool_records_min;
    lq_params.records.max = log_pool_records_max;
    lq_params.records.delta = log_pool_records_delta;
    
    lq_params.push_wait_seconds.min = log_pool_push_wait_seconds_min;
    lq_params.push_wait_seconds.max = log_pool_push_wait_seconds_max;
    lq_params.push_wait_seconds.delta = log_pool_push_wait_seconds_dt;
    lq_params.push_wait_seconds.grow_threshold = log_pool_push_wait_seconds_dt_thresh;
    
    /* Create the log queue: */
    if ( (tc.lq = log_queue_create(&lq_params)) == NULL ) {
        ERROR("Unable to create log queue");
    } else {
        /* Register signal handlers: */
        signal_spec.sa_handler = handle_termination;
        sigemptyset(&signal_spec.sa_mask);
        signal_spec.sa_flags = 0;
        sigaction(SIGHUP, &signal_spec, NULL);
        sigaction(SIGINT, &signal_spec, NULL);
        sigaction(SIGTERM, &signal_spec, NULL);
        
        /* Spawn the database consumer thread: */
        pthread_create(&db_thread, NULL, db_thread_entry, (void*)&tc);
        
        /* Spawn the event consumer thread: */
        pthread_create(&event_thread, NULL, event_thread_entry, (void*)&tc);
        
        /* Spawn the shutdown thread: */
        pthread_create(&shutdown_thread, NULL, shutdown_thread_entry, (void*)&tc);
        
        /* Wait for termination: */
        pthread_join(db_thread, NULL);
        pthread_join(event_thread, NULL);
        pthread_join(shutdown_thread, NULL);
        
        if ( unlink(socket_filepath) < 0 ) {
            ERROR("Failed to remove socket file %s (errno=%d)", socket_filepath, errno);
        } else {
            DEBUG("Removed socket file %s", socket_filepath);
        }
    }
    db_dealloc(tc.db);
    log_queue_destroy(&tc.lq);
    DEBUG("Terminating.");
    
    return 0;
}
