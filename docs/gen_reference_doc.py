import glob
import os
import sys

verbose = '--verbose' in sys.argv
dump = '--dump' in sys.argv
internal = '--internal' in sys.argv

paths = ['include/libtorrent/*.hpp', 'include/libtorrent/kademlia/*.hpp', 'include/libtorrent/extensions/*.hpp']

if internal:
	paths.append('include/libtorrent/aux_/*.hpp')

files = []

for p in paths:
	files.extend(glob.glob(os.path.join('..', p)))

functions = []
classes = []
enums = []

# maps filename to overview description
overviews = {}

# maps names -> URL
symbols = {}

# some files that need pre-processing to turn symbols into
# links into the reference documentation
preprocess_rst = \
{
	'manual.rst':'manual-ref.rst',
}

# some pre-defined sections from the main manual
symbols = \
{
	"queuing_": "manual-ref.html#queuing",
	"fast-resume_": "manual-ref.html#fast-resume",
	"storage-allocation_": "manual-ref.html#storage-allocation",
	"alerts_": "manual-ref.html#alerts",
	"upnp-and-nat-pmp_": "manual-ref.html#upnp-and-nat-pmp",
	"http-seeding_": "manual-ref.html#http-seeding",
	"metadata-from-peers_": "manual-ref.html#metadata-from-peers",
	"magnet-links_": "manual-ref.html#magnet-links",
	"ssl-torrents_": "manual-ref.html#ssl-torrents",
}

static_links = \
{
	".. _`BEP 3`: http://bittorrent.org/beps/bep_0003.html",
	".. _`BEP 17`: http://bittorrent.org/beps/bep_0017.html",
	".. _`BEP 19`: http://bittorrent.org/beps/bep_0019.html"
}

anon_index = 0

category_mapping = {
	'session.hpp': 'Session',
	'add_torrent_params.hpp': 'Session',
	'session_status.hpp': 'Session',
	'error_code.hpp': 'Error Codes',
	'file.hpp': 'File',
	'storage.hpp': 'Custom Storage',
	'storage_defs.hpp': 'Storage',
	'file_storage.hpp': 'Storage',
	'file_pool.hpp': 'Custom Storage',
	'extensions.hpp': 'Plugins',
	'ut_metadata.hpp': 'Plugins',
	'ut_pex.hpp': 'Plugins',
	'ut_trackers.hpp': 'Plugins',
	'metadata_transfer.hpp': 'Plugins',
	'smart_ban.hpp': 'Plugins',
	'lt_trackers.hpp': 'Plugins',
	'create_torrent.hpp': 'Create Torrents',
	'alert.hpp': 'Alerts',
	'alert_types.hpp': 'Alerts',
	'bencode.hpp': 'Bencoding',
	'lazy_entry.hpp': 'Bencoding',
	'entry.hpp': 'Bencoding',
	'time.hpp': 'Time',
	'ptime.hpp': 'Time',
	'escape_string.hpp': 'String',
	'string_util.hpp': 'String',
	'utf8.hpp': 'String',
	'enum_net.hpp': 'Network',
	'broadcast_socket.hpp': 'Network',
	'socket.hpp': 'Network',
	'socket_io.hpp': 'Network',
	'rss.hpp': 'RSS',
	'bitfield.hpp': 'Utility',
	'sha1_hash.hpp': 'Utility',
	'identify_client.hpp': 'Utility',
	'thread.hpp': 'Utility',
	'ip_filter.hpp': 'Filter',
	'session_settings.hpp': 'Settings',
}

category_fun_mapping = {
	'min_memory_usage()': 'Settings',
	'high_performance_seed()': 'Settings',
	'cache_status': 'Session',
}

def categorize_symbol(name, filename):
	f = os.path.split(filename)[1]

	if name.endswith('_category()') \
		or name.endswith('_error_code') \
		or name.endswith('error_code_enum'):
		return 'Error Codes'

	if name in category_fun_mapping:
		return category_fun_mapping[name]

	if f in category_mapping:
		return category_mapping[f]

	return 'Core'

def suppress_warning(filename, name):
	f = os.path.split(filename)[1]
	if f != 'alert_types.hpp': return False

