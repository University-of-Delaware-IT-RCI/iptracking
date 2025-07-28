#!/usr/bin/env python3
#
# Generate report(s) on SSH access to the cluster and perform
# maintenance tasks.
#

import os, sys
import logging
import datetime
import argparse
import errno
import resource
import prettytable
import psycopg
import ldap3

#
# Find the installation root for this script (which is expected to be in the 'bin'
# directory of the venv, for example):
#
VENV_PREFIX = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

#
# Load a config file:
#
CONFIG_FILE = os.path.join(VENV_PREFIX, 'etc', 'iptracking.yml')
if not os.path.isfile(CONFIG_FILE):
    sys.stderr.write(f'CRITICAL ERROR:  no config file at {CONFIG_FILE}\n')
    sys.exit(errno.ENOENT)
try:
    import yaml
    config_fptr = open(CONFIG_FILE)
except Exception as E:
    sys.stderr.write(f'CRITICAL ERROR:  unable to read config file at {CONFIG_FILE}: {E}\n')
    sys.exit(errno.EPERM)
else:
    with config_fptr:
        iptracking_config = yaml.safe_load(config_fptr)
    
    #
    # Cluster name from config:
    #
    if not 'cluster_name' in iptracking_config:
        sys.stderr.write(f'CRITICAL ERROR:  no cluster_name in config file at {CONFIG_FILE}\n')
        sys.exit(EINVAL)
    iptracking_cluster_name = str(iptracking_config['cluster_name'])
    
    #
    # Email configuration properties:
    #
    iptracking_email_subject = f'[{iptracking_cluster_name}] iptracking maintenance and report run'
    iptracking_email_sender = 'root@localhost'
    iptracking_smtp_server = 'localhost'
    if 'email' in iptracking_config:
        iptracking_email_subject = iptracking_config['email'].get('subject',
                        iptracking_email_subject)
        iptracking_email_sender = iptracking_config['email'].get('sender',
                        iptracking_email_sender)
        iptracking_smtp_server = iptracking_config['email'].get('smtp_server',
                        iptracking_smtp_server)
    
    #
    # LDAP configuration properties:
    #
    hpc_ldap_server_hostname = 'localhost'
    hpc_ldap_server_port = 389
    hpc_ldap_base_dn = f''
    if 'ldap' in iptracking_config:
        hpc_ldap_server_hostname = iptracking_config['ldap'].get('hostname',
                        hpc_ldap_server_hostname)
        hpc_ldap_server_port = int(iptracking_config['ldap'].get('port',
                        hpc_ldap_server_port))
        hpc_ldap_base_dn = iptracking_config['ldap'].get('base_dn',
                        hpc_ldap_base_dn)
    
    #
    # Database configuration parameters:
    # [see https://www.postgresql.org/docs/17/libpq-connect.html#LIBPQ-PARAMKEYWORDS]
    #
    iptracking_db_connparams = {
            'dbname': 'iptracking',
            'user': 'iptracking',
            'host': 'XXXXXXXXXXXXXXXXXX',
            'port': '45432',
            'passfile': os.path.join(VENV_PREFIX, 'etc', 'iptracking.passwd')
        }
    if 'database' in iptracking_config:
        for k,v in iptracking_config['database'].items():
            iptracking_db_connparams[k] = str(v)
    
    #
    # Defaults:
    #
    iptracking_default_purge_day_count = 10
    iptracking_default_top_N = 20
    iptracking_default_success_ratio_threshold = 0.05
    if 'defaults' in iptracking_config:
        iptracking_default_purge_day_count = int(iptracking_config['defaults'].get('purge_day_count',
                        iptracking_default_purge_day_count))
        iptracking_default_top_N = int(iptracking_config['defaults'].get('top_N',
                        iptracking_default_top_N))
        iptracking_default_success_ratio_threshold = float(iptracking_config['defaults'].get('success_ratio_threshold',
                        iptracking_default_success_ratio_threshold))

