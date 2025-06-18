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

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

//

static const char *configuration_filepath_default = CONFIGURATION_FILEPATH_DEFAULT;

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
                            *event_db = db_alloc(NULL, &config_doc, node, db_options_no_pam_logging);
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
                   { NULL,              0,                 0,   0  }
               };
static const char *cli_options_str = "hVvqc:";

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
        "\n"
        "  database drivers:\n\n",
        exe,
        configuration_filepath_default);
    while ( (driver_name = db_driver_enumerate_drivers(&driver_iter)) ) printf("    - %s\n", driver_name);
    printf(
        "\n"
        "(v" IPTRACKING_VERSION_STR " built with " CC_VENDOR " %lu on " __DATE__ " " __TIME__ ")\n",
        (unsigned long)CC_VERSION);
}

//

void
firewall_notify(
    db_blocklist_enum_ref   eblocklist,
    const void              *context
)
{
    const char      *s;
    
    while ( (s = db_blocklist_enum_next(eblocklist)) ) {
        printf("%s\n", s);
    }
}

//

int
main(
    int             argc,
    char* const*    argv
)
{
    int                 opt_ch, verbose = 0, quiet = 0;
    const char          *config_filepath = configuration_filepath_default;
    db_ref              the_db = NULL;
    const char          *error_msg = NULL;
    
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
    
    /* Validate configuration: */
    if ( ! config_validate(the_db) ) exit(EINVAL);
    
    /* Open the connection: */
    if ( ! db_open(the_db, &error_msg) ) {
        ERROR("Database: unable to connect to database: %s",
            error_msg ? error_msg : "unknown");
    } else {
        db_blocklist_async_notification_register(the_db, firewall_notify, NULL);
        sleep(120);
        db_close(the_db, NULL);
    }
    
    DEBUG("Terminating.");
    
    return 0;
}