#	if name.endswith('_alert') or name == 'message()':
	return True

#	return False

def first_item(itr):
	for i in itr:
		return i
	return None

def is_visible(desc):
	if desc.strip().startswith('hidden'): return False
	if internal: return True
	if desc.strip().startswith('internal'): return False
	return True

def highlight_signature(s):
	name = s.split('(')
	name2 = name[0].split(' ')
	if len(name2[-1]) == 0: return s

	# make the name of the function bold
	name2[-1] = '**' + name2[-1] + '** '

	# if there is a return value, make sure we preserve pointer types
	if len(name2) > 1:
		name2[0] = name2[0].replace('*', '\\*')
	name[0] = ' '.join(name2)

	# we have to escape asterisks, since this is rendered into
	# a parsed literal in rst
	name[1] = name[1].replace('*', '\\*')

	# comments in signatures are italic
	name[1] = name[1].replace('/\\*', '*/\\*')
	name[1] = name[1].replace('\\*/', '\\*/*')
	return '('.join(name)

def html_sanitize(s):
	ret = ''
	for i in s:
		if i == '<': ret += '&lt;'
		elif i == '>': ret += '&gt;'
		elif i == '&': ret += '&amp;'
		else: ret += i
	return ret

def looks_like_variable(line):
	line = line.strip()
	if not line.endswith(';'): return False
	if not ' ' in line and not '\t' in line: return False
	if line.startswith('friend '): return False
	if line.startswith('enum '): return False
	if line.startswith(','): return False
	if line.startswith(':'): return False
	if line.startswith('typedef'): return False
	return True

def looks_like_function(line):
	if line.startswith('friend'): return False
	if '::' in line.split('(')[0].split(' ')[-1]: return False
	if line.startswith(','): return False
	if line.startswith(':'): return False
	return '(' in line;

def parse_function(lno, lines, filename):
	current_fun = {}

	start_paren = 0
	end_paren = 0
	signature = ''

	while lno < len(lines):
		l = lines[lno].strip()
		lno += 1
		if l.startswith('//'): continue

		start_paren += l.count('(')
		end_paren += l.count(')')

		sig_line = l.replace('TORRENT_EXPORT ', '').replace('TORRENT_EXTRA_EXPORT','').strip()
		if signature != '': sig_line = '\n   ' + sig_line
		signature += sig_line
		if verbose: print 'fun     %s' % l

		if start_paren > 0 and start_paren == end_paren:
			if signature[-1] != ';':
				# we also need to consume the function body
				start_paren = 0
				end_paren = 0
				for i in range(len(signature)):
					if signature[i] == '(': start_paren += 1
					elif signature[i] == ')': end_paren += 1

					if start_paren > 0 and start_paren == end_paren:
						for k in range(i, len(signature)):
							if signature[k] == ':' or signature[k] == '{':
								signature = signature[0:k].strip()
								break
						break

				lno = consume_block(lno - 1, lines)
				signature += ';'
			ret = [{ 'file': filename[11:], 'signatures': set([ signature ]), 'names': set([ signature.split('(')[0].split(' ')[-1].strip() + '()'])}, lno]
			if first_item(ret[0]['names']) == '()': return [None, lno]
			return ret
	if len(signature) > 0:
		print '\x1b[31mFAILED TO PARSE FUNCTION\x1b[0m %s\nline: %d\nfile: %s' % (signature, lno, filename)
	return [None, lno]

