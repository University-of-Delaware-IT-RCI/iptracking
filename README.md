# iptracking

Infrastructure for tracking PAM sshd events



## Build and install

The package includes CMake build infrastructure.  The following non-standard options are present:

| Option | Default | Description |
| ------ | ------- | ----------- |
| `CONFIGURATION_FILEPATH_DEFAULT` | `<install-prefix>/etc/iptracking.yml` | The location of the daemon's YAML configuration file. |
| `FIFO_FILEPATH_DEFAULT` | `<install-prefix>/var/run/iptracking.fifo` | The location of the named pipe the daemon will read from (and the `pam_exec.so` program will write to) |
| `SHOULD_INSTALL_CONFIG_TEMPLATE` | Off | If on, the `iptracking.yml` file generated during build will be installed during `make install` |
| `SHOULD_INSTALL_SYSTEMD_SERVICE` | Off | If on, the `iptracking-daemon.service` file generated during build will be installed during `make install` |

Logged events are read from the named pipe and added to an in-memory queue to be sent to the database.  The number of available records can vary according to these parameters:

| Option | Default | Description |
| ------ | ------- | ----------- |
| `LOG_POOL_RECORDS_MIN` | 32 | Allocate *at least* this many log pool records to hold events as they are read |
| `LOG_POOL_RECORDS_MAX` | 0 | Allocate *at most* this many log pool records to hold events as they are read (zero implies no limit) |
| `LOG_POOL_RECORDS_DELTA` | 32 | Allocate additional records in batches of this many (each record is 128 bytes, times 32 = 4 KiB) |

When logged events are read from the named pipe, a record must be allocated from the pool to hold the data.  If the pool has reached its record limit and none are available, the process must wait for a record to become available.  Each time this happens the program starts by waiting a base amount of time for some number of tries, then increases the wait time by a fixed value on each subsequent try:

| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN` | 5 | Initial wait period |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX` | 600 | Maximum wait period |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH` | 4 | Begin increasing the wait period after this many initial retries |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT` | 5 | Increase the wait period by this many seconds |

### CMake build configuration

The CMake infrastructure will look for a pthreads library; a libyaml library; and a PostgreSQL library (version 15 and up).

To influence the locating of the libyaml library, the `libyaml_ROOT` variable can be set to the installation prefix of the library.  Likewise, discovery of the PostgreSQL library can be influenced by setting `PostgreSQL_ROOT`.

Unlike most CMake projects, the `CMAKE_INSTALL_PREFIX` will default to the root directory, not `/usr/local`.  This will place the YAML configuration file in `/etc/iptracking.yml`, the named pipe at `/var/run/iptracking.fifo`, and the executables in `/usr/sbin`.

As an example, consider the `cmake` command used to generate the build system on our Caviness cluster:

```
$ mkdir build ; cd build
$ vpkg_devrequire postgresql/17.5
$ cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release \
    -DPostgreSQL_ROOT="$POSTGRESQL_PREFIX" \
    -DSHOULD_INSTALL_SYSTEMD_SERVICE=On \
    -DFIFO_FILEPATH_DEFAULT=/var/run/iptracking.fifo \
    ..
```

### Compile and install

The compilation and installation are straightforward:

```
$ make
$ sudo make install
```

Before the systemd service can be used, the configuration file must be created (we did **not** install it in our CMake configuration above).  In our case, that's `/usr/local/etc/iptracking.yml`.  Once that file has been created, the service can be started:

```
$ sudo systemctl daemon-reload
$ sudo systemctl start iptracking-daemon.service
$ sudo systemctl status iptracking-daemon.service
```

If the service started properly, it can be enabled to start at boot, as well:

```
$ sudo systemctl enable iptracking-daemon.service
```

## PAM configuration

The `pam_exec.so` module executes a program (e.g. a script) with the connection information present in the environment.  That program is responsible for writing the connection information to the named pipe that the `iptracking-daemon` is monitoring.  The events logged correspond with the management group type(s) under which the `pam_exec.so` module and program are registered:

```
#%PAM-1.0
auth       optional     pam_exec.so /usr/local/libexec/iptracking-pam-callback
auth	   required	pam_sepermit.so
auth       substack     password-auth
auth       include      postlogin
# Used with polkit to reauthorize users in remote sessions
-auth      optional     pam_reauthorize.so prepare
account    required     pam_nologin.so
# Uncomment when timed lockouts are necessary:
#account    required     pam_time.so
account    required     pam_rhostfilter.so
account    include      password-auth
password   include      password-auth
# pam_selinux.so close should be the first session rule
session    required     pam_selinux.so close
session    required     pam_loginuid.so
# pam_selinux.so open should only be followed by sessions to be executed in the user context
session    required     pam_selinux.so open env_params
session    required     pam_namespace.so
session    optional     pam_keyinit.so force revoke
session    optional     pam_exec.so /usr/local/libexec/iptracking-pam-callback
session    include      password-auth
session    include      postlogin
# Used with polkit to reauthorize users in remote sessions
-session   optional     pam_reauthorize.so prepare
```

The IP logging will happen for the `auth` and `session` management groups, producing three distinct events:  `auth`, `open_session`, and `close_session`.

The `iptracking-pam-callback` program is a part of this software package.  It is written in C to be as compact and efficient as possible.  There are only two available command line options:

```
$ /usr/local/libexec/iptracking-pam-callback --help
usage:

    /usr/local/libexec/iptracking-pam-callback {options}

  options:

    -h/--help                  Show this information
    -p/--fifo <path>           Path to the fifo the daemon is monitoring
                               (default /var/run/iptracking.fifo)
    -t/--timeout <int>         Timeout in seconds for open and write to the named pipe
                               (default 5)

(v1.2.3 built with GNU 40805 on May 19 2025 15:20:25)
```

The timeout is present in order to prevent the program from blocking the PAM stack indefinitely, e.g. if the `iptracking-daemon` is not online.
