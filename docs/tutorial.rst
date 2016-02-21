=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.1.0

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

tutorial
========

The fundamental feature of starting and downloading torrents in libtorrent is
achieved by creating a *session*, which provides the context and a container for
torrents. This is done with via the session_ class, most of its interface is
documented under session_handle_ though.

To add a torrent to the session, you fill in an add_torrent_params_ object and
pass it either to `add_torrent()`_ or `async_add_torrent()`_.

``add_torrent()`` is a blocking call which returns a torrent_handle_.

For example:

.. code:: c++

	#include <libtorrent/session.hpp>
	#include <libtorrent/add_torrent_params.hpp>
	#include <libtorrent/torrent_handle.hpp>
	
	namespace lt = libtorrent;
	int main(int argc, char const* argv[])
	{
		if (argc != 2) {
			fprintf(stderr, "usage: %s <magnet-url>\n");
			return 1;
		}
		lt::session ses;

		lt::add_torrent_params atp;
		atp.url = argv[1];
		atp.save_path = "."; // save in current dir
		lt::torrent_handle h = ses.add_torrent(atp);

		// ...
	}

Once you have a torrent_handle_, you can affect it as well as querying status.
First, let's extend the example to print out messages from the bittorrent engine
about progress and events happening under the hood. libtorrent has a mechanism
referred to as *alerts* to communicate back information to the client application.

Clients can poll libtorrents for new alerts via the `pop_alerts()`_ call on the
session object. This call fills in a vector of alert pointers with all new
alerts since the last call to this function. The pointers are owned by the
session object at will become invalidated by the next call to `pop_alerts()`_.

The alerts form a class hierarchy with alert_ as the root class. Each specific
kind of alert may include additional state, specific to the kind of message. All
alerts implement a message() function that prints out pertinent information
of the alert message. This can be convenient for simply logging events.

For programatically react to certain events, use `alert_cast<>`_ to attempt
a down cast of an alert object to a more specific type.

In order to print out events from libtorrent as well as exiting when the torrent
completes downloading, we can poll the session for alerts periodically and print
them out, as well as listening for the torrent_finished_alert_, which is posted
when a torrent completes.

.. code:: c++

	#include <iostream>
	
	#include <libtorrent/session.hpp>
	#include <libtorrent/add_torrent_params.hpp>
	#include <libtorrent/torrent_handle.hpp>
	#include <libtorrent/alert_types.hpp>
	
	namespace lt = libtorrent;
	int main(int argc, char const* argv[])
	{
		if (argc != 2) {
			std::cerr << "usage: " << argv[0] << " <magnet-url>" << std::endl;
			return 1;
		}
		lt::session ses;

		lt::add_torrent_params atp;
		atp.url = argv[1];
		atp.save_path = "."; // save in current dir
		lt::torrent_handle h = ses.add_torrent(atp);

		bool done = false;
		while (!done) {
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			for (lt::alert const* a : alerts) {
				std::cout << a->message() << std::endl;
				if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
					done = true;
				}
			}
		}
	}

*TODO* cover async_add_torrent()
*TODO* cover post_torrent_updates()
*TODO* cover save_resume_data()

.. _session: reference-Core.html#session
.. _session_handle: reference-Core.html#session_handle
.. _add_torrent_params: reference-Core.html#add_torrent_params
.. _`add_torrent()`: reference-Core.html#add_torrent()
.. _`async_add_torrent()`: reference-Core.html#add_torrent()
.. _torrent_handle: reference-Core.html#torrent_handle
.. _`pop_alerts()`: reference-Core.html#pop_alerts()
.. _`alert`: reference-Alerts.html#alert
.. _`alert_cast<>`: reference-Alerts.html#alert_cast()
.. _torrent_finished_alert: reference-Alerts.html#torrent-finished-alert


