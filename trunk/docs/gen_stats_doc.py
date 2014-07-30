counter_types = {}

f = open('../include/libtorrent/performance_counters.hpp')

counter_type = ''

for l in f:
	l = l.strip()

	if l.startswith('//'): continue
	if l.startswith('#'): continue
	if l == '': continue

	if 'enum stats_counter_t' in l:
		counter_type = 'counter'
		continue

	if 'enum stats_gauges_t' in l:
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
			print 'WARNING: no description for "%s"' % n

	# add link targets for the rest of the manual to reference
	for n in names:
		print >>out, '.. _%s:\n' % n

	if len(names) > 0:
		print >>out, '.. raw:: html\n'
		for n in names:
			print >>out, '\t<a name="%s"></a>' % n
		print >>out, ''

	separator = '+-' + ('-' * max_name_len) + '-+-' + ('-' * max_type_len) + '-+'

	# build a table for the settings, their type and default value
	print >>out, separator
	print >>out, '| %s | %s |' % (print_field('name', max_name_len), print_field('type', max_type_len))
	print >>out, separator.replace('-', '=')
	for i in range(len(names)):
		print >>out, '| %s | %s |' % (print_field(names[i], max_name_len), print_field(types[i], max_type_len))
		print >>out, separator
	print >>out
	print >>out, description
	print >>out, ''

mode = ''

description = ''
names = []
types = []

for l in f:
	l = l.strip()

	if l.startswith('// '):
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