#
# Were we able to create and populate the temp HPC uid table?
#
iptracking_have_hpc_uids_table = False

#
# Table style:
#
iptracking_table_style = prettytable.TableStyle.MARKDOWN



def cursor_to_text_table(db_cursor, alignment=None, sort_by=None, reverse=None):
    """Generate a text-based tabular display of the results in db_cursor.  The optional <alignment> should be a dictionary of column name keys with prettytable alignment strings as values; not every column key needs to be included.  The optional <sort_by> orders the data according to the values in the column indicated by its key; the <reverse> value is a boolean indicating order from least to greatest (False) or greatest to least (True).  The resulting table is returned as an str."""
    table = prettytable.from_db_cursor(db_cursor)
    table.set_style(iptracking_table_style)
    if alignment:
        for k,a in alignment.items():
            table.align[k] = a
    if sort_by:
        if reverse is not None:
            table_str = table.get_string(sortby=sort_by, reversesort=reverse)
        else:
            table_str = table.get_string(sortby=sort_by)
    else:
        table_str = table.get_string()
    return table_str


def results_to_text_table(results, headers, alignment=None, sort_by=None, reverse=None):
    """Generate a text-based tabular display of the results in db_cursor.  The optional <alignment> should be a dictionary of column name keys with prettytable alignment strings as values; not every column key needs to be included.  The optional <sort_by> orders the data according to the values in the column indicated by its key; the <reverse> value is a boolean indicating order from least to greatest (False) or greatest to least (True).  The resulting table is returned as an str."""
    table = prettytable.PrettyTable()
    table.set_style(iptracking_table_style)
    table.field_names = headers
    table.add_rows(results)
    if alignment:
        for k,a in alignment.items():
            table.align[k] = a
    if sort_by:
        if reverse is not None:
            table_str = table.get_string(sortby=sort_by, reversesort=reverse)
        else:
            table_str = table.get_string(sortby=sort_by)
    else:
        table_str = table.get_string()
    return table_str


from ipwhois import IPWhois
try:
    import pycountry
    
    iptracking_have_pycountry = True
    def country_name_for_alpha_2(alpha_2):
        return pycountry.countries.get(alpha_2=alpha_2,
                    default=pycountry.db.Country(name=alpha_2)).name
except:
    iptracking_have_pycountry = False
    
    

class DNSToOrg(object):
    """Caching DNS resolution to CIDR/country via whois."""
    
    def __init__(self):
        self._cache = {}
        self._country_codes = set()
    
    def country_code_table_str(self):
        if iptracking_have_pycountry and len(self._country_codes) > 0:
            rows = [[ALPHA_2, country_name_for_alpha_2(ALPHA_2)] for ALPHA_2 in self._country_codes]
            return results_to_text_table(rows, ('Alpha-2', 'Country name'), alignment={'Alpha-2':'c', 'Country name':'l'}, sort_by='Alpha-2')
        return None
    
    def ip_to_name(self, ipaddr):
        import socket
        
        if ipaddr not in self._cache:
            cache_record = None
            try:
                whois_result = IPWhois(ipaddr)
                if whois_result:
                    rdap_result = whois_result.lookup_rdap()
                    if rdap_result:
                        country_code = rdap_result.get('asn_country_code', '')
                        if country_code:
                            self._country_codes.add(country_code)
                        cache_record = { 'asn_cidr': rdap_result.get('asn_cidr', ''),
                                         'asn_country_code': country_code,
                                         'asn_description': rdap_result.get('asn_description', '') }
                        self._cache[ipaddr] = cache_record
            except:
                pass
        else:
            cache_record = self._cache[ipaddr]
        return cache_record