def parse_class(lno, lines, filename):
	start_brace = 0
	end_brace = 0

	name = ''
	funs = []
	fields = []
	enums = []
	state = 'public'
	context = ''
	class_type = 'struct'
	blanks = 0
	decl = ''

	while lno < len(lines):
		l = lines[lno].strip()
		decl += lines[lno].replace('TORRENT_EXPORT ', '').replace('TORRENT_EXTRA_EXPORT', '').split('{')[0].strip()
		if '{' in l: break
		if verbose: print 'class  %s' % l
		lno += 1

	if decl.startswith('class'):
		state = 'private'
		class_type = 'class'

	name = decl.split(':')[0].replace('class ', '').replace('struct ', '').strip()

	while lno < len(lines):
		l = lines[lno].strip()
		lno += 1

		if l == '':
			blanks += 1
			context = ''
			continue

		if l.startswith('/*'):
			lno = consume_comment(lno - 1, lines)
			continue

		if l.startswith('#'):
			lno = consume_ifdef(lno - 1, lines, True)
			continue

		if 'TORRENT_DEFINE_ALERT' in l:
			if verbose: print 'xx    %s' % l
			blanks += 1
			continue
		if 'TORRENT_DEPRECATED' in l:
			if verbose: print 'xx    %s' % l
			blanks += 1
			continue

		if l.startswith('//'):
			if verbose: print 'desc  %s' % l
			l = l[2:]
			if len(l) and l[0] == ' ': l = l[1:]
			context += l + '\n'
			continue

		start_brace += l.count('{')
		end_brace += l.count('}')

		if l == 'private:': state = 'private'
		elif l == 'protected:': state = 'protected'
		elif l == 'public:': state = 'public'

		if start_brace > 0 and start_brace == end_brace:
			return [{ 'file': filename[11:], 'enums': enums, 'fields':fields, 'type': class_type, 'name': name, 'decl': decl, 'fun': funs}, lno]

		if state != 'public' and not internal:
			if verbose: print 'private %s' % l
			blanks += 1
			continue

		if start_brace - end_brace > 1:
			if verbose: print 'scope   %s' % l
			blanks += 1
			continue;

		if looks_like_function(l):
			current_fun, lno = parse_function(lno - 1, lines, filename)
			if current_fun != None and is_visible(context):
				if context == '' and blanks == 0 and len(funs):
					funs[-1]['signatures'].update(current_fun['signatures'])
					funs[-1]['names'].update(current_fun['names'])
				else:
					current_fun['desc'] = context
					if context == '' and not suppress_warning(filename, first_item(current_fun['names'])):
						print 'WARNING: member function "%s" is not documented: \x1b[34m%s:%d\x1b[0m' \
							% (name + '::' + first_item(current_fun['names']), filename, lno)
					funs.append(current_fun)
				context = ''
				blanks = 0
			continue

		if looks_like_variable(l):
			if not is_visible(context):
				continue
			n = l.split(' ')[-1].split(':')[0].split(';')[0]
			if context == '' and blanks == 0 and len(fields):
				fields[-1]['names'].append(n)
				fields[-1]['signatures'].append(l)
			else:
				if context == '' and not suppress_warning(filename, n):
					print 'WARNING: field "%s" is not documented: \x1b[34m%s:%d\x1b[0m' \
						% (name + '::' + n, filename, lno)
				fields.append({'signatures': [l], 'names': [n], 'desc': context})
			context = ''
			blanks = 0
			continue

		if l.startswith('enum '):
			if not is_visible(context):
				consume_block(lno - 1, lines)
			else:
				enum, lno = parse_enum(lno - 1, lines, filename)
				if enum != None:
					enum['desc'] = context
					if context == '' and not suppress_warning(filename, enum['name']):
						print 'WARNING: enum "%s" is not documented: \x1b[34m%s:%d\x1b[0m' \
							% (name + '::' + enum['name'], filename, lno)
					enums.append(enum)
				context = ''
			continue

		context = ''
		if verbose: print '??      %s' % l
   
	if len(name) > 0:
		print '\x1b[31mFAILED TO PARSE CLASS\x1b[0m %s\nfile: %s:%d' % (name, filename, lno)
	return [None, lno]

