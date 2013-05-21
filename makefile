all: bt/lt.js.gz bt/lt-g.js.gz

bt/lt.js: bt/libtorrent-webui.js
	java -jar compiler.jar --compilation_level ADVANCED_OPTIMIZATIONS --js bt/libtorrent-webui.js --js_output_file bt/lt.js

bt/lt-g.js: bt/libtorrent-graph.js
	java -jar compiler.jar --compilation_level ADVANCED_OPTIMIZATIONS --js bt/libtorrent-graph.js --js_output_file bt/lt-g.js

%.js.gz: %.js
	gzip -9 $< -c >$@

