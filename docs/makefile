# this makefile assumes that you have docutils and rst2pdf installed
# (python-docutils) as well as aafigure (python-aafigure)

ifndef WEB_PATH
WEB_PATH = ~/web/products/libtorrent
endif

ifndef RST2HTML
RST2HTML:=rst2html
endif

ifndef AAFIGURE
AAFIGURE=aafigure
endif

REFERENCE_TARGETS = \
	manual-ref \
	tutorial-ref \
	tuning-ref \
	features-ref \
	upgrade_to_1.2-ref \
	upgrade_to_2.0-ref \
	reference \
	reference-Core \
	reference-DHT \
	reference-Session \
	reference-Torrent_Handle \
	reference-Torrent_Info \
	reference-Trackers \
	reference-PeerClass \
	reference-Torrent_Status \
	reference-Stats \
	reference-Resume_Data \
	reference-Add_Torrent \
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

MANUAL_TARGETS = index \
	udp_tracker_protocol \
	dht_rss \
	dht_store \
	client_test \
	building \
	troubleshooting \
	contributing\
	examples \
	extension_protocol \
	dht_extensions \
	dht_sec \
	python_binding \
	projects \
	utp \
	hacking \
	streaming \
	fuzzing \
	security-audit

TARGETS = single-page-ref \
	$(MANUAL_TARGETS) \
	$(REFERENCE_TARGETS)

FIGURES = $(addprefix img/, \
	read_disk_buffers \
	write_disk_buffers \
	hacking \
	utp_stack \
	storage \
	troubleshooting \
	troubleshooting_thumb \
	screenshot_thumb \
	logo-color \
	logo-bw \
	our_delay_base_thumb \
	delays_thumb \
	cwnd_thumb \
)

STAGE_IMG = $(addprefix img/, \
	logo-color-text \
	logo-color \
	screenshot \
	bitcoin \
	ip_id_v4 \
	ip_id_v6 \
	hash_distribution \
	complete_bit_prefixes \
	our_delay_base \
	delays \
	cwnd \
)

