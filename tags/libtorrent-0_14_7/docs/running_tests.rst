=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

running and building tests
==========================

Some of the tests of libtorrent are not self contained. For instance, in
order to test the ``http_connection`` class in libtorrent, the test requires
lighty_. This document outlines the requirements of the tests as well as
describes how to set up your environment to be able to run them.

.. _lighty: http://www.lighttpd.net

lighty
======

Download lighty_. I've tested with ``lighttpd-1.4.19``. If libtorrent is built
with SSL support (which it is by default), lighty needs SSL support as well.

To build lighty with SSL support do::

	./configure --with-openssl

Followed by::

	sudo make install

Make sure you have SSL support in lighty by running::

	lighttpd -V

Which gives you a list of all enabled features.

delegate
========

Delegate_ can act as many different proxies, which makes it a convenient
tool to use to test libtorrent's support for SOCKS4, SOCKS5, HTTPS and
HTTP proxies.

.. _Delegate: http://www.delegate.org

You can download prebuilt binaries for the most common platforms on
`deletate's download page`_. Make sure to name the executable ``delegated``
and put it in a place where a shell can pick it up, in its ``PATH``. For
instance ``/bin``.

.. _`deletate's download page`: http://www.delegate.org/delegate/download/

OpenSSL
=======

In order to create an SSL certificate for lighty_, openssl is used. More
specifically, the following command is issued by the test to create the
certificate file::

	echo -e "AU\ntest province\ntest city\ntest company\ntest department\n\
		tester\ntest@test.com" | openssl req -new -x509 -keyout server.pem \
		-out server.pem -days 365 -nodes

This will write ``server.pem`` which is referenced in the lighty
confiuration file.

OpenSSL comes installed with most Linux and BSD distros, including Mac OS X.
You can download it from `the openssl homepage`_.

.. _`the openssl homepage`: http://www.openssl.org/