def parse_enum(lno, lines, filename):
	start_brace = 0
	end_brace = 0
	global anon_index

	l = lines[lno].strip()
	name = l.replace('enum ', '').split('{')[0].strip()
	if len(name) == 0:
		if not internal:
			print 'WARNING: anonymous enum at: \x1b[34m%s:%d\x1b[0m' % (filename, lno)
			lno = consume_block(lno - 1, lines)
			return [None, lno]
		name = 'anonymous_enum_%d' % anon_index
		anon_index += 1

	values = []
	context = ''
	if not '{' in l:
		if verbose: print 'enum  %s' % lines[lno]
		lno += 1

	val = 0
	while lno < len(lines):
		l = lines[lno].strip()
		lno += 1

		if l.startswith('//'):
			if verbose: print 'desc  %s' % l
			l = l[2:]
			if len(l) and l[0] == ' ': l = l[1:]
			context += l + '\n'
			continue

		if l.startswith('#'):
			lno = consume_ifdef(lno - 1, lines)
			continue

		start_brace += l.count('{')
		end_brace += l.count('}')

		if '{' in l: 
			l = l.split('{')[1]
		l = l.split('}')[0]

		if len(l):
			if verbose: print 'enumv %s' % lines[lno-1]
			for v in l.split(','):
				v = v.strip();
				if v.startswith('//'): break
				if v == '': continue
				valstr = ''
				try:
					if '=' in v: val = int(v.split('=')[1].strip(), 0)
					valstr = str(val)
				except: pass

				if '=' in v: v = v.split('=')[0].strip()
				if is_visible(context):
					values.append({'name': v.strip(), 'desc': context, 'val': valstr})
					if verbose: print 'enumv %s' % valstr
				context = ''
				val += 1
		else:
			if verbose: print '??    %s' % lines[lno-1]

		if start_brace > 0 and start_brace == end_brace:
			return [{'file': filename[11:], 'name': name, 'values': values}, lno]

	if len(name) > 0:
		print '\x1b[31mFAILED TO PARSE ENUM\x1b[0m %s\nline: %d\nfile: %s' % (name, lno, filename)
	return [None, lno]

def consume_block(lno, lines):
	start_brace = 0
	end_brace = 0

	while lno < len(lines):
		l = lines[lno].strip()
		if verbose: print 'xx    %s' % l
		lno += 1

		start_brace += l.count('{')
		end_brace += l.count('}')

		if start_brace > 0 and start_brace == end_brace:
			break
	return lno

def consume_comment(lno, lines):
	while lno < len(lines):
		l = lines[lno].strip()
		if verbose: print 'xx    %s' % l
		lno += 1
		if '*/' in l: break

	return lno

def trim_define(l):
	return l.replace('#ifndef', '').replace('#ifdef', '') \
		.replace('#if', '').replace('defined', '') \
		.replace('TORRENT_USE_IPV6', '').replace('TORRENT_NO_DEPRECATE', '') \
		.replace('||', '').replace('&&', '').replace('(', '').replace(')','') \
		.replace('!', '').replace('\\', '').strip()

def consume_ifdef(lno, lines, warn_on_ifdefs = False):
	l = lines[lno].strip()
	lno += 1

	start_if = 1
	end_if = 0

	if verbose: print 'prep  %s' % l

	if warn_on_ifdefs and ('TORRENT_DEBUG' in l or 'TORRENT_DISABLE_FULL_STATS' in l):
		while l.endswith('\\'):
			lno += 1
			l += lines[lno].strip()
			if verbose: print 'prep  %s' % lines[lno].trim()
		define = trim_define(l)
		print '\x1b[31mWARNING: possible ABI breakage in public struct! "%s" \x1b[34m %s:%d\x1b[0m' % \
			(define, filename, lno)
		# we've already warned once, no need to do it twice
		warn_on_ifdefs = False

	if warn_on_ifdefs and '#if' in l:
		while l.endswith('\\'):
			lno += 1
			l += lines[lno].strip()
			if verbose: print 'prep  %s' % lines[lno].trim()
		define = trim_define(l)
		if define != '':
			print '\x1b[33msensitive define in public struct: "%s"\x1b[34m %s:%d\x1b[0m' % (define, filename, lno)

	if l == '#ifndef TORRENT_NO_DEPRECATE' or \
		l == '#ifdef TORRENT_DEBUG' or \
		(l.startswith('#if ') and ' TORRENT_USE_ASSERTS' in l) or \
		(l.startswith('#if ') and ' TORRENT_USE_INVARIANT_CHECKS' in l) or \
		l == '#ifdef TORRENT_ASIO_DEBUGGING' or \
		(l.startswith('#if') and 'defined TORRENT_DEBUG' in l) or \
		(l.startswith('#if') and 'defined TORRENT_ASIO_DEBUGGING' in l):
		while lno < len(lines):
			l = lines[lno].strip()
			lno += 1
			if verbose: print 'prep  %s' % l
			if l.startswith('#endif'): end_if += 1
			if l.startswith('#if'): start_if += 1
			if l == '#else' and start_if - end_if == 1: break
			if start_if - end_if == 0: break
		return lno
	else:
		while l.endswith('\\') and lno < len(lines):
			l = lines[lno].strip()
			lno += 1
			if verbose: print 'prep  %s' % l

	return lno

