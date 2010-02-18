# this makefile assumes that you have docutils and rst2pdf installed

WEB_PATH = ~/Documents/rasterbar/web/products/libtorrent

TARGETS = index \
	udp_tracker_protocol \
	client_test \
	manual \
	building \
	features \
	contributing\
	examples \
	extension_protocol \
	make_torrent \
	dht_extensions \
	libtorrent_plugins \
	python_binding \
	projects

html: $(TARGETS:=.html)

pdf: $(TARGETS:=.pdf)

all: html

%.pdf:%.rst
	rst2pdf $? -o $@

%.html:%.rst
	rst2html.py --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $@
	cp $@ $(WEB_PATH)/$@

clean:
	rm -f $(TARGETS:=.html) $(TARGETS:=.pdf)

