:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.0.6

.. raw:: html

   <div id="librarySidebar">

* download_
* `download python binding`_
* features_
* contributing_
* `building libtorrent`_
* examples_
* `library overview`_
* `reference documentation`_
* `troubleshooting issues`_
* `tuning`_
* screenshot_
* `mailing list`_ (archive_)
* `who's using libtorrent?`_
* `report bugs`_
* `github page`_
* `blog`_

--------

Extensions

* `uTP`_
* `extensions protocol`_
* `plugin interface`_
* `streaming`_
* `DHT extensions`_
* `DHT security extension`_
* `DHT store extension`_
* `UDP tracker protocol`_
* `HTTP seed`_
* multitracker_

--------

Bindings

* `ruby bindings`_
* `python bindings`_

--------

* `Introduction, slides`_

.. raw:: html

   </div>
   <div id="libraryBody">

==========
libtorrent
==========

.. _download: https://sourceforge.net/projects/libtorrent/files/libtorrent/
.. _`download python binding`: https://sourceforge.net/projects/libtorrent/files/py-libtorrent/
.. _features: features.html
.. _contributing: contributing.html
.. _`building libtorrent`: building.html
.. _examples: examples.html
.. _`library overview`: manual-ref.html
.. _`reference documentation`: reference.html
.. _`troubleshooting issues`: troubleshooting.html
.. _`tuning`: tuning.html
.. _screenshot: client_test.png
.. _`uTP`: utp.html
.. _`extensions protocol`: extension_protocol.html
.. _`plugin interface`: reference-Plugins.html
.. _`streaming`: streaming.html
.. _`DHT extensions`: dht_extensions.html
.. _`DHT security extension`: dht_sec.html
.. _`DHT store extension`: dht_store.html
.. _`UDP tracker protocol`: udp_tracker_protocol.html
.. _`HTTP seed`: http://www.getright.com/seedtorrent.html
.. _multitracker: http://bittorrent.org/beps/bep_0012.html
.. _mailing list: http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss
.. _archive: http://dir.gmane.org/gmane.network.bit-torrent.libtorrent
.. _`who's using libtorrent?`: projects.html
.. _`report bugs`: https://github.com/arvidn/libtorrent/issues
.. _`github page`: http:/github.com/arvidn/libtorrent
.. _blog: http://blog.libtorrent.org

.. _`ruby bindings`: http://libtorrent-ruby.rubyforge.org/
.. _`python bindings`: python_binding.html

.. _`Introduction, slides`: bittorrent.pdf

libtorrent is a feature complete C++ bittorrent implementation focusing
on efficiency and scalability. It runs on embedded devices as well as
desktops. It boasts a well documented library interface that is easy to
use. It comes with a `simple bittorrent client`__ demonstrating the use of
the library.

__ client_test.html

The main goals of libtorrent are:

* to be cpu efficient
* to be memory efficient
* to be very easy to use


Donate
======

Support the development of libtorrent

.. raw:: html
	
	<a class="FlattrButton" style="display:none;" href="http://libtorrent.org"></a>
	<noscript><a href="https://flattr.com/thing/95662/libtorrent" target="_blank">
	<img src="http://api.flattr.com/button/flattr-badge-large.png" alt="Flattr this" title="Flattr this" border="0" /></a></noscript>


Feedback
========

There's a `mailing list`__, general libtorrent discussion.

__ http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss

You can usually find me as hydri in ``#libtorrent`` on ``irc.freenode.net``.

license
=======

libtorrent is released under the BSD-license_.

.. _BSD-license: http://opensource.org/licenses/bsd-license.php

This means that you can use the library in your project without having to
release its source code. The only requirement is that you give credit
to the author of the library by including the libtorrent license in your
software or documentation.

It is however greatly appreciated if additional features are contributed
back to the open source project. Patches can be emailed to the mailing
list or posted to the `bug tracker`_.

.. _`bug tracker`: https://github.com/arvidn/libtorrent/issues

Acknowledgements
================

Written by Arvid Norberg. Copyright |copy| 2003-2014

Contributions by Magnus Jonsson, Daniel Wallin and Cory Nelson

Thanks to Reimond Retz for bugfixes, suggestions and testing

Thanks to `Umeå University`__ for providing development and test hardware.

__ http://www.cs.umu.se

Project is hosted by sourceforge.

|sf_logo|__

__ http://sourceforge.net

.. |sf_logo| image:: http://sourceforge.net/sflogo.php?group_id=7994
.. |copy| unicode:: 0xA9 .. copyright sign

.. raw:: html

   </div>

