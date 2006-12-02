:Author: Torsten Bergander

building libtorrent on SuSE 10.1 (i586)
=======================================

0. Prerequisites
	Install packages ``boost`` and ``boost-devel`` from packman:
	http://packman.links2linux.org/package/boost/10887

	There are standard packages delivered with the distribution. They give
	some headaches when building new stuff, so the above mentioned ones fix
	these problems.
	All other potential prerequisites are available via yast in the standard
	distribution.

1. Building and installing the lib
	Get the libtorrent-011 release tarbal, unpack and change into its
	directory. Then::

		./configure --with-boost-date-time=boost_date_time
		--with-boost-filesystem=boost_filesystem
		--with-boost-thread=boost_thread-mt --with-boost-regex=boost_regex
		--with-boost-program-options=boost_program_options --disable-debug
		make 
		sudo make install

	If you don't give the --with-boost parameters configure does not find
	the main in the libs and fails. Also, when later installing e.g. btg,
	you have to make sure to use the same parameters there, otherwise it is
	bound to fail.

