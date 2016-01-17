===================
libtorrent Examples
===================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.0.8

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

examples
========

Except for the example programs in this manual, there's also a bigger example
of a (little bit) more complete client, ``client_test``. There are separate
instructions for how to use it here__ if you'd like to try it. Note that building
``client_test`` also requires boost.regex and boost.program_options library.

__ client_test.html

simple client
-------------

This is a simple client. It doesn't have much output to keep it simple:

.. include:: ../examples/simple_client.cpp
	:code: c++
	:tab-width: 2
	:start-after: */

make_torrent
------------

Shows how to create a torrent from a directory tree:

.. include:: ../examples/make_torrent.cpp
	:code: c++
	:tab-width: 2
	:start-after: */

dump_torrent
------------

This is an example of a program that will take a torrent-file as a parameter and
print information about it to std out:

.. include:: ../examples/dump_torrent.cpp
	:code: c++
	:tab-width: 2
	:start-after: */

