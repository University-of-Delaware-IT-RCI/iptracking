# iptracking

PAM and SSH services on a Linux system typically log authentication and session events through syslog to a file under `/var/log` that gets periodically rotated.  These logs can be shipped periodically for ingest and analysis or syslog can be configured to direct events to external hosts as they occur.

In the former scheme there is a significant delay between the occurrence of events and their availability for analysis.  In the latter, access to the data for the sake of analysis may not be as flexible or even permissible.  Near real-time reaction to events is best implemented on the system itself, and the fastest path to extracting meaning from the events is to place them in a queryable database as they occur.

This project uses the PAM `pg_exec.so` service to execute an external program with user authorization information.  That program writes the data to a named pipe which is monitored by a daemon that logs the event to a database.

## Database 

The daemon can be built with support for multiple database backends.  Regardless of what additional libraries a system may have, a `csvfile` database driver is present that logs events as one-per-line records with an arbitrary delimiter between fields.

Each driver is described below, with its driver_name as the section header.

### csvfile

The configuration mapping can include the following keys:

| Key | Description |
| --- | ----------- |
| `driver_name` | `csvfile` (mandatory for this driver) |
| `filename` | Path to the file to which events will be appended. |
| `delimiter` | The string that will be used to separate each column; defaults to a comma. |

### sqlite3

The configuration mapping can include the following keys:

| Key | Description |
| --- | ----------- |
| `driver_name` | `sqlite3` (mandatory for this driver) |
| `filename` | Path to the SQLite3 database file.  See also `uri` -- the two are mutually exclusive with `uri` as the default. |
| `url` | URI specifying the SQLite3 database file.  See also `filename` -- the two are mutually exclusive with `uri` as the default. |
| `flags` | Contains a sequence of SQLite3 database open flags that should be applied (see below). |

