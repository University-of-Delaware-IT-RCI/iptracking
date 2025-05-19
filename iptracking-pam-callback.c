/*
 * iptracking
 * iptracking-pam-callback.c
 *
 * PAM exec module callback.
 *
 */

#include "iptracking-daemon.h"
#include "log_queue.h"

//

static struct option cli_options[] = {
                   { "help",    no_argument,       0,  'h' },
                   { "fifo",    required_argument, 0,  'p' },
                   { NULL,      0,                 0,   0  }
               };
static const char *cli_options_str = "hp:";

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
        "    -p/--fifo <path>           Path to the fifo the daemon is monitoring\n"
        "                               (default %s)\n"
        "\n"
        "(v" IPTRACKING_VERSION_STR " built with " CC_VENDOR " %lu on " __DATE__ " " __TIME__ ")\n",
        exe,
        FIFO_FILEPATH_DEFAULT,
        (unsigned long)CC_VERSION);
}

//

int
main(
    int             argc,
    char* const*    argv
)
{
    int                 rc = 0, fifo_fd, opt_ch;
    size_t              bytes_ready;
    
    const char          *fifo_filepath = FIFO_FILEPATH_DEFAULT;
    
    const char          *pam_type = getenv("PAM_TYPE");
    log_event_t         event;
    
    const char          *pam_user = getenv("PAM_USER");
    
    time_t              now_t;
    struct tm           now_tm;
    char                now_s[24];
    
    const char          *ssh_connection = getenv("SSH_CONNECTION");
    const char          *ssh_connection_field_start[3],
                        *ssh_connection_field_end[3];
    
    char                out_buffer[1024], *out_buffer_p = out_buffer,
                                          *out_buffer_e = out_buffer_p + sizeof(out_buffer);
    
    /* Parse all CLI arguments: */
    while ( (opt_ch = getopt_long(argc, argv, cli_options_str, cli_options, NULL)) != -1 ) {
        switch ( opt_ch ) {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 'p':
                fifo_filepath = optarg;
                break;
        }
    }
    
    /* We must have gotten values for all fields: */
    if ( !(pam_type && *pam_type) ||
         !(pam_user && *pam_user) ||
         !(ssh_connection && *ssh_connection) ) exit(EINVAL);
    
    /* Resolve the event name to an id: */
    event = log_event_parse_str(pam_type);

    /* Get the timestamp ready: */
    now_t = time(NULL);
    localtime_r(&now_t, &now_tm);
    strftime(now_s, sizeof(now_s), "%Y-%m-%d %H:%M:%S", &now_tm);
         
    /* Isolate the ssh connection string fields: */
    while ( *ssh_connection && isspace(*ssh_connection) ) ssh_connection++;
    if ( !(*ssh_connection) ) exit(EINVAL);
    ssh_connection_field_start[1] = ssh_connection;
    while ( *ssh_connection && ! isspace(*ssh_connection) ) ssh_connection++;
    ssh_connection_field_end[1] = ssh_connection;
    
    while ( *ssh_connection && isspace(*ssh_connection) ) ssh_connection++;
    if ( !(*ssh_connection) ) exit(EINVAL);
    ssh_connection_field_start[2] = ssh_connection;
    while ( *ssh_connection && ! isspace(*ssh_connection) ) ssh_connection++;
    ssh_connection_field_end[2] = ssh_connection;
    
    while ( *ssh_connection && isspace(*ssh_connection) ) ssh_connection++;
    if ( !(*ssh_connection) ) exit(EINVAL);
    ssh_connection_field_start[0] = ssh_connection;
    while ( *ssh_connection && ! isspace(*ssh_connection) ) ssh_connection++;
    ssh_connection_field_end[0] = ssh_connection;
    
    /* Format the event: */
    /* Enough room for the value and a comma? */
    if ( (ssh_connection_field_end[0] - ssh_connection_field_start[0]) >= (2 + out_buffer_e - out_buffer_p) ) exit(ENOMEM);

    memcpy(out_buffer_p, ssh_connection_field_start[0], ssh_connection_field_end[0] - ssh_connection_field_start[0]);
    out_buffer_p += ssh_connection_field_end[0] - ssh_connection_field_start[0];
    *out_buffer_p++ = ',';
    
    /* Enough room for the value and a comma? */
    if ( (ssh_connection_field_end[1] - ssh_connection_field_start[1]) >= (2 + out_buffer_e - out_buffer_p) ) exit(ENOMEM);

    memcpy(out_buffer_p, ssh_connection_field_start[1], ssh_connection_field_end[1] - ssh_connection_field_start[1]);
    out_buffer_p += ssh_connection_field_end[1] - ssh_connection_field_start[1];
    *out_buffer_p++ = ',';
    
    /* Enough room for the value and a comma? */
    if ( (ssh_connection_field_end[1] - ssh_connection_field_start[1]) >= (2 + out_buffer_e - out_buffer_p) ) exit(ENOMEM);

    memcpy(out_buffer_p, ssh_connection_field_start[2], ssh_connection_field_end[2] - ssh_connection_field_start[2]);
    out_buffer_p += ssh_connection_field_end[2] - ssh_connection_field_start[2];
    *out_buffer_p++ = ',';
    
    /* Add the event id, uid, and timestamp: */
    rc = snprintf(out_buffer_p, out_buffer_e - out_buffer_p, "%d,%s,%s", event, pam_user, now_s);
    if ( rc <= 0 ) exit(EINVAL);
    out_buffer_p += rc;
    rc = 0;
    
    /* Open the fifo for writing: */
    fifo_fd = open(fifo_filepath, O_WRONLY);
    if ( fifo_fd < 0 ) exit(errno);
    
    bytes_ready = out_buffer_p - out_buffer;
    out_buffer_p = out_buffer;
    while ( bytes_ready > 0 ) {
        ssize_t bytes_written = write(fifo_fd, out_buffer_p, bytes_ready);
        
        if ( bytes_written < 0 ) {
            if ( errno != EAGAIN ) exit(errno);
        } else if ( bytes_written > 0 ) {
            bytes_ready -= bytes_written;
            out_buffer_p += bytes_written;
        }
    }
    close(fifo_fd);
    
    return 0;
}
