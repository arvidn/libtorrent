# this makefile assumes that you have docutils and rst2pdf installed

WEB_PATH = ~/Documents/rasterbar/web/products/libtorrent

REFERENCE_TARGETS = \
	reference \
	reference-Session \
	reference-Core \
	reference-String \
	reference-Plugins \
	reference-Create_Torrents \
	reference-Error_Codes \
	reference-Time \
	reference-Storage \
	reference-Custom_Storage \
	reference-Utility \
	reference-Bencoding \
	reference-Alerts \
	reference-RSS \
	reference-Filter \
	reference-Settings \
	manual-ref

TARGETS = index \
	udp_tracker_protocol \
	dht_rss \
	dht_store \
	client_test \
	building \
	features \
	troubleshooting \
	contributing\
	examples \
	extension_protocol \
	dht_extensions \
	dht_sec \
	python_binding \
	projects \
	utp \
	tuning \
	hacking \
	streaming \
	$(REFERENCE_TARGETS)

FIGURES = \
	read_disk_buffers \
	write_disk_buffers \
	troubleshooting \
	hacking \
	utp_stack \
	storage

html: $(TARGETS:=.html) $(FIGURES:=.png) todo.html 

pdf: $(TARGETS:=.pdf) $(FIGURES:=.eps)

epub: $(TARGETS:=.epub) $(FIGURES:=.png)

all: html

troubleshooting_thumb.png: troubleshooting.png
	convert troubleshooting.png -resize 800x800 troubleshooting_thumb.png
	cp $@ $(WEB_PATH)/$@

todo.html:gen_todo.py ../src/*.cpp ../include/libtorrent/*.hpp
	python gen_todo.py
	cp $@ $(WEB_PATH)/$@

$(REFERENCE_TARGETS:=.rst):gen_reference_doc.py ../include/libtorrent/*.hpp ../include/libtorrent/kademlia/*.hpp manual.rst
	python gen_reference_doc.py

%.epub:%.rst
	rst2epub $? $@

%.pdf:%.rst
	rst2pdf $? -o $@ --stylesheets stylesheet

%.html:%.rst
	rst2html-3.4.py --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $@
	rst2html-3.4.py --template=template2.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $(WEB_PATH)/$@

%.png:%.dot
	dot -Tpng $? >$@
	cp $@ $(WEB_PATH)/$@

%.eps:%.dot
	dot -Teps $? >$@
	cp $@ $(WEB_PATH)/$@

%.png:%.diagram
	java -jar /opt/local/share/java/ditaa0_9.jar -E $? $@
	cp $@ $(WEB_PATH)/$@

clean:
	rm -f $(TARGETS:=.html) $(TARGETS:=.pdf) todo.html reference*.html reference*.rst