def load_cluster_uids(db_cursor):
    """Query LDAP for a list of all HPC users and load them into a temporary database table."""
    global      iptracking_have_hpc_uids_table
    
    # Prep the temp table:
    try:
        db_cursor.execute('CREATE TEMP TABLE hpc_uids ( uid TEXT )', prepare=False)
    except Exception as E:
        info_strs.append(f'ERROR:  Failed to create temporary uid table: {E}')
        return
    
    # Connect to the LDAP server
    ldap_server = ldap3.Server(hpc_ldap_server_hostname, port=hpc_ldap_server_port, get_info='NONE')
    with ldap3.Connection(ldap_server, auto_bind=True) as ldap_conn:
        ldap_conn.search(
                search_base=hpc_ldap_base_dn,
                search_filter='(objectClass=alias)',
                search_scope=ldap3.LEVEL,
                dereference_aliases=ldap3.DEREF_NEVER,
                attributes=['uid'])
        found_at_least_one = False
        for ldap_rec in ldap_conn.entries:
            try:
                db_cursor.execute('INSERT INTO hpc_uids VALUES (%s)', (str(ldap_rec.uid),), prepare=True)
                found_at_least_one = True
            except Exception as E:
                info_strs.append(f'ERROR:  Failed to add uid to temporary uid table: {E}')
                return
        iptracking_have_hpc_uids_table = found_at_least_one
    info_strs.append('Temporary HPC uid table created')


class MessageBody():
    """Maintain an ordered list of strings with a standardized header that will be written to stdout or emailed."""
    def __init__(self, cli_args):
        self._start_usage = resource.getrusage(resource.RUSAGE_SELF)
        self._official_timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
        self._info_strs = []
        
        s = f'    - {"dry" if cli_args.is_dry_run else "production"}-run selected\n'
        if cli_args.should_do_maint:
            s += f'    - purge records older than {cli_args.purge_day_count} day(s)\n'
        if cli_args.should_do_reports:
            s += f'    - limit result lists to {cli_args.top_N} record(s)\n'
            s += f'    - open_session:auth ratio < {cli_args.success_ratio_threshold} considered suspect\n'
        
        self._header = '\n\n'.join([
                '\n# iptracking maintenance and reporting run, ' + self._official_timestamp,
                s,
                'The following information was generated during the iptracking-maint run:',
            ])
    def __str__(self):
        end_usage = resource.getrusage(resource.RUSAGE_SELF)
        user_time = end_usage.ru_utime - self._start_usage.ru_utime
        system_time = end_usage.ru_stime - self._start_usage.ru_stime
        usage_str = '\n' + \
                    '## Summary\n\n' + \
                    '    --------------------------------------------------------------------------------\n' + \
                    f'      maintenance and reporting completed in {user_time + system_time:.6f} s\n' + \
                    f'      (user: {user_time:.6f} s | system: {system_time:.6f} s)\n' + \
                    '    --------------------------------------------------------------------------------\n'
        return self._header + '\n\n' + '\n\n'.join(self._info_strs) + '\n\n' + usage_str
    def is_empty(self):
        return len(self._info_strs) <= 0
    def official_timestamp(self):
        return self._official_timestamp
    def append(self, *args):
        for s in args:
            self._info_strs.append(s)


def pre_maintenance(db_cursor, cli_args, dns_helper):
    """Perform database maintenance:  remove logged events older than some time period."""
    #
    # Scrub older records:
    #
    if cli_args.should_do_maint:
        try:
            info_strs.append('## Database cleanup (pre-reporting)')
            #
            # Since we are doing autocommit, we want to put this destructive query
            # inside a transaction that we can rollback if necessary:
            #
            with db_cursor.connection.transaction(force_rollback=cli_args.is_dry_run):
                query_str = f"""
    DELETE FROM pam.inet_log
        WHERE log_date < CURRENT_DATE - '{cli_args.purge_day_count} day'::INTERVAL"""
                db_cursor.execute(query=query_str, prepare=False)
                info_strs.append(f'Removed logged events older than {cli_args.purge_day_count} day(s): {db_cursor.rowcount} tuples.')
        except Exception as E:
            info_strs.append(f'ERROR:  Failed to remove logged events older than {cli_args.purge_day_count} day(s): {E}')


