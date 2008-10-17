WEB_PATH = ~/Documents/rasterbar/web/products/libtorrent
DOCUTILS = ~/docutils

TARGETS = index.html \
	udp_tracker_protocol.html \
	projects.html \
	client_test.html \
	manual.html \
	building.html \
	features.html \
	examples.html \
	extension_protocol.html \
	dht_extensions.html \
	libtorrent_plugins.html
	
all: $(TARGETS)

%.pdf:%.tex
	pdflatex $?

%.tex:%.rst
	python $(DOCUTILS)/tools/rst2latex.py $? $@

%.html:%.rst
	python $(DOCUTILS)/tools/rst2html.py --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $@
	cp $@ $(WEB_PATH)/$@

clean:
	rm -f $(TARGETS) round_trip_distribution.log

