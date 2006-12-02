:Author: Torsten Bergander

building libtorrent-0.11 on ubuntu Edy Eft 6.10
===============================================

1. Install prerequisites (maybe cann be stripped down but it works)::

	sudo apt-get install bjam boost-build libboost-date-time-dev
	libboost-date-time1.33.1 libboost-filesystem-dev
	libboost-filesystem1.33.1 libboost-graph-dev libboost-graph1.33.1
	libboost-iostreams-dev libboost-iostreams1.33.1
	libboost-program-options-dev libboost-program-options1.33.1
	libboost-regex-dev libboost-regex1.33.1 libboost-serialization-dev
	libboost-signals-dev libboost-signals1.33.1 libboost-test-dev
	libboost-test1.33.1 libboost-thread-dev libboost-thread1.33.1
	libboost-wave-dev libboost-dev

2. Get release tar ball (0.11 at time of writing), unpack, change into::

	libtorrent-0.11 dir

3. Build and install::

	export BOOST_BUILD_PATH=/usr/share/boost-build/tools
	./configure 
	make
	sudo make install