def daily_event_count(db_cursor, cli_args, dns_helper):
    """Summarize event count per day."""
    #
    # Determine daily event counts:
    #
    if cli_args.should_do_reports:
        try:
            info_strs.append('## Daily event counts')
            #
            # Force the query into a transaction that is ALWAYS rolled
            # back to guard against any data change/loss:
            #
            with db_cursor.connection.transaction(force_rollback=True):
                query_str = f"""
    SELECT DATE_TRUNC('day', I.log_date)::DATE AS log_day, D.abbrev AS day_of_week, COUNT(*) AS event_count
        FROM pam.inet_log AS I
        INNER JOIN pam.dow_to_text AS D ON (EXTRACT(DOW FROM I.log_date) = D.dow_value)
        GROUP BY log_day, day_of_week
        ORDER BY log_day ASC"""
                db_cursor.execute(query=query_str, prepare=False)
                info_strs.append(cursor_to_text_table(db_cursor, alignment={'log_day':'l', 'day_of_week': 'c', 'event_count':'r'}))
        except Exception as E:
            info_strs.append(f'ERROR: Failed to produce daily event counts: {E}')


def top_counts(db_cursor, cli_args, dns_helper):
    """Summarize the top sessions granted stats."""
    #
    # Top N by IP across all events:
    #
    if cli_args.should_do_reports:
        #
        # Force the query into a transaction that is ALWAYS rolled
        # back to guard against any data change/loss:
        #
        try:
            with db_cursor.connection.transaction(force_rollback=True):
                try:
                    info_strs.append('## Top IPs by event count')
                    query_str = f"""
SELECT *, (3600*event_count/EXTRACT(EPOCH FROM period))::NUMERIC(8,2) AS avg_per_day FROM (
    SELECT COUNT(*) AS event_count, COUNT(CASE WHEN log_event = 'open_session' THEN 1 END) AS session_count, src_ipaddr, COUNT(DISTINCT uid) AS unique_uids, (MAX(log_date) - MIN(log_date)) AS period
        FROM pam.inet_log
        GROUP BY src_ipaddr
        ORDER BY event_count DESC, session_count DESC
    ) WHERE event_count > 2500
    LIMIT {cli_args.top_N}"""
                    db_cursor.execute(query=query_str, prepare=False)
                    if db_cursor.rowcount <= 0:
                        info_strs.append('No matching data.')
                    else:
                        results = []
                        headers = [c.name for c in db_cursor.description]
                        headers.extend(['org cidr', 'org country', 'org descrip'])
                        #
                        # Try to substitute org CIDR and country code for each IP:
                        #
                        for result in db_cursor:
                            ip_info = dns_helper.ip_to_name(result[2].packed)
                            if ip_info:
                                results.append([*result, ip_info['asn_cidr'], ip_info['asn_country_code'], ip_info['asn_description']])
                            else:
                                results.append([*result, '', '', ''])
                        info_strs.append(results_to_text_table(results, headers, alignment={'event_count':'r', 'session_count':'r', 'src_ipaddr':'l', 'unique_uids': 'r', 'period': 'r', 'avg_per_day': 'r', 'org cidr': 'r', 'org country': 'c', 'org descrip': 'l' }))
                except Exception as E:
                    info_strs.append(f'ERROR:  Failed to produce top IPs by event count: {E}')
                
                #
                # Top N by uid across all events:
                #
                try:
                    info_strs.append('## Top UIDs by event count')
                    query_str = f"""
SELECT *, (3600*event_count/EXTRACT(EPOCH FROM period))::NUMERIC(8,2) AS avg_per_day FROM (
    SELECT uid, COUNT(*) AS event_count, (MAX(log_date) - MIN(log_date)) AS period
        FROM pam.inet_log
        GROUP BY uid
        ORDER BY event_count DESC
    ) WHERE event_count > 2500
    LIMIT {cli_args.top_N}"""
                    db_cursor.execute(query=query_str, prepare=False)
                    info_strs.append(cursor_to_text_table(db_cursor, alignment={'event_count':'r', 'uid':'l', 'period': 'r', 'avg_per_day': 'r' }))
                except Exception as E:
                    info_strs.append(f'ERROR:  Failed to produce top uids by event count: {E}')
                    
                #
                # Top N by IP by session success rate:
                #
                try:
                    info_strs.append('## Top IPs by session success rate')
                    query_str = f"""
SELECT * FROM (SELECT (session_count::REAL/auth_count::REAL) AS success_ratio, * FROM (
    SELECT COUNT(CASE WHEN log_event='open_session' THEN 1 END) AS session_count,
            COUNT(CASE WHEN log_event='auth' THEN 1 END) AS auth_count,
            src_ipaddr, COUNT(DISTINCT uid) AS unique_uids, (MAX(log_date) - MIN(log_date)) AS period
        FROM pam.inet_log
        GROUP BY src_ipaddr
    ) WHERE auth_count > 0 AND session_count > 0 )
        WHERE success_ratio < {cli_args.success_ratio_threshold}
        ORDER BY success_ratio ASC
        LIMIT {cli_args.top_N}"""
                    db_cursor.execute(query=query_str, prepare=False)
                    if db_cursor.rowcount <= 0:
                        info_strs.append('No matching data.')
                    else:
                        results = db_cursor.fetchall()
                        headers = [c.name for c in db_cursor.description]
                        info_strs.append(results_to_text_table(results, headers, alignment={'src_ipaddr': 'l', 'success_ratio':'r', 'session_count':'r', 'auth_count': 'r', 'unique_uids': 'r', 'period': 'r' }))
                        
                        # For each hit, show the uids that were granted sessions:
                        for r in results:
                            db_cursor.execute(query="SELECT DISTINCT uid FROM pam.inet_log WHERE src_ipaddr = %s AND log_event = 'open_session'",
                                    params=[r[3]])
                            info_strs.append(f'    - src_ipaddr "{r[3]}", sessions granted on uids:\n'  + 
                                              '        - ' + \
                                              '\n        - '.join([str(i[0]) for i in db_cursor.fetchall()]))
                except Exception as E:
                    info_strs.append(f'ERROR:  Failed to produce top IPs by session success rate: {E}')
                    
                #
                # Top N by uids by session success rate:
                #
                try:
                    info_strs.append('## Top UIDs by session success rate')
                    query_str = f"""
SELECT * FROM (SELECT (session_count::REAL/auth_count::REAL) AS success_ratio, * FROM (
    SELECT COUNT(CASE WHEN log_event='open_session' THEN 1 END) AS session_count,
            COUNT(CASE WHEN log_event='auth' THEN 1 END) AS auth_count,
            uid, COUNT(DISTINCT src_ipaddr) AS unique_ips, (MAX(log_date) - MIN(log_date)) AS period
        FROM pam.inet_log
        GROUP BY uid
    ) WHERE auth_count > 0 AND session_count > 0 )
        WHERE success_ratio < {cli_args.success_ratio_threshold}
        ORDER BY success_ratio ASC
        LIMIT {cli_args.top_N}"""
                    db_cursor.execute(query=query_str, prepare=False)
                    if db_cursor.rowcount <= 0:
                        info_strs.append('No matching data.')
                    else:
                        results = db_cursor.fetchall()
                        headers = [c.name for c in db_cursor.description]
                        info_strs.append(results_to_text_table(results, headers, alignment={'uid': 'l', 'success_ratio':'r', 'session_count':'r', 'auth_count': 'r', 'unique_ips': 'r', 'period': 'r' }))
                        
                        # For each hit, show the IPs that were granted sessions:
                        for r in results:
                            db_cursor.execute(query="SELECT DISTINCT src_ipaddr FROM pam.inet_log WHERE uid = %s AND log_event = 'open_session'",
                                    params=[r[3]])
                            info_strs.append(f'    - uid "{r[3]}", sessions granted on IPs:\n'  + 
                                              '        - ' + \
                                              '\n        - '.join([str(i[0]) for i in db_cursor.fetchall()]))
                except Exception as E:
                    info_strs.append(f'ERROR:  Failed to produce top uids by session success rate: {E}')
            
                #
                # Top N by "open sessions" from off-campus IPs:
                #
                try:
                    info_strs.append('## Top (possibly) open sessions from foreign IPs')
                    query_str = f"""
SELECT uid, src_ipaddr, count(*) as live_sessions, method
    FROM pam.open_sessions
    WHERE NOT (src_ipaddr << '128.175.0.0/16'::CIDR OR src_ipaddr << '128.4.0.0/16'::CIDR OR
               src_ipaddr << '10.0.0.0/8'::CIDR)
    GROUP BY uid, src_ipaddr, method
    ORDER BY live_sessions DESC
    LIMIT {cli_args.top_N}"""
                    db_cursor.execute(query=query_str, prepare=False)
                    if db_cursor.rowcount <= 0:
                        info_strs.append('No matching data.')
                    else:
                        results = []
                        headers = [c.name for c in db_cursor.description]
                        headers.extend(['org cidr', 'org country', 'org descrip'])
                        #
                        # Try to substitute org CIDR and country code for each IP:
                        #
                        for result in db_cursor:
                            ip_info = dns_helper.ip_to_name(result[1].packed)
                            if ip_info:
                                results.append([*result, ip_info['asn_cidr'], ip_info['asn_country_code'], ip_info['asn_description']])
                            else:
                                results.append([*result, '', '', ''])
                        info_strs.append(results_to_text_table(results, headers, alignment={'uid':'l', 'src_ipaddr': 'l', 'live_sessions': 'r', 'method': 'r', 'org cidr': 'r', 'org country': 'c', 'org descrip': 'l' }))
                except Exception as E:
                    info_strs.append(f'ERROR:  Failed to produce top (possibly) open sessions from foreign IPs: {E}')
                
                if iptracking_have_hpc_uids_table:
                    #
                    # Top N for real users by number of unique IPs
                    #
                    try:
                        info_strs.append('## Top unique IP counts for open_session, actual HPC users')
                        query_str = f"""
SELECT unique_ip_count, array_length(foreign_ips, 1) AS foreign_ip_count, uid, foreign_ips FROM (
    SELECT COUNT(DISTINCT src_ipaddr) AS unique_ip_count,
           uid,
           array_agg(DISTINCT src_ipaddr) FILTER (WHERE NOT (src_ipaddr << '128.175.0.0/16'::CIDR OR src_ipaddr << '128.4.0.0/16'::CIDR OR src_ipaddr << '10.0.0.0/8'::CIDR)) AS foreign_ips
        FROM pam.inet_log
        WHERE uid IN (SELECT uid FROM hpc_uids) AND log_event='open_session'
        GROUP BY uid
    )
    WHERE array_length(foreign_ips, 1) > 0
    ORDER BY (unique_ip_count * array_length(foreign_ips, 1)) DESC, uid ASC
    LIMIT {cli_args.top_N}"""
                        db_cursor.execute(query=query_str, prepare=False)
                        if db_cursor.rowcount <= 0:
                            info_strs.append('No matching data.')
                        else:
                            # We're going to process each tuple to add duplicate rows for the actual IPs:
                            results = []
                            headers = ('unique_ip_count', 'foreign_ip_count', 'uid', 'asn_cidr', 'asn_country_code', 'asn_description', 'foreign_ips')
                            for result in db_cursor:
                                ips = result[3]
                                if len(ips):
                                    row = [result[0], result[1], result[2]]
                                    asns = {}
                                    for ip in ips:
                                        ip_info = dns_helper.ip_to_name(ip.packed)
                                        if ip_info:
                                            if not ip_info['asn_cidr'] in asns:
                                                asns[ip_info['asn_cidr']] = [ip_info['asn_cidr'], ip_info['asn_country_code'], ip_info['asn_description'], [str(ip)]]
                                            else:
                                                asns[ip_info['asn_cidr']][3].append(str(ip))
                                        else:
                                            if not '' in asns:
                                                asns[''] = ['', '', '', [str(ip)]]
                                            else:
                                                asns[''][3].append(str(ip))
                                    for _, asn in asns.items():
                                        row.extend(asn[0:3])
                                        row.append(','.join(asn[3]))
                                        results.append(row)
                                        row = ['', '', '']
                            info_strs.append(results_to_text_table(results, headers, alignment={'unique_ip_count':'r', 'foreign_ip_count': 'r', 'uid':'l', 'asn_cidr':'l', 'asn_country_code':'c', 'asn_description':'l', 'foreign_ips':'l'}))
                    except Exception as E:
                        info_strs.append(f'ERROR:  Failed to produce top unique IP counts for open_session, actual HPC users: {E}')
                
                #
                # Firewall blocks:
                #
                try:
                    info_strs.append('## Active firewall blocks')
                    query_str = f"""SELECT ip_entity FROM firewall.block_now ORDER BY ip_entity"""
                    db_cursor.execute(query=query_str, prepare=False)
                    if db_cursor.rowcount <= 0:
                        info_strs.append('No data.')
                    else:
                        # We're going to process each tuple to add duplicate rows for the actual IPs:
                        results = []
                        headers = ('ip_entity', 'org cidr', 'org country', 'org descrip')
                        #
                        # Try to substitute org CIDR and country code for each IP:
                        #
                        for result in db_cursor:
                            ip_info = dns_helper.ip_to_name(result[0].network_address.packed)
                            if ip_info:
                                results.append([result[0], ip_info['asn_cidr'], ip_info['asn_country_code'], ip_info['asn_description']])
                            else:
                                results.append([result[0], '', '', ''])
                        info_strs.append(results_to_text_table(results, headers, alignment={'ip_entity': 'r', 'org cidr': 'r', 'org country': 'c', 'org descrip': 'l' }))
                    
                except Exception as E:
                    info_strs.append(f'ERROR:  Failed to produce top uids by event count: {E}')
                    
        except Exception as E:
            info_strs.append(f'ERROR:  Failed to open transaction block in top_counts: {E}')


