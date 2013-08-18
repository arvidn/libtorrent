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
	hacking

FIGURES = read_disk_buffers write_disk_buffers troubleshooting

html: $(TARGETS:=.html) $(FIGURES:=.png)

pdf: $(TARGETS:=.pdf) $(FIGURES:=.eps)

all: html

%.pdf:%.rst
	rst2pdf $? -o $@ --stylesheets stylesheet

%.html:%.rst
	rst2html.py --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $@
	cp $@ $(WEB_PATH)/$@

%.png:%.dot
	dot -Tpng $? >$@
	cp $@ $(WEB_PATH)/$@

%.eps:%.dot
	dot -Teps $? >$@
	cp $@ $(WEB_PATH)/$@

clean:
	rm -f $(TARGETS:=.html) $(TARGETS:=.pdf)