ORIG_HEADERS = $(wildcard ../include/libtorrent/*.hpp)
HEADERS = $(ORIG_HEADERS:../%=%)

html: $(TARGETS:=.html) $(FIGURES:=.png) todo.html favicon.ico $(HEADERS)

stage: $(addprefix $(WEB_PATH)/, $(TARGETS:=.html) $(FIGURES:=.png) todo.html favicon.ico style.css $(HEADERS) $(STAGE_IMG:=.png))

rst: $(TARGETS:=.rst) todo.html

pdf: $(TARGETS:=.pdf) $(FIGURES:=.png)

epub: $(TARGETS:=.epub) $(FIGURES:=.png)

all: html pdf favicon.ico

img/logo-color-text.png: img/logo-color-text.svg
	convert -background transparent $< -resize 400 $@

img/%.png: img/%.svg
	convert -background transparent $< -resize 128x128 $@

favicon.ico: favicon-16.png favicon-32.png favicon-64.png
	icotool -o $@ -c $?

favicon-16.png: img/logo-color.svg
	convert -background transparent $< -resize 16x16 $@

favicon-32.png: img/logo-color.svg
	convert -background transparent $< -resize 32x32 $@

favicon-64.png: img/logo-color.svg
	convert -background transparent $< -resize 64x64 $@

single-page-ref.rst: gen_reference_doc.py ../include/libtorrent/*.hpp ../include/libtorrent/kademlia/*.hpp settings-ref.rst stats_counters.rst
	python3 gen_reference_doc.py --single-page

settings.rst hunspell/settings.dic: ../include/libtorrent/settings_pack.hpp hunspell/libtorrent.dic
	python3 gen_settings_doc.py || { rm $@; exit 1; }
	cat hunspell/libtorrent.dic >>hunspell/settings.dic

stats_counters.rst: ../src/session_stats.cpp ../include/libtorrent/performance_counters.hpp
	python3 gen_stats_doc.py || { rm $@; exit 1; }

manual.rst: stats_counters.rst
	touch manual.rst

%_thumb.png: %.png
	convert $< -resize 400 $@

img/troubleshooting_thumb.png: img/troubleshooting.png
	convert $< -resize 800x800 $@

todo.html:gen_todo.py ../src/*.cpp ../include/libtorrent/*.hpp
	python3 gen_todo.py

$(REFERENCE_TARGETS:=.rst) plain_text_out.txt settings-ref.rst:gen_reference_doc.py ../include/libtorrent/*.hpp ../include/libtorrent/kademlia/*.hpp manual.rst tuning.rst tutorial.rst features.rst settings.rst stats_counters.rst hunspell/settings.dic
	python3 gen_reference_doc.py --plain-output

spell-check:plain_text_out.txt $(MANUAL_TARGETS:=.html) manual.rst settings.rst tutorial.rst
	python3 filter-rst.py manual.rst >manual-plain.txt
	python3 filter-rst.py tutorial.rst >tutorial-plain.txt
	python3 filter-rst.py tuning.rst >tuning-plain.txt
	python3 filter-rst.py settings.rst >settings-plain.txt
	python3 filter-rst.py upgrade_to_1.2.rst >upgrade-1_2-plain.txt
	python3 filter-rst.py upgrade_to_2.0.rst >upgrade-2_0-plain.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -l plain_text_out.txt >hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -l manual-plain.txt >>hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -l tutorial-plain.txt >>hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -l tuning-plain.txt >>hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -l upgrade-1_2-plain.txt >>hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -l upgrade-2_0-plain.txt >>hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/settings.dic -l settings-plain.txt >>hunspell-report.txt
	hunspell -i UTF-8 -d hunspell/en_US -p hunspell/libtorrent.dic -H -l $(MANUAL_TARGETS:=.html) >>hunspell-report.txt
	@if [ -s hunspell-report.txt ]; then echo 'spellcheck failed, fix words or add to dictionary:'; cat hunspell-report.txt; false; fi;

%.epub:%.rst
	rst2epub --exit-status=2 $< $@

%.pdf:%.rst
	rst2pdf $< -o $@ --stylesheets stylesheet

%.html:%.rst template.txt template2.txt
	$(RST2HTML) --title=libtorrent --exit-status=2 --template=template.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $< > $@ || { rm $@; exit 1; }

%.png:%.dot
	dot -Tpng $< >$@ || { rm $@; exit 1; }

%.png:%.diagram
	$(AAFIGURE) --scale 0.6 -o $@ $< || { rm $@; exit 1; }

include/libtorrent/%.hpp: ../include/libtorrent/%.hpp
	mkdir -p include/libtorrent >/dev/null
	cp $< $@

# stage rules

$(WEB_PATH)/%.html:%.rst template.txt template2.txt
	mkdir -p $(WEB_PATH) >/dev/null
	$(RST2HTML) --title=libtorrent --exit-status=2 --template=template2.txt --stylesheet-path=style.css --link-stylesheet --no-toc-backlinks $< > $@ || { rm $@; exit 1; }

$(WEB_PATH)/img/%.png: img/%.png
	mkdir -p $(WEB_PATH)/img >/dev/null
	cp $< $@

$(WEB_PATH)/%.png: %.png
	cp $< $@

$(WEB_PATH)/%.css: %.css
	cp $< $@

$(WEB_PATH)/%.svg: %.svg
	cp $< $@

$(WEB_PATH)/%.html: %.html
	cp $< $@

$(WEB_PATH)/%.ico: %.ico
	cp $< $@

$(WEB_PATH)/include/libtorrent/%.hpp: ../include/libtorrent/%.hpp
	mkdir -p $(WEB_PATH)/include/libtorrent
	cp $< $@

$(WEB_PATH)/img/%.png: img/%.png
	mkdir -p $(WEB_PATH)/img >/dev/null
	cp $< $@

clean:
	rm -f $(TARGETS:=.html) $(TARGETS:=.pdf) $(FIGURES:=.png) $(REFERENCE_TARGETS:=.rst) settings.rst todo.html reference*.html stats_counters.rst hunspell/settings.dic favicon-16.png favicon-32.png favicon-64.png favicon.ico

