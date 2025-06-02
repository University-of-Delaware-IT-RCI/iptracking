# iptracking — maintenance and reporting

With data funneling into an iptracking PostgreSQL database, the `inet_log` table will continually grow unless purged from time to time.  It would also be useful to produce information from the data:  are specific IPs making a high rate of SSH connections, for example.

The script presented here is specific to one of our clusters.  It should be a viable template to get you started, but it's not likely to produce useful data outside of our organization.

## Python virtualenv

The [environment.yml](./environment.yml) file in the repository can be used to reproduce the Python virtual environment in which the maintenance script is run:

```
$ conda env create --prefix=<path-to-virtual-env> --file environment.yml 
```

The [iptracking-maint.py](./iptracking-maint.py) script is copied into the virtualenv `bin` directory, and a PostgreSQL [password file](https://www.postgresql.org/docs/current/libpq-pgpass.html) is created at `etc/iptracking.passwd` (to avoid hard-coding passwords into the script).


## Maintenance

The `maintenance()` function uses a query that reduces the `log_date` field on every `inet_log` tuple to the date (dropping the time from the timestamp) and deleting all tuples older than some arbitrary number of days, *<N>*.  If I were to execute the query on 2025-05-31 with *<N>=10*, any tuple with a `log_date` earlier (less) than (2025-05-31 - 10 days) = 2025-05-21.

The value of *<N>* defaults to 10 but can be altered using the `-p<N>`/`--purge-day-count=<N>` flag.


## Reports

### Daily event count

This report aggregates all events by the date they occurred.  A table providing the name/abbreviation for each day-of-the-week index is used to label each date (making it easier to see periodic trends to connection volume, for example).

| log_day    | day_of_week | event_count |
| :----------| :---------: |-----------: |
| 2025-05-23 |     Fri     |       47958 |
| 2025-05-24 |     Sat     |       37083 |
| 2025-05-25 |     Sun     |       40880 |
| 2025-05-26 |     Mon     |       41211 |
| 2025-05-27 |     Tue     |       50497 |
| 2025-05-28 |     Wed     |       59479 |
| 2025-05-29 |     Thu     |       49601 |
| 2025-05-30 |     Fri     |       58321 |
| 2025-05-31 |     Sat     |       59340 |
| 2025-06-01 |     Sun     |       64307 |
| 2025-06-02 |     Mon     |       36419 |


### Top counts

Non-aggregate queries (e.g. they do not ignore source IP address or UID in the event) fall into this category.  All queries limit the number of results to the top *<N>* tuples returned by the query.

The value of *<N>* defaults to 20 but can be altered using the `-N<N>`/`--top-N=<N>` flag.


#### Top IPs by event count

Events are aggregated across all days by the source IP address.  The query returns:

- the number of events associated with each IP address
- the number of unique UIDs tried from that IP address
- the period of time events have been observed for that IP address
- the average event count per day for that IP address

The WhoIs database is leveraged to turn each IP address into a registered organizational CIDR and country code.  (This works better than doing a reverse lookup on the IP, which often returns no PTR record.)


#### Top UIDs by event count

Events are aggregated across all days by the UID used to connect.  The query returns:

- the number of events associated with each UID
- the number of unique IPs from which that UID connected
- the period of time events have been observed for that UID
- the average event count per day for that UID


#### Top IPs by session success rate

This query attempts to highlight IPs that have made a relatively high number of connection attempts with an extremely low rate of successful session creations:  a brute-force hacking attempt that possibly gained access to the system.  The query returns:

- the number of open_session and auth events associated with each IP address
- the number of unique UIDs tried from that IP address
- the period of time events have been observed for that IP address
- the ratio of open_session:auth events for that IP address

For each returned IP address, the list of unique UIDs attempted is summarized.

The matching threshold defaults to 0.05 open_session:auth events but can be altered using the `-s<N.>`/`--success-ratio-thresh=<N.>` flag.  The value is a floating-point number in the range (0,1].


#### Top UIDs by session success rate

This query attempts to highlight UIDs that have made a relatively high number of connection attempts with an extremely low rate of successful session creations:  a brute-force hacking attempt that possibly gained access to the system.  The query returns:

- the number of open_session and auth events associated with each UID
- the number of unique IP addresses from which the UID was tried
- the period of time events have been observed for that UID
- the ratio of open_session:auth events for that UID

For each returned UID, the list of unique off-campus IP addresses is summarized.

The matching threshold defaults to 0.05 open_session:auth events but can be altered using the `-s<N.>`/`--success-ratio-thresh=<N.>` flag.  The value is a floating-point number in the range (0,1].


#### Top N by "open sessions" from off-campus IPs

For each unique UID, the aggregate number of close_session events are subtracted from the aggregate number of open_session events as a measure of sessions that are still open.  Only events with an off-campus IP address are considered.The query returns:

- the total number of events associated with the UID and IP address
- the (open_session - close_session) event count associated with the UID and IP address

The WhoIs database is leveraged to turn each IP address into a registered organizational CIDR and country code.