for filename in files:
	h = open(filename)
	lines = h.read().split('\n')

	if verbose: print '\n=== %s ===\n' % filename

	blanks = 0
	lno = 0
	while lno < len(lines):
		l = lines[lno].strip()
		lno += 1

		if l == '':
			blanks += 1
			context = ''
			continue

		if l.startswith('//') and l[2:].strip() == 'OVERVIEW':
			# this is a section overview
			current_overview = ''
			while lno < len(lines):
				l = lines[lno].strip()
				lno += 1
				if not l.startswith('//'):
					# end of overview
					overviews[filename[11:]] = current_overview
					current_overview = ''
					break
				l = l[2:]
				if l.startswith(' '): l = l[1:]
				current_overview += l + '\n'

		if l.startswith('//'):
			if verbose: print 'desc  %s' % l
			l = l[2:]
			if len(l) and l[0] == ' ': l = l[1:]
			context += l + '\n'
			continue

		if l.startswith('/*'):
			lno = consume_comment(lno - 1, lines)
			continue

		if l.startswith('#'):
			lno = consume_ifdef(lno - 1, lines)
			continue

		if (l == 'namespace detail' or \
			l == 'namespace aux') \
			and not internal:
			lno = consume_block(lno, lines)
			continue

		if 'TORRENT_CFG' in l:
			blanks += 1
			if verbose: print 'xx    %s' % l
			continue
		if 'TORRENT_DEPRECATED' in l:
			if ('class ' in l or 'struct ' in l) and not ';' in l:
				lno = consume_block(lno - 1, lines)
				context = ''
			blanks += 1
			if verbose: print 'xx    %s' % l
			continue

		if 'TORRENT_EXPORT ' in l or l.startswith('inline ') or l.startswith('template') or internal:
			if l.startswith('class ') or l.startswith('struct '):
				if not l.endswith(';'):
					current_class, lno = parse_class(lno -1, lines, filename)
					if current_class != None and is_visible(context):
						current_class['desc'] = context
						if context == '':
							print 'WARNING: class "%s" is not documented: \x1b[34m%s:%d\x1b[0m' \
							% (current_class['name'], filename, lno)
						classes.append(current_class)
				context = ''
				blanks += 1
				continue

			if looks_like_function(l):
				current_fun, lno = parse_function(lno - 1, lines, filename)
				if current_fun != None and is_visible(context):
					if context == '' and blanks == 0 and len(functions):
						functions[-1]['signatures'].update(current_fun['signatures'])
						functions[-1]['names'].update(current_fun['names'])
					else:
						current_fun['desc'] = context
						if context == '':
							print 'WARNING: function "%s" is not documented: \x1b[34m%s:%d\x1b[0m' \
								% (first_item(current_fun['names']), filename, lno)
						functions.append(current_fun)
					context = ''
					blanks = 0
				continue

		if ('class ' in l or 'struct ' in l) and not ';' in l:
			lno = consume_block(lno - 1, lines)
			context = ''
			blanks += 1
			continue

		if l.startswith('enum '):
			if not is_visible(context):
				consume_block(lno - 1, lines)
			else:
				current_enum, lno = parse_enum(lno - 1, lines, filename)
				if current_enum != None and is_visible(context):
					current_enum['desc'] = context
					if context == '':
						print 'WARNING: enum "%s" is not documented: \x1b[34m%s:%d\x1b[0m' \
							% (current_enum['name'], filename, lno)
					enums.append(current_enum)
			context = ''
			blanks += 1
			continue

		blanks += 1
		if verbose: print '??    %s' % l

		context = ''
	h.close()

