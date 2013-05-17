bt/lt.js: bt/libtorrent-webui.js
	java -jar compiler.jar --compilation_level ADVANCED_OPTIMIZATIONS --js bt/libtorrent-webui.js --js_output_file bt/lt.js

bt/lt.js.gz: bt/lt.js
	gzip -9 bt/lt.js -c >bt/lt.js.gz

