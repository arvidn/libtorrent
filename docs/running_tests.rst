=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

running and building tests
==========================

The tests for SOCKS and HTTP proxy relies on ``delegate`` being installed
to set up test proxies.  This document outlines the requirements of the
tests as well as describes how to set up your environment to be able to run them.

.. _lighty: http://www.lighttpd.net

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

