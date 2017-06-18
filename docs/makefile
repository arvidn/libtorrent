# this makefile assumes that you have docutils and rst2pdf installed
# (python-docutils) as well as aafigure (python-aafigure)

ifneq ($(STAGE),)
WEB_PATH = ~/Documents/rasterbar/web/products/libtorrent
endif

ifndef RST2HTML
RST2HTML:=rst2html
endif

ifndef AAFIGURE
AAFIGURE=aafigure
endif

REFERENCE_TARGETS = \
	manual-ref \
	reference \
	reference-Core \
	reference-Plugins \
	reference-Create_Torrents \
	reference-Error_Codes \
	reference-Storage \
	reference-Custom_Storage \
	reference-Utility \
	reference-Bencoding \
	reference-Alerts \
	reference-Filter \
	reference-Settings \
	reference-Bdecoding \
	reference-ed25519

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
	tutorial \
	single-page-ref \
	$(REFERENCE_TARGETS)

FIGURES = \
	read_disk_buffers \
	write_disk_buffers \
	hacking \
	utp_stack \
	storage \
	disk_cache \
	troubleshooting

html: $(TARGETS:=.html) $(FIGURES:=.png) todo.html

rst: $(TARGETS:=.rst) todo.html

pdf: $(TARGETS:=.pdf) $(FIGURES:=.eps)

epub: $(TARGETS:=.epub) $(FIGURES:=.png)

all: html pdf

single-page-ref.rst: $(REFERENCE_TARGETS:=.rst)
	python join_rst.py $(filter-out reference.rst, $(REFERENCE_TARGETS:=.rst)) >single-page-ref.rst

settings.rst: ../include/libtorrent/settings_pack.hpp
	python gen_settings_doc.py

stats_counters.rst: ../src/session_stats.cpp ../include/libtorrent/performance_counters.hpp
	python gen_stats_doc.py

manual.rst: stats_counters.rst
	touch manual.rst

#troubleshooting_thumb.png: troubleshooting.png
#	convert troubleshooting.png -resize 800x800 troubleshooting_thumb.png
#ifneq ($(STAGE),)
#	cp $@ $(WEB_PATH)/$@
#endif

todo.html:gen_todo.py ../src/*.cpp ../include/libtorrent/*.hpp
	python gen_todo.py
ifneq ($(STAGE),)
	cp $@ $(WEB_PATH)/$@
endif

$(REFERENCE_TARGETS:=.rst) plain_text_out.txt:gen_reference_doc.py ../include/libtorrent/*.hpp ../include/libtorrent/kademlia/*.hpp manual.rst settings.rst stats_counters.rst
	python gen_reference_doc.py --plain-output

spell-check:plain_text_out.txt
	hunspell -d hunspell/en_US -p hunspell/libtorrent.dic -l plain_text_out.txt >hunspell-report.txt
	@if [ -s hunspell-report.txt ]; then echo 'spellcheck failed, fix words or add to dictionary:'; cat hunspell-report.txt; false; fi;

%.epub:%.rst
	rst2epub --exit-status=2 $? $@

%.pdf:%.rst
	rst2pdf --exit-status=2 $? -o $@ --stylesheets stylesheet

%.html:%.rst
	$(RST2HTML) --exit-status=2 --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $@
ifneq ($(STAGE),)
	$(RST2HTML) --exit-status=2 --template=template2.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $? > $(WEB_PATH)/$@
endif

%.png:%.dot
	dot -Tpng $? >$@
ifneq ($(STAGE),)
	cp $@ $(WEB_PATH)/$@
endif

%.eps:%.dot
	dot -Teps $? >$@
ifneq ($(STAGE),)
	cp $@ $(WEB_PATH)/$@
endif

%.png:%.diagram
	$(AAFIGURE) --scale 0.6 -o $@ $?
ifneq ($(STAGE),)
	cp $@ $(WEB_PATH)/$@
endif

clean:
	rm -f $(TARGETS:=.html) $(TARGETS:=.pdf) $(FIGURES:=.png) $(FIGURES:=.eps) $(REFERENCE_TARGETS:=.rst) settings.rst todo.html reference*.html stats_counters.rst