Database open flags are discussed in depth on [this page](https://www.sqlite.org/c3ref/open.html):

| Flag string | SQLite3 constant |
| ----------- | ---------------- |
| `URI` | `SQLITE_OPEN_URI` |
| `MEMORY` | `SQLITE_OPEN_MEMORY` |
| `NOMUTEX` | `SQLITE_OPEN_NOMUTEX` |
| `FULLMUTEX` | `SQLITE_OPEN_FULLMUTEX` |
| `SHAREDCACHE` | `SQLITE_OPEN_SHAREDCACHE` |
| `PRIVATECACHE` | `SQLITE_OPEN_PRIVATECACHE` |
| `NOFOLLOW` | `SQLITE_OPEN_NOFOLLOW` |

The [schema in this repository](psql-sqlite3.schema) is simple and straightforward.  Columns present are:

| Column | Type | Description |
| ------ | ---- | ----------- |
| `dst_ipaddr` | `TEXT` | IP address of the sshd server that logged this event |
| `src_ipaddr` | `TEXT` | IP address of the remote client attempting to connect |
| `src_port` | `INTEGER` | TCP/IP port from which the remote client connected |
| `log_event` | `INTEGER` | `auth`=1, `open_session`=2, `close_session`=3 |
| `uid` | `TEXT` | User identifier attempting to connect |
| `log_date` | `TEXT` | The date and time the event was logged |

### postgresql

The configuration mapping can include the following keys:

| Key | Description |
| --- | ----------- |
| `driver_name` | `postgresql` (mandatory for this driver) |
| `schema` | The PostgreSQL schema name that should prepend all table/view/function names.  By default no schema name is used. |

Additionally, all keywords recognized by the PostgreSQL 17.5 database connection functions are permissible.  See [this page](https://www.postgresql.org/docs/17/libpq-connect.html#LIBPQ-PARAMKEYWORDS) for a list of the keywords with descriptions of their values.

The [schema in this repository](psql-db.schema) is written to be the sole occupant of a database but could be modifed to introduce a namespace for all entities (and the daemon can be built with a default schema name and has the ability to set the schema name at runtime via a CLI flag).

The `inet_log` table contains all event tuples.  Columns present are:

| Column | Type | Description |
| ------ | ---- | ----------- |
| `dst_ipaddr` | `INET` | IP address of the sshd server that logged this event |
| `src_ipaddr` | `INET` | IP address of the remote client attempting to connect |
| `src_port` | `INTEGER` | TCP/IP port from which the remote client connected |
| `log_event` | `log_event_t` | Enumeration of `auth`, `open_session`, `close_session` |
| `uid` | `TEXT` | User identifier attempting to connect |
| `log_date` | `TIMESTAMP` | The date and time the event was logged |

Injection of data is abstracted into a server-side function, `log_one_event()`, so that the table could be restructured without altering the daemon's mechanism (not including addition of more columns provided by the daemon).

Some views are present that summarize the `inet_log` table data in different manners:

| View | Description |
| ---- | ----------- |
| `ips` | Collapses data for combinations of `dst_` and `src_ipaddr` to an event count and time range |
| `nets_24` | Similar to `ips` but replaces the `src_ipaddr` with the IPv4 /24 subnet associated with each address |
| `nets_16` | Similar to `ips` but replaces the `src_ipaddr` with the IPv4 /16 subnet associated with each address |
| `nets_8` | Similar to `ips` but replaces the `src_ipaddr` with the IPv4 /8 subnet associated with each address |
| `agg_ips` | Similar to `ips` but aggregates across all `dst_ipaddr` values |
| `agg_nets_24` | Similar to `nets_24` but aggregates across all `dst_ipaddr` values |
| `agg_nets_16` | Similar to `nets_16` but aggregates across all `dst_ipaddr` values |
| `agg_nets_8` | Similar to `nets_8` but aggregates across all `dst_ipaddr` values |

Two tuple-yielding functions are present that mimic the `nets_*` and `agg_nets_*` views but for an arbitrary IPv4 network prefix:

| Function | Description |
| -------- | ----------- |
| `nets_view(<INTEGER>)` | Yields tuples similar to `ips` but replaces the `src_ipaddr` with the IPv4 subnet (prefix length as the sole argument to the function) associated with each address |
| `agg_nets_view(<INTEGER>)` | Yields tuples similar to `agg_ips` but replaces the `src_ipaddr` with the IPv4 subnet (prefix length as the sole argument to the function) associated with each address |

#### Useful queries

The simplest query of any usefulness is the event count and time range:

```
iptracking=# select count(*), min(log_date) as start_date, max(log_date) as end_date from inet_log;
 count |       start_date       |        end_date        
-------+------------------------+------------------------
 19651 | 2025-05-19 18:10:55-04 | 2025-05-20 13:03:42-04
(1 row)
```

Another useful query is a total event count per unique IP address and the time range of attempts from those IPs:

```
iptracking=# select * from agg_ips order by log_count desc;
     ipaddr      | log_count |       start_date       |        end_date        
-----------------+-----------+------------------------+------------------------
 118.70.81.125   |      6703 | 2025-05-19 20:16:25-04 | 2025-05-20 13:07:04-04
 218.145.31.213  |      5803 | 2025-05-19 20:16:22-04 | 2025-05-20 13:07:02-04
 36.155.130.6    |       945 | 2025-05-19 18:10:58-04 | 2025-05-19 20:38:01-04
 115.190.12.175  |       437 | 2025-05-20 12:07:32-04 | 2025-05-20 12:41:00-04
 196.251.88.103  |       429 | 2025-05-19 22:19:01-04 | 2025-05-20 08:45:33-04
 10.65.0.2       |       404 | 2025-05-19 20:20:05-04 | 2025-05-20 13:00:10-04
 80.94.95.112    |       325 | 2025-05-19 18:37:56-04 | 2025-05-20 13:00:13-04
 134.199.160.158 |       234 | 2025-05-20 06:48:49-04 | 2025-05-20 07:06:37-04
 209.38.83.177   |       234 | 2025-05-20 03:06:42-04 | 2025-05-20 03:25:19-04
 134.199.166.27  |       234 | 2025-05-20 08:10:25-04 | 2025-05-20 08:28:22-04
 37.238.10.118   |       218 | 2025-05-19 18:11:21-04 | 2025-05-19 23:04:48-04
 45.135.232.92   |       194 | 2025-05-19 18:11:39-04 | 2025-05-20 12:55:02-04
    :
```

The top two tuples in that list are of some concern, so what user identifiers are being tried from those addresses?

```
iptracking=# select * from agg_ips order by log_count desc limit 6; select count(*), uid from inet_log where src_ipaddr in ('118.70.81.125', '218.145.31.213') group by uid order by count desc;
     ipaddr      | log_count |       start_date       |        end_date        
-----------------+-----------+------------------------+------------------------
 118.70.81.125   |      6743 | 2025-05-19 20:16:25-04 | 2025-05-20 13:12:37-04
 218.145.31.213  |      5838 | 2025-05-19 20:16:22-04 | 2025-05-20 13:12:42-04
 36.155.130.6    |       945 | 2025-05-19 18:10:58-04 | 2025-05-19 20:38:01-04
 115.190.12.175  |       437 | 2025-05-20 12:07:32-04 | 2025-05-20 12:41:00-04
 196.251.88.103  |       429 | 2025-05-19 22:19:01-04 | 2025-05-20 08:45:33-04
 10.65.0.2       |       408 | 2025-05-19 20:20:05-04 | 2025-05-20 13:10:08-04
(10 rows)

 count |   uid    
-------+----------
  6528 | breakdown
  4479 | apache
  1359 | adm
   215 | shutdown
(4 rows)
```

On average, how fast are events being generated for the `breakdown` uid?

```
iptracking=select avg(delta_t) from (select log_date - lag(log_date) over () as delta_t from inet_log where uid = 'breakdown');
       avg       
-----------------
 00:00:08.911319
(1 row)
```

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

| Option | Default | Description |
| ------ | ------- | ----------- |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN` | 5 | Initial wait period |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX` | 600 | Maximum wait period |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH` | 4 | Begin increasing the wait period after this many initial retries |
| `LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT` | 5 | Increase the wait period by this many seconds |

### Database drivers

The `csvfile` driver is always included in the daemon.

| Option | Default | Description |
| ------ | ------- | ----------- |
| `ENABLE_POSTGRESQL_DRIVER` | Off | Build the PostgreSQL database driver |
| `PostgreSQL_ROOT` | | Prefix path hint for locating the PostgreSQL header/library |
| `ENABLE_SQLITE3_DRIVER` | Off | Build the SQLite3 database driver |
| `SQLite3_ROOT` | | Prefix path hint for locating the SQLite3 header/library |

### CMake build configuration

The CMake infrastructure will look for a pthreads library; a libyaml library; and a PostgreSQL library (version 15 and up).

To influence the locating of the libyaml library, the `libyaml_ROOT` variable can be set to the installation prefix of the library.  Likewise, discovery of the PostgreSQL library can be influenced by setting `PostgreSQL_ROOT`.

Unlike most CMake projects, the `CMAKE_INSTALL_PREFIX` will default to the root directory, not `/usr/local`.  This will place the YAML configuration file in `/etc/iptracking.yml`, the named pipe at `/var/run/iptracking.fifo`, and the executables in `/usr/sbin`.

As an example, consider the `cmake` command used to generate the build system on our Caviness cluster:

```
$ mkdir build ; cd build
$ vpkg_require cmake/3.25.2
$ vpkg_devrequire postgresql/17.5 sqlite3/3.34.1
$ cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_POSTGRESQL_DRIVER=On -DPostgreSQL_ROOT="$POSTGRESQL_PREFIX" \
    -DENABLE_SQLITE3_DRIVER=On -DSQLite3_ROOT="$SQLITE3_PREFIX" \
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

Before the systemd service can be used, the daemon configuration file must be created (we did **not** install it in our CMake configuration above).  In our case, that's `/usr/local/etc/iptracking.yml`.  A description of the daemon configuration file can be found in a later section.

Once that file has been created, the service can be started:

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
    -V/--version               Display program version
    -p/--fifo <path>           Path to the fifo the daemon is monitoring
                               (default /var/run/iptracking.fifo)
    -t/--timeout <int>         Timeout in seconds for open and write to the named pipe
                               (default 5)

(v0.0.1 built with GNU 40805 on May 21 2025 16:25:32)
```

The timeout is present in order to prevent the program from blocking the PAM stack indefinitely, e.g. if the `iptracking-daemon` is not online.

## Daemon configuration file

The configuration is a YAML-formatted file.  Each top-level key in the document is a subsection below.

### database

The `database` key is associated with a mapping of key-value pairs associated with a database driver.  See the **Database** section at the top of this document for more information on this mapping.

### fifo-file

The `fifo-file` key is associated with a string containing the path to the named pipe that the daemon will monitor and to which the PAM helper will write event data.

If omitted, the compiled-in default will be used.

### log-pool

The `log-pool` key is associated with a mapping of two other keys.

#### records

The `log-pool.records` key is associated with a mapping of key-value pairs:

| Key | Description |
| --- | ----------- |
| `min` | The minimum number of event records the daemon will allocate |
| `max` | The maximum number of event records the daemon will allocate; zero (0) implies no limit |
| `delta` | When all event records are in-use and the maximum has not been reached, allocate this many additional records |

The default for `min` and `delta` is 32:  each record is 128 bytes in size, so 32 of them fit in 4 KiB (a typical Linux page of memory).  The `max` defaults to zero (0).

#### push-wait-seconds

The `log-pool.push-wait-seconds` key is associated with a mapping of key-value pairs:

| Key | Description |
| --- | ----------- |
| `min` | The initial (shortest) wait period when all records are in-use |
| `max` | The maximum (longest) wait period when all records are in-use |
| `grow-threshold` | How many retries at the current wait period must fail before the period is incremented |
| `delta` | The number of seconds to increment the wait period |

The `min`, `max`, and `delta` values are all in units of seconds.