def post_maintenance(db_cursor, cli_args, dns_helper):
    """Perform database maintenance:  vacuum!"""
    #
    # Switch to auto-commit mode and perform the vacuum:
    #
    if cli_args.should_do_maint:  # and not cli_args.is_dry_run
        try:
            info_strs.append('## Database cleanup (post-reporting)')
            db_cursor.execute(query='VACUUM ANALYZE', prepare=False)
            info_strs.append(f'Completed database vacuum and analysis.')
        except Exception as E:
            info_strs.append(f'ERROR:  Failed database vacuum and analysis: {E}')


cli_parser = argparse.ArgumentParser(description='Perform iptracking database maintenance and reporting tasks')
cli_parser.add_argument('-v', '--verbose',
            dest='verbosity',
            action='count',
            default=1,
            help='increase verbosity')
cli_parser.add_argument('-q', '--quiet',
            dest='quietness',
            action='count',
            default=0,
            help='decrease verbosity')
cli_parser.add_argument('-n', '--production-run',
            dest='is_dry_run',
            action='store_false',
            default=True,
            help='allow changes to the database')
cli_parser.add_argument('-m', '--skip-maint',
            dest='should_do_maint',
            action='store_false',
            default=True,
            help='do not perform maintenance tasks')
cli_parser.add_argument('-r', '--skip-reports',
            dest='should_do_reports',
            action='store_false',
            default=True,
            help='do not perform reporting tasks')
