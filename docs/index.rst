.. raw:: html

   <div id="librarySidebar">

Getting started

* download_
* building_
* tutorial_
* overview_
* examples_
* features_

--------

Documentation

* reference_
* `blog`_
* `upgrade to 2.0`_
* `upgrade to 1.2`_
* contributing_
* troubleshooting_
* tuning_
* fuzzing_
* `security audit (2020)`_
* `projects using libtorrent`_

--------

Contact

* `mailing list`_ (archive_)
* `report bugs`_
* `github page`_

--------

Extensions

* uTP_
* `extensions protocol`_
* `libtorrent plugins`_
* `streaming`_
* `DHT extensions`_
* `DHT security extension`_
* `DHT store extension`_
* `UDP tracker protocol`_
* `HTTP seed`_
* multi-tracker_

--------

Bindings

* python_
* Java_
* golang_
* node_

--------

* `Introduction, slides`_

.. raw:: html

   </div>
   <div id="libraryBody">

.. _download: https://github.com/arvidn/libtorrent/releases
.. _features: features-ref.html
.. _tutorial: tutorial-ref.html
.. _contributing: contributing.html
.. _building: building.html
.. _examples: examples.html
.. _overview: manual-ref.html
.. _reference: reference.html
.. _`upgrade to 2.0`: upgrade_to_2.0-ref.html
.. _`upgrade to 1.2`: upgrade_to_1.2-ref.html
.. _troubleshooting: troubleshooting.html
.. _tuning: tuning-ref.html
.. _fuzzing: fuzzing.html
.. _`security audit (2020)`: security-audit.html
.. _uTP: utp.html
.. _`extensions protocol`: extension_protocol.html
.. _`libtorrent plugins`: reference-Plugins.html
.. _`streaming`: streaming.html
.. _`DHT extensions`: dht_extensions.html
.. _`DHT security extension`: dht_sec.html
.. _`DHT store extension`: dht_store.html
.. _`UDP tracker protocol`: udp_tracker_protocol.html
.. _`HTTP seed`: http://www.getright.com/seedtorrent.html
.. _multi-tracker: https://www.bittorrent.org/beps/bep_0012.html
.. _mailing list: https://lists.sourceforge.net/lists/listinfo/libtorrent-discuss
.. _archive: https://sourceforge.net/p/libtorrent/mailman/libtorrent-discuss/
.. _`projects using libtorrent`: projects.html
.. _`report bugs`: https://github.com/arvidn/libtorrent/issues
.. _`github page`: https://github.com/arvidn/libtorrent
.. _blog: https://blog.libtorrent.org

.. _Java: https://github.com/frostwire/frostwire-jlibtorrent/
.. _python: python_binding.html
.. _golang: https://github.com/steeve/libtorrent-go
.. _node: https://github.com/fanatid/node-libtorrent

.. _`Introduction, slides`: bittorrent.pdf

introduction
============

libtorrent is a feature complete C++ bittorrent implementation focusing
on efficiency and scalability. It runs on embedded devices as well as
desktops. It boasts a well documented library interface that is easy to
use. It comes with a `simple bittorrent client`__ demonstrating the use of
the library.

__ client_test.html

.. image:: img/screenshot_thumb.png
	:target: client_test.html
	:alt: screenshot of libtorrent's client_test
	:class: front-page-screenshot
	:width: 400
	:height: 239

The main goals of libtorrent are:

* to be CPU efficient
* to be memory efficient
* to be very easy to use

getting started
===============

The tutorial_ is an introduction to using libtorrent (C++). Also see the
`reference documentation`_.

.. _`reference documentation`: reference.html

.. raw:: html

	<br/>
	<a href="bitcoin:373ZDeQgQSQNuxdinNAPnQ63CRNn4iEXzg">
	<img src="img/bitcoin.png" class="front-page-qr" alt="bitcoin address for libtorrent donations" width="190" height="190"></a>

contribute
==========

If your organization uses libtorrent, please consider supporting its development.
See the contributing_ page for other ways to help out.

.. raw:: html

	<div style="text-align: right;">
	<a href="bitcoin:373ZDeQgQSQNuxdinNAPnQ63CRNn4iEXzg">bitcoin:373ZDeQgQSQNuxdinNAPnQ63CRNn4iEXzg</a>
	</div>

	<span style="display:inline-block">
	<form action="https://www.paypal.com/cgi-bin/webscr" method="post" target="_top">
	<input type="hidden" name="cmd" value="_donations">
	<input type="hidden" name="business" value="ZNR45WU2PP5W2">
	<input type="hidden" name="lc" value="US">
	<input type="hidden" name="item_name" value="libtorrent">
	<input type="hidden" name="currency_code" value="USD">
	<input type="hidden" name="bn" value="PP-DonationsBF:btn_donate_LG.gif:NonHosted">
	<input type="image" src="img/pp-acceptance-medium.png" border="0" name="submit" alt="PayPal - The safer, easier way to pay online!">
	</form>
	</span>
	</span>


support
=======

Please direct questions to the `mailing list`__, general libtorrent discussion.

__ https://lists.sourceforge.net/lists/listinfo/libtorrent-discuss

You can usually find me as hydri in ``#libtorrent`` on ``irc.freenode.net``.

license
=======

libtorrent is released under the BSD-license_.

.. _BSD-license: https://opensource.org/licenses/bsd-license.php

This means that you can use the library in your project without having to
release its source code. The only requirement is that you give credit
to the author of the library by including the libtorrent license in your
software or documentation.

It is however greatly appreciated if additional features are contributed
back to the open source project. Patches can be emailed to the mailing
list or posted to the `bug tracker`_.

.. _`bug tracker`: https://github.com/arvidn/libtorrent/issues

acknowledgements
================

Written by Arvid Norberg. Copyright |copy| 2003-2018

Contributions by Steven Siloti, Alden Torres, Magnus Jonsson, Daniel Wallin and Cory Nelson

Thanks to Reimond Retz for bug fixes, suggestions and testing

See github__ for full list of contributors.

__ https://github.com/arvidn/libtorrent/graphs/contributors

Thanks to `Umeå University`__ for providing development and test hardware.

__ http://www.cs.umu.se

Project is hosted by github__.

__ https://www.github.com/arvidn/libtorrent

.. |copy| unicode:: 0xA9 .. copyright sign

.. raw:: html

   </div>