# ====================================================================
#
#                               RENDER PART
#
# ====================================================================


if dump:

	if verbose: print '\n===============================\n'

	for c in classes:
		print '\x1b[4m%s\x1b[0m %s\n{' % (c['type'], c['name'])
		for f in c['fun']:
			for s in f['signatures']:
				print '   %s' % s.replace('\n', '\n   ')

		if len(c['fun']) > 0 and len(c['fields']) > 0: print ''

		for f in c['fields']:
			for s in f['signatures']:
				print '   %s' % s

		if len(c['fields']) > 0 and len(c['enums']) > 0: print ''

		for e in c['enums']:
			print '   \x1b[4menum\x1b[0m %s\n   {' % e['name']
			for v in e['values']:
				print '      %s' % v['name']
			print '   };'
		print '};\n'

	for f in functions:
		print '%s' % f['signature']

	for e in enums:
		print '\x1b[4menum\x1b[0m %s\n{' % e['name']
		for v in e['values']:
			print '   %s' % v['name']
		print '};'

categories = {}

for c in classes:
	cat = categorize_symbol(c['name'], c['file'])
	if not cat in categories:
		categories[cat] = { 'classes': [], 'functions': [], 'enums': [], 'filename': 'reference-%s.rst' % cat.replace(' ', '_')}

	if c['file'] in overviews:
		categories[cat]['overview'] = overviews[c['file']]

	filename = categories[cat]['filename'].replace('.rst', '.html') + '#'
	categories[cat]['classes'].append(c)
	symbols[c['name']] = filename + c['name']
	for f in c['fun']:
		for n in f['names']:
			symbols[n] = filename + n
			symbols[c['name'] + '::' + n] = filename + n

	for f in c['fields']:
		for n in f['names']:
			symbols[c['name'] + '::' + n] = filename + n

	for e in c['enums']:
		symbols[e['name']] = filename + e['name']
		symbols[c['name'] + '::' + e['name']] = filename + e['name']
		for v in e['values']:
#			symbols[v['name']] = filename + v['name']
			symbols[e['name'] + '::' + v['name']] = filename + v['name']
			symbols[c['name'] + '::' + v['name']] = filename + v['name']

for f in functions:
	cat = categorize_symbol(first_item(f['names']), f['file'])
	if not cat in categories:
		categories[cat] = { 'classes': [], 'functions': [], 'enums': [], 'filename': 'reference-%s.rst' % cat.replace(' ', '_')}

	if f['file'] in overviews:
		categories[cat]['overview'] = overviews[f['file']]

	for n in f['names']:
		symbols[n] = categories[cat]['filename'].replace('.rst', '.html') + '#' + n
	categories[cat]['functions'].append(f)

for e in enums:
	cat = categorize_symbol(e['name'], e['file'])
	if not cat in categories:
		categories[cat] = { 'classes': [], 'functions': [], 'enums': [], 'filename': 'reference-%s.rst' % cat.replace(' ', '_')}
	categories[cat]['enums'].append(e)
	filename = categories[cat]['filename'].replace('.rst', '.html') + '#'
	symbols[e['name']] = filename + e['name']
	for v in e['values']:
		symbols[e['name'] + '::' + v['name']] = filename + v['name']

def print_declared_in(out, o):
	out.write('Declared in "%s"\n\n' % print_link(o['file'], '../include/%s' % o['file']))
	print >>out, dump_link_targets()

# returns RST marked up string
def linkify_symbols(string):
	lines = string.split('\n')
	ret = []
	in_literal = False
	for l in lines:
		if l.startswith('|'):
			ret.append(l)
			continue
		if in_literal and not l.startswith('\t') and not l == '':
#			print '  end literal: "%s"' % l
			in_literal = False
		if in_literal:
#			print '  literal: "%s"' % l
			ret.append(l)
			continue
		if l.endswith('::') or '.. code::' in l:
