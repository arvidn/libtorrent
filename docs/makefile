# this makefile assumes that you have docutils and rst2pdf installed

WEB_PATH = ~/Documents/rasterbar/web/products/libtorrent

TARGETS = index \
	udp_tracker_protocol \
	dht_rss \
	dht_store \
	client_test \
	manual \
	building \
	features \
	contributing\
	examples \
	extension_protocol \
	make_torrent \
	dht_extensions \
	dht_sec \
	libtorrent_plugins \
	python_binding \
	projects \
	running_tests \
	utp \
	tuning \
	hacking \
	reference \
	reference-Core \
	reference-String \
	reference-Plugins \
	reference-Create_Torrents \
	reference-Error_Codes \
	reference-Time \
	reference-Storage \
	reference-Utility \
	reference-Bencoding \
	reference-Alerts \
	reference-RSS

FIGURES = read_disk_buffers write_disk_buffers troubleshooting

html: $(TARGETS:=.html) $(FIGURES:=.png) todo.html 

pdf: $(TARGETS:=.pdf) $(FIGURES:=.eps)

epub: $(TARGETS:=.epub) $(FIGURES:=.png)

all: html

todo.html:gen_todo.py ../src/*.cpp ../include/libtorrent/*.hpp
	python gen_todo.py

reference.rst:gen_reference_doc.py ../include/libtorrent/*.hpp
	python gen_reference_doc.py

%.epub:%.rst
	rst2epub $? $@

%.pdf:%.rst
	rst2pdf $? -o $@ --stylesheets stylesheet

%.html:%.rst
	rst2html-2.6.py --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $@
	cp $@ $(WEB_PATH)/$@

%.png:%.dot
	dot -Tpng $? >$@
	cp $@ $(WEB_PATH)/$@

%.eps:%.dot
	dot -Teps $? >$@
	cp $@ $(WEB_PATH)/$@

clean:
	rm -f $(TARGETS:=.html) $(TARGETS:=.pdf) todo.html reference*.html

