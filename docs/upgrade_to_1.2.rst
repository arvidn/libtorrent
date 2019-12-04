===========================
Upgrading to libtorrent 1.2
===========================

:Author: Arvid Norberg, arvid@libtorrent.org

.. contents:: Table of contents
  :depth: 1
  :backlinks: none

libtorrent version 1.2 comes with some significant updates in the API.
This document summarizes the changes affecting library users.

C++98 no longer supported
=========================

With libtorrent 1.2, C++98 is no longer supported, you need a compiler capable
of at least C++11 to build libtorrent.

This also means libtorrent types now support move.

listen interfaces
=================

There's a subtle change in how the ``listen_interfaces`` setting is interpreted
in 1.2 compared to 1.1.

In libtorrent-1.1, if you listen to ``0.0.0.0:6881`` (which was the default),
not only would an IPv4 listen socket be opened (bound to INADDR_ANY) but also an
IPv6 socket would be opened.

In libtorrent-1.2, if you listen to ``0.0.0.0:6881`` only the IPv4 INADDR_ANY is
opened as a listen socket. If you want to listen to both IPv4 and IPv6, you need
to listen to ``0.0.0.0:6881,[::]:6881``.

forward declaring libtorrent types deprecated
=============================================

Clients are discouraged from forward declaring types from libtorrent.
Instead, include the <libtorrent/fwd.hpp> header.

A future release will introduce ABI versioning using an inline namespace, which will break any forward declarations by clients.

There is a new namespace alias, ``lt`` which is shorthand for ``libtorrent``.
In the future, ``libtorrent`` will be the alias and ``lt`` the namespace name.
With no forward declarations inside libtorrent's namespace though, there should not be any reason for clients to re-open the namespace.

resume data handling
====================

To significantly simplify handling of resume data, the previous way of handling it is deprecated.
resume data is no longer passed in as a flat buffer in the add_torrent_params.
The add_torrent_params structure itself *is* the resume data now.

In order to parse the bencoded fast resume file (which is still the same format, and backwards compatible) use the read_resume_data() function.

Similarly, when saving resume data, the save_resume_data_alert now has a ``params`` field of type add_torrent_params which contains the resume data.
This object can be serialized into the bencoded form using write_resume_data().

This give the client full control over which properties should be loaded from the resume data and which should be controlled by the client directly.
The flags ``flag_override_resume_data``, ``flag_merge_resume_trackers``, ``flag_use_resume_save_path`` and ``flag_merge_resume_http_seeds`` have all been deprecated, since they are no longer needed.

The old API is still supported as long as libtorrent is built with deprecated functions enabled (which is the default).
It will be performing slightly better without deprecated functions present.

rate_limit_utp changed defaults
===============================

The setting ``rate_limit_utp`` was deprecated in libtorrent 1.1.
When building without deprecated features (``deprecated-functions=off``) the default behavior also changed to have rate limits apply to utp sockets too.
In order to be more consistent between the two build configurations, the default value has changed to true.
The new mechanism provided to apply special rate limiting rules is *peer classes*.
In order to implement the old behavior of not rate limiting uTP peers, one can set up a peer class for all uTP peers, to make the normal peer classes not apply to them (which is where the rate limits are set).

announce entry multi-home support
=================================

The announce_entry type now captures status on individual endpoints, as opposed to treating every tracker behind the same name as a single tracker.
This means some properties has moved into the ``announce_endpoint`` structure, and an announce entry has 0 or more endpoints.

alerts no longer cloneable
==========================

As part of the transition to a more efficient handling of alerts, 1.1 allocated them in a contiguous, heterogeneous, vector.
This means they are no longer heap allocated nor held by a smart pointer.
The ``clone()`` member on alerts was deprecated in 1.1 and removed in 1.2.
To pass alerts across threads, instead pull out the relevant information from the alerts and pass that across.

progress alert category
=======================

The ``alert::progress_notification`` category has been deprecated.
Alerts posted in this category are now also posted in one of these new categories:

* ``alert::block_progress_notification``
* ``alert::piece_progress_notification``
* ``alert::file_progress_notification``
* ``alert::upload_notification``

boost replaced by std
=====================

``boost::shared_ptr`` has been replaced by ``std::shared_ptr`` in the libtorrent API.
The same goes for ``<cstdint>`` types, instead of ``boost::int64_t``, libtorrent now uses ``std::int64_t``.
Instead of ``boost::array``, ``std::array`` is used, and ``boost::function`` has been replaced by ``std::function``.

strong typedefs
===============

In order to strengthen type-safety, libtorrent now uses special types to represent certain indexes and ID types.
Any integer referring to a piece index, now has the type ``piece_index_t``, and indices to files in a torrent, use ``file_index_t``.
Similarly, time points and duration now use ``time_point`` and ``duration`` from the ``<chrono>`` standard library.

The specific types have typedefs at ``lt::time_point`` and ``lt::duration``, and the clock used by libtorrent is ``lt::clock_type``.`

strongly typed flags
====================

Enum flags have been replaced by strongly typed flags.
This means their implicit conversion to and from ``int`` is deprecated.
For example, the following expressions are deprecated::

	if ((atp.flags & add_torrent_params::flag_paused) == 0)

	atp.flags = 0;

Insted say::

	if (!(atp.flags & torrent_flags::paused))

	atp.flags = {};

(Also note that in this specific example, the flags moved out of the ``add_torrent_params`` structure, but this is unrelated to them also having stronger types).

span<> and string_view
======================

The interface has adopted ``string_view`` (from boost for now) and ``span<>`` (custom implementation for now).
This means some function calls that previously took ``char const*`` or ``std::string`` may now take an ``lt::string_view``.
Similarly, functions that previously would take a pointer and length pair will now take a ``span<>``.

periphery utility functions no longer exported
==============================================

Historically, libtorrent has exported functions not essential to its core bittorrent functionality.
Such as filesystem functions like ``directory``, ``file`` classes and ``remove``, ``create_directory`` functions.
Path manipulation functions like ``combine_path``, ``extension``, ``split_path`` etc.
String manipulation functions like ``from_hex`` and ``to_hex``.
Time functions like ``time_now``. These functions are no longer available to clients, and some have been removed from the library.
Instead, it is recommended to use boost.filesystem or the experimental filesystem TS.

plugins
=======

libtorrent session plugins no longer have all callbacks called unconditionally.
The plugin has to register which callbacks it's interested in receiving by returning a bitmask from ``feature_flags_t implemented_features()``.
The return value is documented in the plugin class.

RSS functions removed
=====================

The deprecated RSS functions have been removed from the library interface.


