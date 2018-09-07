#!/usr/bin/env python
from __future__ import print_function

counter_types = {}

f = open('../include/libtorrent/performance_counters.hpp')

counter_type = ''

for line in f:

    # ignore anything after //
    if '//' in line:
        line = line.split('//')[0]

    line = line.strip()

    if line.startswith('#'):
        continue
    if line == '':
        continue

    if 'enum stats_counter_t' in line:
        counter_type = 'counter'
        continue

    if 'enum stats_gauge_t' in line:
        counter_type = 'gauge'
        continue

    if '{' in line or '}' in line or 'struct' in line or 'namespace' in line:
        continue
    if counter_type == '':
        continue
    if not line.endswith(','):
        continue

    # strip off trailing comma
    line = line[:-1]
    if '=' in line:
        line = line[:line.index('=')].strip()

    counter_types[line] = counter_type

f.close()

f = open('../src/session_stats.cpp')

out = open('stats_counters.rst', 'w+')


def print_field(str, width):
    return '%s%s' % (str, ' ' * (width - len(str)))


def render_section(names, description, types):
    max_name_len = max(len(max(names, key=len)), len('name'))
    max_type_len = max(len(max(types, key=len)), len('type'))

    if description == '':
        for n in names:
            print('WARNING: no description for "%s"' % n)

    # add link targets for the rest of the manual to reference
    for n in names:
        print('.. _%s:\n' % n, file=out)

    if len(names) > 0:
        print('.. raw:: html\n', file=out)
        for n in names:
            print('\t<a name="%s"></a>' % n, file=out)
        print('', file=out)

    separator = '+-' + ('-' * max_name_len) + '-+-' + ('-' * max_type_len) + '-+'

    # build a table for the settings, their type and default value
    print(separator, file=out)
    print('| %s | %s |' % (print_field('name', max_name_len), print_field('type', max_type_len)), file=out)
    print(separator.replace('-', '='), file=out)
    for i in range(len(names)):
        print('| %s | %s |' % (print_field(names[i], max_name_len), print_field(types[i], max_type_len)), file=out)
        print(separator, file=out)
    print(file=out)
    print(description, file=out)
    print('', file=out)


mode = ''

description = ''
names = []
types = []

for line in f:
    description_line = line.lstrip().startswith('//')

    line = line.strip()

    if mode == 'ignore':
        if '#endif' in line:
            mode = ''
        continue

    if 'TORRENT_ABI_VERSION == 1' in line:
        mode = 'ignore'
        continue

    if description_line:
        if len(names) > 0:
            render_section(names, description, types)
            description = ''
            names = []
            types = []

        description += '\n' + line[3:]

    if '#define' in line:
        continue

    if 'METRIC(' in line:
        args = line.split('(')[1].split(')')[0].split(',')

        # args: category, name, type

        args[1] = args[1].strip()
        names.append(args[0].strip() + '.' + args[1].strip())
        types.append(counter_types[args[1]])

if len(names) > 0:
    render_section(names, description, types)

out.close()
f.close()
