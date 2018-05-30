#!/usr/bin/env python

counter_types = {}

f = open('../include/libtorrent/performance_counters.hpp')

counter_type = ''

for l in f:

	# ignore anything after //
	if '//' in l: l = l.split('//')[0]

	l = l.strip()

	if l.startswith('#'): continue
	if l == '': continue

	if 'enum stats_counter_t' in l:
		counter_type = 'counter'
		continue

	if 'enum stats_gauge_t' in l:
		counter_type = 'gauge'
		continue

	if '{' in l or '}' in l or 'struct' in l or 'namespace' in l: continue
	if counter_type == '': continue
	if not l.endswith(','): continue

	# strip off trailing comma
	l = l[:-1]
	if '=' in l: l = l[:l.index('=')].strip()

	counter_types[l] = counter_type

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

for l in f:
	description_line = l.lstrip().startswith('//')

	l = l.strip()

	if mode == 'ignore':
		if '#endif' in l: mode = ''
		continue

	if 'TORRENT_ABI_VERSION == 1' in l:
		mode = 'ignore'
		continue

	if description_line == True:
		if len(names) > 0:
			render_section(names, description, types)
			description = ''
			names = []
			types = []

		description += '\n' + l[3:]

	if '#define' in l: continue

	if 'METRIC(' in l:
		args = l.split('(')[1].split(')')[0].split(',')

		# args: category, name, type

		args[1] = args[1].strip()
		names.append(args[0].strip() + '.' + args[1].strip())
		types.append(counter_types[args[1]])

if len(names) > 0:
	render_section(names, description, types)

out.close()
f.close()

