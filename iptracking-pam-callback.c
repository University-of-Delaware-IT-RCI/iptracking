/*
 * iptracking
 * iptracking-pam-callback.c
 *
 * PAM exec module callback.
 *
 */

#include "iptracking-daemon.h"
#include "log_queue.h"

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

//

#define SOCKET_TIMEOUT_DEFAULT   5   /* seconds */

//

static struct option cli_options[] = {
                   { "help",    no_argument,       0,  'h' },
                   { "version", no_argument,       0,  'V' },
                   { "socket",  required_argument, 0,  's' },
                   { "timeout", required_argument, 0,  't' },
                   { NULL,      0,                 0,   0  }
               };
static const char *cli_options_str = "hVs:t:";

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
        "    -V/--version               Display program version\n"
        "    -s/--socket <path>         Path to the socket file the daemon is monitoring\n"
        "                               (default %s)\n"
        "    -t/--timeout <int>         Timeout in seconds for sending data via the socket file\n"
        "                               (default %d)\n"
        "\n"
        "(v" IPTRACKING_VERSION_STR " built with " CC_VENDOR " %lu on " __DATE__ " " __TIME__ ")\n",
        exe,
        SOCKET_FILEPATH_DEFAULT,
        SOCKET_TIMEOUT_DEFAULT,
        (unsigned long)CC_VERSION);
}

//

void
socket_timeout_handler(
    int     signum
)
{
    exit(ETIME);
}

//

extern char **environ;

//

int
main(
    int             argc,
    char* const*    argv
)
{
    int                 rc = 0, client_fd, opt_ch;
    size_t              bytes_ready;
    struct sockaddr_un  server_addr;
    
    const char          *socket_filepath = SOCKET_FILEPATH_DEFAULT;
    int                 socket_timeout = SOCKET_TIMEOUT_DEFAULT;
    
    const char          *pam_type = getenv("PAM_TYPE");
    const char          *pam_user = getenv("PAM_USER");
    
    time_t              now_t;
    struct tm           now_tm;
    
    const char          *ssh_connection = getenv("SSH_CONNECTION");

    log_data_t          data_buffer, *data_buffer_ptr = &data_buffer;
    size_t              data_buffer_len = sizeof(data_buffer);
    
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
            case 's':
                socket_filepath = optarg;
                break;
            case 't': {
                char    *endptr;
                long    i = strtol(optarg, &endptr, 0);
                
                if ( endptr > optarg ) {
                    if ( i < 0 ) i = 0;
                    else if ( i > INT_MAX ) i = INT_MAX;
                    socket_timeout = i;
                } else {
                    fprintf(stderr, "ERROR: invalid timeout: %s\n", optarg);
                    exit(100);
                }
                break;
            }
        }
    }
    
    /* Valid socket filename length: */
    opt_ch = strlen(socket_filepath);
    if ( opt_ch >= sizeof(server_addr.sun_path) ) exit(100);
    
    /* NUL-out the entire data structure: */
    memset(data_buffer_ptr, 0, data_buffer_len);

    /* Get the timestamp ready: */
    now_t = time(NULL);
    localtime_r(&now_t, &now_tm);
    strftime(data_buffer.log_date, sizeof(data_buffer.log_date), "%Y-%m-%d %H:%M:%S", &now_tm);
    
    /* We must have gotten values for all fields: */
    if ( !(pam_type && *pam_type) ) exit(101);
    data_buffer.event = log_event_parse_str(pam_type);
    
    /* If the user is empty just use a sentinel value: */
    strncpy(data_buffer.uid, 
                (pam_user && *pam_user) ? pam_user : "<<EMPTY>>",
                sizeof(data_buffer.uid));
                
    if ( !(ssh_connection && *ssh_connection) ) {
        ssh_connection = getenv("PAM_RHOST");
        if ( !(ssh_connection && *ssh_connection) ) exit(102);
        if ( strlen(ssh_connection) >= sizeof(data_buffer.src_ipaddr) ) exit(103);
        strncpy(data_buffer.src_ipaddr, ssh_connection, sizeof(data_buffer.src_ipaddr));
        strncpy(data_buffer.dst_ipaddr, "0.0.0.0", sizeof(data_buffer.dst_ipaddr));
        data_buffer.src_port = 0;
    } else {
        const char  *p;
        uint16_t    port_val = 0, prev_port_val = 0;
        int         p_len;
        
        /* Isolate the ssh connection string fields */
        
        /* src_ipaddr */
        while ( *ssh_connection && isspace(*ssh_connection) ) ssh_connection++;
        p = ssh_connection, p_len = 0;
        while ( *ssh_connection && ! isspace(*ssh_connection) ) ssh_connection++, p_len++;
        if ( (p_len == 0) || (p_len >= sizeof(data_buffer.src_ipaddr)) ) exit(104);
        memcpy(data_buffer.src_ipaddr, p, p_len);
        
        /* src_port */
        while ( *ssh_connection && isspace(*ssh_connection) ) ssh_connection++;
        while ( *ssh_connection && isdigit(*ssh_connection) ) {
            port_val = port_val * 10 + (*ssh_connection++ - '0');
            if ( port_val < prev_port_val ) exit(105);
        }
        if ( !(*ssh_connection) || ! isspace(*ssh_connection) ) exit(106);
        data_buffer.src_port = port_val;
        
        /* dst_ipaddr */
        while ( *ssh_connection && isspace(*ssh_connection) ) ssh_connection++;
        p = ssh_connection, p_len = 0;
        while ( *ssh_connection && ! isspace(*ssh_connection) ) ssh_connection++, p_len++;
        if ( (p_len == 0) || (p_len >= sizeof(data_buffer.dst_ipaddr)) ) exit(107);
        memcpy(data_buffer.dst_ipaddr, p, p_len);
    }
    
    /* Open the socket: */
    if ( (client_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) exit(108);
    
    /* Setup the address: */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_filepath, sizeof(server_addr.sun_path));
    
    /* Set the timeout alarm: */
    if ( socket_timeout > 0 ) {
        alarm(0);
        signal(SIGALRM, socket_timeout_handler);
        alarm(socket_timeout);
    }
    while ( data_buffer_len > 0 ) {
        if ( connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) >= 0 ) {
            bool        is_connected = true;
            
            while ( is_connected && (data_buffer_len > 0) ) {
                ssize_t     nbytes = send(client_fd, data_buffer_ptr, data_buffer_len, 0);
                
                if ( nbytes < 0 ) {
                    switch ( errno ) {
                        case EINTR:
                        case ENOBUFS:
                            /* Just try again */
                            break;
                        case ECONNRESET:
                            /* Reset and send all over again: */
                            data_buffer_ptr = &data_buffer;
                            data_buffer_len = sizeof(data_buffer);
                            is_connected = false;
                            break;
                        default:
                            fprintf(stderr, "(%d) %s\n", errno, strerror(errno));
                            exit(109);
                    }
                } else if ( nbytes > 0 ) {
                    data_buffer_ptr += nbytes;
                    data_buffer_len -= nbytes;
                }
            }
        }
    }
    if ( socket_timeout > 0 ) alarm(0);
    close(client_fd);
    
    return 0;
}
