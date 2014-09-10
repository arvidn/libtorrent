import glob
import os

paths = ['src/*.cpp', 'src/kademlia/*.cpp', 'include/libtorrent/*.hpp', 'include/libtorrent/kademlia/*.hpp', 'include/libtorrent/aux_/*.hpp', 'include/libtorrent/extensions/*.hpp']

os.system('(cd .. ; ctags %s 2>/dev/null)' % ' '.join(paths))

files = []

for p in paths:
	files.extend(glob.glob(os.path.join('..', p)))

items = []

# keeps 20 non-comment lines, used to get more context around
# todo-items
context = []

priority_count = [0, 0, 0, 0, 0]

def html_sanitize(s):
	ret = ''
	for i in s:
		if i == '<': ret += '&lt;'
		elif i == '>': ret += '&gt;'
		elif i == '&': ret += '&amp;'
		else: ret += i
	return ret

for f in files:
	h = open(f)

	state = ''
	line_no = 0
	context_lines = 0

	for l in h:
		line_no += 1
		line = l.strip()
		if 'TODO:' in line and line.startswith('//'):
			line = line.split('TODO:')[1].strip()
			state = 'todo'
			items.append({})
			items[-1]['location'] = '%s:%d' % (f, line_no)
			items[-1]['priority'] = 0
			if line[0] in '0123456789':
				items[-1]['priority'] = int(line[0])
				line = line[1:].strip()
			items[-1]['todo'] = line
			prio = items[-1]['priority']
			if prio >= 0 and prio <= 3: priority_count[prio] += 1
			continue
			
		if state == '':
			context.append(html_sanitize(l))
			if len(context) > 20: context.pop(0)
			continue

		if state == 'todo':
			if line.strip().startswith('//'):
				items[-1]['todo'] += '\n'
				items[-1]['todo'] += line[2:].strip()
			else:
				state = 'context'
				items[-1]['context'] = ''.join(context) + '<div style="background: #ffff00" width="100%">' + html_sanitize(l) + '</div>';
				context_lines = 1

				context.append(html_sanitize(l))
				if len(context) > 20: context.pop(0)
			continue

		if state == 'context':
			items[-1]['context'] += html_sanitize(l)
			context_lines += 1

			context.append(html_sanitize(l))
			if len(context) > 20: context.pop(0)
			if context_lines > 30: state = ''

	h.close()

items.sort(key = lambda x: x['priority'], reverse = True)

#for i in items:
#	print '\n\n', i['todo'], '\n'
#	print i['location'], '\n'
#	print 'prio: ', i['priority'], '\n'
#	if 'context' in i:
#		print i['context'], '\n'

out = open('todo.html', 'w+')
out.write('''<html><head>
<script type="text/javascript">
/* <![CDATA[ */
	var expanded = -1
	function expand(id) {
		if (expanded != -1) {
			var ctx = document.getElementById(expanded);
			ctx.style.display = "none";
			// if we're expanding the field that's already
			// expanded, just collapse it
			var no_expand = id == expanded;
			expanded = -1;
			if (no_expand) return;
		}
		var ctx = document.getElementById(id);
		ctx.style.display = "table-row";
		expanded = id;
	}
/* ]]> */
</script>

</head><body>
<h1>libtorrent todo-list</h1>
<span style="color: #f77">%d important</span>
<span style="color: #3c3">%d relevant</span>
<span style="color: #77f">%d feasible</span>
<span style="color: #999">%d notes</span>
<table width="100%%" border="1" style="border-collapse: collapse;">''' % \
	(priority_count[3], priority_count[2], priority_count[1], priority_count[0]))

prio_colors = [ '#ccc', '#ccf', '#cfc', '#fcc', '#fdd']

index = 0
for i in items:
	if not 'context' in i: i['context'] = ''
	out.write('<tr style="background: %s"><td>relevance&nbsp;%d</td><td><a href="javascript:expand(%d)">%s</a></td><td>%s</td></tr>' \
		% (prio_colors[i['priority']], i['priority'], index, i['location'], i['todo'].replace('\n', ' ')))

	out.write('<tr id="%d" style="display: none;" colspan="3"><td colspan="3"><h2>%s</h2><h4>%s</h4><pre style="background: #f6f6f6; border: solid 1px #ddd;">%s</pre></td></tr>' \
		% (index, i['todo'], i['location'], i['context']))
	index += 1

out.write('</table></body></html>')
out.close()