cli_parser.add_argument('-p', '--purge-day-count', metavar='<N>',
            dest='purge_day_count',
            type=int,
            default=iptracking_default_purge_day_count,
            help='purge event records older than this many days')
cli_parser.add_argument('-N', '--top-N', metavar='<N>',
            dest='top_N',
            type=int,
            default=iptracking_default_top_N,
            help='limit result lists to this many records')
cli_parser.add_argument('-s', '--success-ratio-thresh', metavar='<N.>',
            dest='success_ratio_threshold',
            type=float,
            default=iptracking_default_success_ratio_threshold,
            help='IPs/UIDs with an open_session:auth ratio less than this value will be considered suspect')
cli_parser.add_argument('-e', '--email', metavar='<email-address>',
            dest='emailAddresses',
            action='append',
            help='send output to one or more email addresses')
cli_args = cli_parser.parse_args()

levels = [ logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO, logging.DEBUG ]
selected_level = max(0, min(len(levels) - 1, cli_args.verbosity - cli_args.quietness))
logging.basicConfig(level=levels[selected_level], format='%(asctime)s [%(levelname)s] %(message)s')

if not ( cli_args.should_do_maint or cli_args.should_do_reports ):
    logging.error('No work to be done.')
    exit(errno.EINVAL)

if cli_args.top_N < 5:
    logging.error('Top N result limit must be at least 5: %d', cli_args.top_N)
    exit(errno.EINVAL)
    