#			print '  start literal: "%s"' % l
			in_literal = True
		words = l.split(' ')

		for i in range(len(words)):
			# it's important to preserve leading
			# tabs, since that's relevant for
			# rst markup

			leading = ''
			w = words[i]

			if len(w) == 0: continue

			while len(w) > 0 and \
				w[0] in ['\t', ' ', '(', '[', '{']:
				leading += w[0]
				w = w[1:]

			# preserve commas and dots at the end
			w = w.strip()
			trailing = ''

			if len(w) == 0: continue

			while len(w) > 1 and w[-1] in ['.', ',', ')'] and w[-2:] != '()':
				trailing = w[-1] + trailing
				w = w[:-1]

			link_name = w;

#			print w

			if len(w) == 0: continue

			if link_name[-1] == '_': link_name = link_name[:-1]

			if w in symbols:
				link_name = link_name.replace('-', ' ')
#				print '  found %s -> %s' % (w, link_name)
				words[i] = leading + print_link(link_name, symbols[w]) + trailing
		ret.append(' '.join(words))
	return '\n'.join(ret)

link_targets = []

def print_link(name, target):
	global link_targets
	link_targets.append(target)
	return "`%s`__" % name

def dump_link_targets():
	global link_targets
	ret = ''
	for l in link_targets:
		ret += '__ %s\n' % l
	link_targets = []
	return ret

def heading(string, c):
	return '\n' + string + '\n' + (c * len(string)) + '\n'

def render_enums(out, enums, print_declared_reference):
	for e in enums:
		print >>out, '.. raw:: html\n'
		print >>out, '\t<a name="%s"></a>' % e['name']
		print >>out, ''
		print >>out, heading('enum %s' % e['name'], '.')

		print_declared_in(out, e)

		width = [len('name'), len('value'), len('description')]

		for i in range(len(e['values'])):
			e['values'][i]['desc'] = linkify_symbols(e['values'][i]['desc'])

		for v in e['values']:
			width[0] = max(width[0], len(v['name']))
			width[1] = max(width[1], len(v['val']))
			for d in v['desc'].split('\n'):
				width[2] = max(width[2], len(d))

		print >>out, '+-' + ('-' * width[0]) + '-+-' + ('-' * width[1]) + '-+-' + ('-' * width[2]) + '-+'
		print >>out, '| ' + 'name'.ljust(width[0]) + ' | '  + 'value'.ljust(width[1]) + ' | ' + 'description'.ljust(width[2]) + ' |'
		print >>out, '+=' + ('=' * width[0]) + '=+=' + ('=' * width[1]) + '=+=' + ('=' * width[2]) + '=+'
		for v in e['values']:
			d = v['desc'].split('\n')
			if len(d) == 0: d = ['']
			print >>out, '| ' + v['name'].ljust(width[0]) + ' | '  + v['val'].ljust(width[1]) + ' | ' + d[0].ljust(width[2]) + ' |'
			for s in d[1:]:
				print >>out, '| ' + (' ' * width[0]) + ' | '  + (' ' * width[1]) + ' | ' + s.ljust(width[2]) + ' |'
			print >>out, '+-' + ('-' * width[0]) + '-+-' + ('-' * width[1]) + '-+-' + ('-' * width[2]) + '-+'
		print >>out, ''

		print >>out, dump_link_targets()


out = open('reference.rst', 'w+')
out.write('''==================================
libtorrent reference documentation
==================================

.. raw:: html

	<div style="column-count: 4; -webkit-column-count: 4; -moz-column-count: 4; column-width: 13em; -webkit-column-width: 13em; -moz-column-width: 13em">

''')

for cat in categories:
	print >>out, '%s' % heading(cat, '-')

	if 'overview' in categories[cat]:
		print >>out, '| overview__'

	category_filename = categories[cat]['filename'].replace('.rst', '.html')
	for c in categories[cat]['classes']:
		print >>out, '| ' + print_link(c['name'], symbols[c['name']])
	for f in categories[cat]['functions']:
		for n in f['names']:
			print >>out, '| ' + print_link(n, symbols[n])
	for e in categories[cat]['enums']:
		print >>out, '| ' + print_link(e['name'], symbols[e['name']])
	print >>out, ''

	if 'overview' in categories[cat]:
		print >>out, '__ %s#overview' % categories[cat]['filename'].replace('.rst', '.html')
	print >>out, dump_link_targets()

