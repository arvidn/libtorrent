f = open('../include/libtorrent/settings_pack.hpp')

out = open('settings.rst', 'w+')

def print_field(str, width):
	return '%s%s' % (str, ' ' * (width - len(str)))

def render_section(names, description, type, default_values):
	max_name_len = max(len(max(names, key=len)), len('name'))
	max_type_len = max(len(type), len('type'))
	max_val_len = max(len(max(default_values, key=len)), len('default'))

	# add link targets for the rest of the manual to reference
	for n in names:
		print >>out, '.. _%s:\n' % n

	if len(names) > 0:
		print >>out, '.. raw:: html\n'
		for n in names:
			print >>out, '\t<a name="%s"></a>' % n
		print >>out, ''

	separator = '+-' + ('-' * max_name_len) + '-+-' + ('-' * max_type_len) + '-+-' + ('-' * max_val_len) + '-+'

	# build a table for the settings, their type and default value
	print >>out, separator
	print >>out, '| %s | %s | %s |' % (print_field('name', max_name_len), print_field('type', max_type_len), print_field('default', max_val_len))
	print >>out, separator.replace('-', '=')
	for i in range(len(names)):
		print >>out, '| %s | %s | %s |' % (print_field(names[i], max_name_len), print_field(type, max_type_len), print_field(default_values[i], max_val_len))
		print >>out, separator
	print >>out
	print >>out, description

mode = ''

# parse out default values for settings
f2 = open('../src/settings_pack.cpp')
def_map = {}
for l in f2:
	l = l.strip()
	if not l.startswith('SET(') \
		and not l.startswith('SET_NOPREV(') \
		and not l.startswith('DEPRECATED_SET('): continue

	l = l.split('(')[1].split(',')
	def_map[l[0]] = l[1].strip()
	print '%s = %s' % (l[0], l[1].strip())

description = ''
names = []

for l in f:
	if 'enum string_types' in l: mode = 'string'
	if 'enum bool_types' in l: mode = 'bool'
	if 'enum int_types' in l: mode = 'int'
	if '#ifndef TORRENT_NO_DEPRECATE' in l: mode += 'skip'
	if '#endif' in l: mode = mode[0:-4]

	if mode == '': continue
	if mode[-4:] == 'skip': continue

	l = l.lstrip()

	if l == '' and len(names) > 0:
		if description == '':
			for n in names:
				print 'WARNING: no description for "%s"' % n
		else:
			default_values = []
			for n in names:
				default_values.append(def_map[n])
			render_section(names, description, mode, default_values)
		description = ''
		names = []

	if l.startswith('};'):
		mode = ''
		continue

	if l.startswith('// '):
		description += l[3:]
		continue

	l = l.strip()
	if l.endswith(','):
		l = l[:-1] # strip trailing comma
		if '=' in l: l = l.split('=')[0].strip()
		if l.endswith('_internal'): continue

		names.append(l)

out.close()
f.close()

