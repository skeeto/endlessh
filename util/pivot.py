#!/usr/bin/env python3

# This script accepts a log on standard input and produces a CSV table
# with one connection per row.
#
#   $ util/pivot.py <log | sqlite3 -init util/schema.sql log.db

import sys
import pyrfc3339

table = {}
for line in sys.stdin:
    parts = line.split(' ')
    entry = {}
    entry['logtime'] = pyrfc3339.parse(parts[0])
    action = parts[1]
    if action == 'ACCEPT' or action == 'CLOSE':
        for item in parts[2:]:
            key, value = item.split('=')
            entry[key] = value
        if action == 'ACCEPT':
            table[entry['fd']] = entry
        else:
            accept = table[entry['fd']]
            del table[entry['fd']]
            delta = (entry['logtime'] - accept['logtime']).total_seconds()
            host = entry['host']
            port = entry['port']
            if host.startswith('::ffff:'):
                host = host[7:]
            nbytes = int(entry['bytes'])
            print('%s,%s,%.3f,%d' % (host, port, delta, nbytes))

if len(table) > 0:
    print('warning: %d hanging entries' % len(table), file=sys.stderr)
