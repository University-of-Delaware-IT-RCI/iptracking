/*
 * iptracking
 * iptracking-firewalld.c
 *
 * Main program.
 *
 */

#include "iptracking.h"
#include "logging.h"
#include "db_interface.h"
#include "yaml_helpers.h"
#include "ipset_helper.h"

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

//

static const char *configuration_filepath_default = CONFIGURATION_FILEPATH_DEFAULT;

//

static uint32_t firewalld_check_interval = FIREWALLD_CHECK_INTERVAL_DEFAULT;
static const char *firewalld_ipset_name_production = FIREWALLD_IPSET_NAME_PRODUCTION_DEFAULT;
static bool firewalld_ipset_name_production_isset = false;
static const char *firewalld_ipset_name_rebuild = FIREWALLD_IPSET_NAME_REBUILD_DEFAULT;
static bool firewalld_ipset_name_rebuild_isset = false;

//

static inline bool
__is_valid_ipset_name(
    const char  *s
)
{
    int         l = 0;
    
    while ( *s && (l <= 256) ) {
        if ( ! isalnum(*s) && (*s != '_') ) return false;
        s++, l++;
    }
    return ((l > 0) && (l <= 256));
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
                    bool            had_prod_name = false;
                    
                    rc = true;
                    while ( 1 ) {
                        /*
                         * Check for any database config items:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, "database")) ) {
                            *event_db = db_alloc(NULL, &config_doc, node, db_options_no_pam_logging);
                        }
                        
                        /*
                         * Check for the firewalld config sub-dict:
                         */
                        if ( (node = yaml_helper_doc_node_at_path(&config_doc, root_node, "firewalld")) ) {
                            yaml_node_t     *firewall_node;
                            
                            /*
                             * Check for the check interval:
                             */
                            if ( (firewall_node = yaml_helper_doc_node_at_path(&config_doc, node, "check-interval")) ) {
                                if ( ! yaml_helper_get_scalar_uint32_value(firewall_node, &firewalld_check_interval) ) {
                                    ERROR("Configuration: invalid check-interval value: %s", yaml_helper_get_scalar_value(firewall_node));
                                    rc = false;
                                    break;
                                }
                            }
                            
                            /*
                             * Check for the production ipset name:
                             */
                            if ( (firewall_node = yaml_helper_doc_node_at_path(&config_doc, node, "ipset-name.production")) ) {
                                const char  *s = yaml_helper_get_scalar_value(firewall_node);
                                
                                if ( ! s ) {
                                    ERROR("Configuration: invalid ipset-name.production value: (empty string)");
                                    rc = false;
                                    break;
                                }
                                firewalld_ipset_name_production = s;
                                firewalld_ipset_name_production_isset = true;
                            }
                            
                            /*
                             * Check for the production ipset name:
                             */
                            if ( (firewall_node = yaml_helper_doc_node_at_path(&config_doc, node, "ipset-name.rebuild")) ) {
                                const char  *s = yaml_helper_get_scalar_value(firewall_node);
                                
                                if ( ! s ) {
                                    ERROR("Configuration: invalid ipset-name.rebuild value: (empty string)");
                                    rc = false;
                                    break;
                                }
                                firewalld_ipset_name_rebuild = s;
                                firewalld_ipset_name_rebuild_isset = true;
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
    
    if (firewalld_check_interval < 120 ) {
        ERROR("Configuration: invalid check-interval value: %lu < 120", firewalld_check_interval);
        return false;
    }
        
    if ( ! __is_valid_ipset_name(firewalld_ipset_name_production) ) {
        ERROR("Configuration: invalid ipset-name.production value:  '%s'", firewalld_ipset_name_production);
        return false;
    }
    if ( ! __is_valid_ipset_name(firewalld_ipset_name_rebuild) ) {
        ERROR("Configuration: invalid ipset-name.rebuild value:  '%s'", firewalld_ipset_name_rebuild);
        return false;
    }
    if ( strcmp(firewalld_ipset_name_rebuild, firewalld_ipset_name_production) == 0 ) {
        ERROR("Configuration: invalid ipset-name.rebuild value: same as production value");
        return false;
    }
    if ( firewalld_ipset_name_production_isset && ! firewalld_ipset_name_rebuild_isset ) {
        // Append "_update" to the production name:
        firewalld_ipset_name_rebuild = NULL;
        asprintf((char**)&firewalld_ipset_name_rebuild, "%s_update", firewalld_ipset_name_production);
    }
    
    INFO("                             check-interval = %lus", firewalld_check_interval);
    INFO("                      ipset-name.production = %s", firewalld_ipset_name_production);
    INFO("                         ipset-name.rebuild = %s", firewalld_ipset_name_rebuild);
    
    db_summarize_to_log(event_db);
    
    return true;
}

//

static struct option cli_options[] = {
                   { "help",                    no_argument,       0,  'h' },
                   { "version",                 no_argument,       0,  'V' },
                   { "verbose",                 no_argument,       0,  'v' },
                   { "quiet",                   no_argument,       0,  'q' },
                   { "config",                  required_argument, 0,  'c' },
                   { "check-interval",          required_argument, 0,  'i' },
                   { "ipset-name-production",   required_argument, 0,  'p' },
                   { "ipset-name-rebuild",      required_argument, 0,  'r' },
                   { NULL,                      0,                 0,   0  }
               };
static const char *cli_options_str = "hVvqc:i:p:r:";

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
        "    -h/--help                          Show this information\n"
        "    -V/--version                       Display daemon version\n"
        "    -v/--verbose                       Increase level of printing\n"
        "    -q/--quiet                         Decrease level of printing\n"
        "    -c/--config <filepath>             Read configuration directives from the YAML file\n"
        "                                       at <filepath> (default: %s)\n"
        "    -i/--check-interval <int>          The maximum number of seconds the daemon will wait\n"
        "                                       between ipset updates (default: %d)\n"
        "    -p/--ipset-name-production <name>  The ipset name to use for the subnet/address set\n"
        "                                       referenced by filter rules (default: %s)\n"
        "    -r/--ipset-name-rebuild <name>     The ipset name to use for the subnet/address set\n"
        "                                       for updates (default: %s)\n"
        "\n"
        "  database drivers:\n\n",
        exe,
        configuration_filepath_default,
        firewalld_check_interval,
        firewalld_ipset_name_production,
        firewalld_ipset_name_rebuild);
    while ( (driver_name = db_driver_enumerate_drivers(&driver_iter)) ) printf("    - %s\n", driver_name);
    printf(
        "\n"
        "(v" IPTRACKING_VERSION_STR " built with " CC_VENDOR " %lu on " __DATE__ " " __TIME__ ")\n",
        (unsigned long)CC_VERSION);
}

//

typedef struct {
    ipset_helper_t  *ipset_helper;
    const char      *ipset_name_prod;
    const char      *ipset_name_rebuild;
} firewall_notify_ctxt_t;

void
firewall_notify(
    db_blocklist_enum_ref   eblocklist,
    const void              *context
)
{
    firewall_notify_ctxt_t  *CONTEXT = (firewall_notify_ctxt_t*)context;
    int                     rc;
    
    rc = ipset_helper_destroy(CONTEXT->ipset_helper, CONTEXT->ipset_name_rebuild);
    // We don't care if this succeeded or not...
    
    rc = ipset_helper_create(CONTEXT->ipset_helper, CONTEXT->ipset_name_rebuild);
    if ( rc == 0 ) {
        // Populate the ipset with the block list:
        const char      *ip_entity;
        
        DEBUG("ipset update:  created ipset '%s'", CONTEXT->ipset_name_rebuild);
        while ( (ip_entity = db_blocklist_enum_next(eblocklist)) ) {
            if ( ip_entity && *ip_entity ) {
                rc = ipset_helper_add(CONTEXT->ipset_helper, CONTEXT->ipset_name_rebuild, ip_entity);
                if ( rc ) {
                    WARN("ipset update:  failed to add '%s' to ipset '%s' (rc = %d): %s", ip_entity, CONTEXT->ipset_name_rebuild, rc, ipset_helper_last_error_message(CONTEXT->ipset_helper));
                } else {
                    DEBUG("ipset update:  added '%s' to ipset '%s'", ip_entity, CONTEXT->ipset_name_rebuild);
                }
            }
        }
        rc = ipset_helper_activate(CONTEXT->ipset_helper, CONTEXT->ipset_name_rebuild, CONTEXT->ipset_name_prod);
        if ( rc == 0 ) {
            DEBUG("ipset update:  successful");
        } else {
            ERROR("ipset update:  failed to activate updated ipset (rc = %d): %s", rc, ipset_helper_last_error_message(CONTEXT->ipset_helper));
        }
    } else {
        ERROR("ipset update:  failed to create rebuild ipset '%s' (rc = %d): %s", CONTEXT->ipset_name_rebuild, rc, ipset_helper_last_error_message(CONTEXT->ipset_helper));
    }
}

//

int
main(
    int             argc,
    char* const*    argv
)
{
    int                     opt_ch, verbose = 0, quiet = 0;
    const char              *config_filepath = configuration_filepath_default;
    db_ref                  the_db = NULL;
    firewall_notify_ctxt_t  firewall_thread_ctxt;
    const char              *error_msg = NULL;
    
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
    if ( ! config_read_yaml_file(config_filepath, &the_db) ) exit(EINVAL);
    
    /* Overrides from the command line: */
    optind = 1;
    while ( (opt_ch = getopt_long(argc, argv, cli_options_str, cli_options, NULL)) != -1 ) {
        switch ( opt_ch ) {
            case 'i': {
                char        *endp = NULL;
                long int    v = strtol(optarg, &endp, 0);
                
                if ( ! (endp > optarg) ) {
                    fprintf(stderr, "ERROR:  invalid -i/--check-interval value: '%s'", optarg);
                    exit(EINVAL);
                }
                firewalld_check_interval = v;
                break;
            }
            case 'p':
                firewalld_ipset_name_production = optarg;
                firewalld_ipset_name_production_isset = true;
                break;
            case 'r':
                firewalld_ipset_name_rebuild = optarg;
                firewalld_ipset_name_rebuild_isset = true;
                break;
        }
    }
    
    /* Validate configuration: */
    if ( ! config_validate(the_db) ) exit(EINVAL);
    
    /* Open the connection: */
    if ( ! db_open(the_db, &error_msg) ) {
        ERROR("Database: unable to connect to database: %s",
            error_msg ? error_msg : "unknown");
    } else {
        /* Connect to ipset facilities: */
        firewall_thread_ctxt.ipset_helper = ipset_helper_init();
        if ( firewall_thread_ctxt.ipset_helper ) {
            firewall_thread_ctxt.ipset_name_prod = firewalld_ipset_name_production;
            firewall_thread_ctxt.ipset_name_rebuild = firewalld_ipset_name_rebuild;
            db_blocklist_async_notification_register(the_db, firewall_notify, &firewall_thread_ctxt, &error_msg);
            sleep(120);
            db_close(the_db, &error_msg);
            
            // Ensure we've dumped the rebuilt list:
            ipset_helper_destroy(firewall_thread_ctxt.ipset_helper, firewall_thread_ctxt.ipset_name_rebuild);
            ipset_helper_fini(firewall_thread_ctxt.ipset_helper);
        }
    }
    
    DEBUG("Terminating.");
    
    return 0;
}
