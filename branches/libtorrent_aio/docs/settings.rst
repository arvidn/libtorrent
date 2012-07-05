.. _user_agent:

+------------+--------+---------------------------------+
| name       | type   | default                         |
+============+========+=================================+
| user_agent | string | "libtorrent/"LIBTORRENT_VERSION |
+------------+--------+---------------------------------+

this is the client identification to the tracker.
The recommended format of this string is:
"ClientName/ClientVersion libtorrent/libtorrentVersion".
This name will not only be used when making HTTP requests, but also when
sending extended headers to peers that support that extension.
It may not contain \r or \n

.. _announce_ip:

+-------------+--------+---------+
| name        | type   | default |
+=============+========+=========+
| announce_ip | string | 0       |
+-------------+--------+---------+

``announce_ip`` is the ip address passed along to trackers as the ``&ip=`` parameter.
If left as the default, that parameter is omitted.

.. _mmap_cache:

+------------+--------+---------+
| name       | type   | default |
+============+========+=========+
| mmap_cache | string | 0       |
+------------+--------+---------+

``mmap_cache`` may be set to a filename where the disk cache will be mmapped
to. This could be useful, for instance, to map the disk cache from regular
rotating hard drives onto an SSD drive. Doing that effectively introduces
a second layer of caching, allowing the disk cache to be as big as can
fit on an SSD drive (probably about one order of magnitude more than the
available RAM). The intention of this setting is to set it up once at the
start up and not change it while running. The setting may not be changed
as long as there are any disk buffers in use. This default to the empty
string, which means use regular RAM allocations for the disk cache. The file
specified will be created and truncated to the disk cache size (``cache_size``).
Any existing file with the same name will be replaced.

Since this setting sets a hard upper limit on cache usage, it cannot be combined
with ``session_settings::contiguous_recv_buffer``, since that feature treats the
``cache_size`` setting as a soft (but still pretty hard) limit. The result of combining
the two is peers being disconnected after failing to allocate more disk buffers.

This feature requires the ``mmap`` system call, on systems that don't have ``mmap``
this setting is ignored.

.. _allow_multiple_connections_per_ip:

+-----------------------------------+------+---------+
| name                              | type | default |
+===================================+======+=========+
| allow_multiple_connections_per_ip | bool | false   |
+-----------------------------------+------+---------+

determines if connections from the same IP address as
existing connections should be rejected or not. Multiple
connections from the same IP address is not allowed by
default, to prevent abusive behavior by peers. It may
be useful to allow such connections in cases where
simulations are run on the same machie, and all peers
in a swarm has the same IP address.

