import glob
import os

paths = ['src/*.cpp', 'src/kademlia/*.cpp', 'include/libtorrent/*.hpp', 'include/libtorrent/kademlia/*.hpp', 'include/libtorrent/aux_/*.hpp', 'include/libtorrent/extensions/*.hpp']

os.system('ctags %s' % ' '.join(paths))

files = []

for p in paths:
	files.extend(glob.glob(p))

output = open('todo.rst', 'w+')

for f in files:
	h = open(f)

	state = ''
	line_no = 0

	for line in h:
		line_no += 1
		line = line.strip()
		if 'TODO:' in line and line.startswith('//'):
			line = line.split('TODO:')[1]
			state = 'todo'
			headline = '%s:%d' % (f.replace('_', '\\_'), line_no)
			print >>output, '\n' + headline
			print >>output, ('-' * len(headline)) + '\n'
			print >>output, line.strip()
			continue
			
		if state == '': continue

		if state == 'todo':
			if line.strip().startswith('//'):
				print >>output, line[2:].strip()
			else:
				state = 'context'
				print >>output, '\n::\n'
				print >>output, '\t%s' % line
			continue

		if state == 'context':
			print >>output, '\t%s' % line
			state = ''

	h.close()

output.close()

os.system('rst2html-2.6.py todo.rst >todo.html')

