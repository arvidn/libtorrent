.. raw:: html

   <div id="librarySidebar">

* download_
* features_
* `building libtorrent`_
* examples_
* `api documentation`_
* `create torrents`_
* `running tests`_
* screenshot_
* `mailing list`_ (archive_)
* `who's using libtorrent?`_
* `report bugs`_
* `sourceforge page`_
* `wiki`_

--------

Extensions

* `extensions protocol`_
* `plugin interface`_
* `DHT extensions`_
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

.. _download: http://sourceforge.net/project/showfiles.php?group_id=79942
.. _features: features.html
.. _`building libtorrent`: building.html
.. _examples: examples.html
.. _`api documentation`: manual.html
.. _`create torrents`: make_torrent.html
.. _`running tests`: running_tests.html
.. _screenshot: client_test.png
.. _`extensions protocol`: extension_protocol.html
.. _`plugin interface`: libtorrent_plugins.html
.. _`DHT extensions`: dht_extensions.html
.. _`UDP tracker protocol`: udp_tracker_protocol.html
.. _`HTTP seed`: http://www.getright.com/seedtorrent.html
.. _multitracker: http://home.elp.rr.com/tur/multitracker-spec.txt
.. _mailing list: http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss
.. _archive: http://dir.gmane.org/gmane.network.bit-torrent.libtorrent
.. _`who's using libtorrent?`: projects.html
.. _`report bugs`: http://code.rasterbar.com/libtorrent/newticket
.. _sourceforge page: http://www.sourceforge.net/projects/libtorrent
.. _wiki: http://code.rasterbar.com/libtorrent

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
	
	<form action="https://www.paypal.com/cgi-bin/webscr" method="post" target="_top">
		<input type="hidden" name="cmd" value="_xclick">
		<input type="hidden" name="business" value="arvid@cs.umu.se">
		<input type="hidden" name="item_name" value="libtorrent">
		<input type="hidden" name="return" value="http://libtorrent.sf.net">
		<input type="hidden" name="currency_code" value="USD">
		<input type="hidden" name="tax" value="0">
		<input type="image" src="https://www.paypal.com/images/x-click-but04.gif"
			border="0" name="submit" alt="Make payments with PayPal - it's fast, free and secure!">
	</form>



Feedback
========

There's a `mailing list`__, general libtorrent discussion.

__ http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss

You can usually find me as hydri in ``#libtorrent`` on ``irc.freenode.net``.

license
=======

libtorrent is released under the BSD-license_.

.. _BSD-license: http://www.opensource.org/licenses/bsd-license.php

This means that you can use the library in your project without having to
release its source code. The only requirement is that you give credit
to the author of the library by including the libtorrent license in your
software or documentation.

It is however greatly appreciated if additional features are contributed
back to the open source project. Patches can be emailed to the mailing
list or posted to the `bug tracker`_.

.. _`bug tracker`: http://code.rasterbar.com/libtorrent/newticket

Acknowledgements
================

Written by Arvid Norberg. Copyright |copy| 2003-2009

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

