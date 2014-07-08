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

		names.append(args[0].strip() + '.' + args[1].strip())

		# strip type_ prefix of type_gauge and type_counter
		args[2] = args[2].strip()[5:]
		types.append(args[2])

if len(names) > 0:
	render_section(names, decsription, types)

out.close()
f.close()