if cli_args.success_ratio_threshold <= sys.float_info.epsilon or (cli_args.success_ratio_threshold - 1.0) > sys.float_info.epsilon:
    logging.error('Success ratio threshold must be in range (0,1]: %.15g', cli_args.success_ratio_threshold)
    exit(errno.EINVAL)

dns_helper = DNSToOrg()

#
# Create the to-do list:
#
to_do_list = [
        pre_maintenance,
        daily_event_count,
        top_counts,
        post_maintenance
    ]

info_strs = MessageBody(cli_args)

#
# Connect to the database:
#
try:
    logging.debug('Connecting to database')
    db_conn = psycopg.connect(
                    conninfo=' '.join(f'{k}={v}' for k,v in iptracking_db_connparams.items()),
                    autocommit=True)
except Exception as E:
    logging.critical(f'Unable to connect to database: {E}')

#
# Do the reporting and maintenance work:
#
try:
    with db_conn.cursor() as db_cursor:
        load_cluster_uids(db_cursor)
        for to_do_item in to_do_list:
            to_do_item(db_cursor, cli_args, dns_helper)
    
    # Add the DNS country code summary if necessary:
    cc_summary = dns_helper.country_code_table_str()
    if cc_summary:
        info_strs.append('## Country codes')
        info_strs.append(cc_summary)
except Exception as E:
    logging.error(f'Failure while executing tasks: {E}')
finally:
    logging.debug('Closing database connection')
    db_conn.close()

#
# Deliver the report:
#
if not info_strs.is_empty():
    message_body = str(info_strs)
    if cli_args.emailAddresses is None:
        print(message_body)
    else:
        import smtplib
        from email.message import Message

        mm = Message()
        mm.set_payload(message_body.encode('utf8'), charset='utf-8')
        mm['Subject'] = iptracking_email_subject + ', ' + info_strs.official_timestamp()
        mm['From'] = iptracking_email_sender
        mm['To'] = ', '.join(cli_args.emailAddresses)

        mta = smtplib.SMTP(iptracking_smtp_server)
        mta.send_message(mm)
        mta.quit()

