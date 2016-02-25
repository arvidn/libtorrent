:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.1.0

.. raw:: html

   <div id="librarySidebar">

* download_
* features_
* tutorial_
* examples_
* overview_
* documentation_
* contributing_
* building_
* troubleshooting_
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

* python_
* java_
* go_
* node_

--------

* `Introduction, slides`_

.. raw:: html

   </div>
   <div id="libraryBody">

==========
libtorrent
==========

.. _download: https://github.com/arvidn/libtorrent/releases
.. _features: features.html
.. _tutorial: tutorial.html
.. _contributing: contributing.html
.. _building: building.html
.. _examples: examples.html
.. _overview: manual-ref.html
.. _documentation: reference.html
.. _troubleshooting: troubleshooting.html
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
.. _`github page`: https://github.com/arvidn/libtorrent
.. _blog: http://blog.libtorrent.org

.. _java: https://github.com/frostwire/frostwire-jlibtorrent/
.. _python: python_binding.html
.. _go: https://github.com/steeve/libtorrent-go
.. _node: https://github.com/fanatid/node-libtorrent

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

Getting started
===============

The tutorial_ is an introduction to using libtorrent (C++). Also see the
`reference documentation`_.

.. _`reference documentation`: reference.html

Contribute
==========

If your organization use libtorrent, please consider supporting its development.
See the contribute_ page for other ways to help out.

.. raw:: html

	<span style="display:inline-block">
	<a class="FlattrButton" style="display:none;" href="http://libtorrent.org"></a>
	<noscript><a href="https://flattr.com/thing/95662/libtorrent" target="_blank">
	<img src="http://api.flattr.com/button/flattr-badge-large.png" alt="Flattr this" title="Flattr this" border="0" /></a></noscript>
	</span>
	
	<span style="display:inline-block">
	<form action="https://www.paypal.com/cgi-bin/webscr" method="post" target="_top">
	<input type="hidden" name="cmd" value="_donations">
	<input type="hidden" name="business" value="ZNR45WU2PP5W2">
	<input type="hidden" name="lc" value="US">
	<input type="hidden" name="item_name" value="libtorrent">
	<input type="hidden" name="currency_code" value="USD">
	<input type="hidden" name="bn" value="PP-DonationsBF:btn_donate_LG.gif:NonHosted">
	<input type="image" src="https://www.paypalobjects.com/webstatic/en_US/i/buttons/pp-acceptance-medium.png" border="0" name="submit" alt="PayPal - The safer, easier way to pay online!">
	<img alt="" border="0" src="https://www.paypalobjects.com/en_US/i/scr/pixel.gif" width="1" height="1">
	</form>
	</span>


Support
=======

Please direct questions to the `mailing list`__, general libtorrent discussion.

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

Written by Arvid Norberg. Copyright |copy| 2003-2016

Contributions by Steven Siloti, Magnus Jonsson, Daniel Wallin and Cory Nelson

Thanks to Reimond Retz for bugfixes, suggestions and testing

Thanks to `Umeå University`__ for providing development and test hardware.

__ http://www.cs.umu.se

Project is hosted by github__.

__ https://www.github.com/arvidn/libtorrent

.. |copy| unicode:: 0xA9 .. copyright sign

.. raw:: html

   </div>