out.write('''

.. raw:: html

	</div>

''')
out.close()

for cat in categories:
	out = open(categories[cat]['filename'], 'w+')

	classes = categories[cat]['classes']
	functions = categories[cat]['functions']
	enums = categories[cat]['enums']

	out.write('%s\n' % heading(cat, '='))

	out.write('''
:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.0.8

.. contents:: Table of contents
  :depth: 1
  :backlinks: none

''')

	if 'overview' in categories[cat]:
		out.write('%s\n' % linkify_symbols(categories[cat]['overview']))

	for c in classes:

		print >>out, '.. raw:: html\n'
		print >>out, '\t<a name="%s"></a>' % c['name']
		print >>out, ''

		out.write('%s\n' % heading(c['name'], '-'))
		print_declared_in(out, c)
		c['desc'] = linkify_symbols(c['desc'])
		out.write('%s\n' % c['desc'])
		print >>out, dump_link_targets()

		print >>out,'\n.. parsed-literal::\n\t'

		block = '\n%s\n{\n' % c['decl']
		for f in c['fun']:
			for s in f['signatures']:
				block += '   %s\n' % highlight_signature(s.replace('\n', '\n   '))

		if len(c['fun']) > 0 and len(c['enums']) > 0: block += '\n'

		first = True
		for e in c['enums']:
			if not first:
				block += '\n'
			first = False
			block += '   enum %s\n   {\n' % e['name']
			for v in e['values']:
				block += '      %s,\n' % v['name']
			block += '   };\n'

		if len(c['fun']) + len(c['enums']) > 0 and len(c['fields']): block += '\n'

		for f in c['fields']:
			for s in f['signatures']:
				block += '   %s\n' % s

		block += '};'

		print >>out, block.replace('\n', '\n\t') + '\n'

		for f in c['fun']:
			if f['desc'] == '': continue
			title = ''
			print >>out, '.. raw:: html\n'
			for n in f['names']:
				print >>out, '\t<a name="%s"></a>' % n
			print >>out, ''
			for n in f['names']:
				title += '%s ' % n
			print >>out, heading(title.strip(), '.')

			block = '.. parsed-literal::\n\n'

			for s in f['signatures']:
				block += highlight_signature(s.replace('\n', '\n   ')) + '\n'
			print >>out, '%s\n' % block.replace('\n', '\n\t')
			f['desc'] = linkify_symbols(f['desc'])
			print >>out, '%s' % f['desc']
	
			print >>out, dump_link_targets()

		render_enums(out, c['enums'], False)

		for f in c['fields']:
			if f['desc'] == '': continue

			print >>out, '.. raw:: html\n'
			for n in f['names']:
				print >>out, '\t<a name="%s"></a>' % n
			print >>out, ''

			for n in f['names']:
				print >>out, '%s ' % n,
			print >>out, ''
			f['desc'] = linkify_symbols(f['desc'])
			print >>out, '\t%s' % f['desc'].replace('\n', '\n\t')

			print >>out, dump_link_targets()


	for f in functions:
		h = ''
		print >>out, '.. raw:: html\n'
		for n in f['names']:
			print >>out, '\t<a name="%s"></a>' % n
		print >>out, ''
		for n in f['names']:
			h += '%s ' % n
		print >>out, heading(h, '.')
		print_declared_in(out, f)

		block = '.. parsed-literal::\n\n'
		for s in f['signatures']:
			block += highlight_signature(s) + '\n'

		print >>out, '%s\n' % block.replace('\n', '\n\t')
		print >>out, linkify_symbols(f['desc'])

		print >>out, dump_link_targets()
	
	render_enums(out, enums, True)

	print >>out, dump_link_targets()

	for i in static_links:
		print >>out, i

	out.close()

#for s in symbols:
#	print s

for i,o in preprocess_rst.items():
	f = open(i, 'r')
	out = open(o, 'w+')
	print 'processing %s -> %s' % (i, o)
	l = linkify_symbols(f.read())
	print >>out, l,

	print >>out, dump_link_targets()

	out.close()
	f.close()
		

