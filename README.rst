High level utility classes for use with libtorrent (libtorrent.org),
primarily centered around web UI support.

Currently supported web UIs:

* transmission
* deluge
* uTorrent

(patches for improving support or new web UIs are welcome).

Supported features:

* save/load settings (including custom settings)
* save/load resume data regularly
* HTTP basic authorization
* post .torrent files to be added
* download torrent content (via ``/proxy``, like uTorrent)
* auto-load directory
* torrent update queue, to return only torrents that have
  changed since last check-in
* serving files off of filesystem

The main web UI framework leverages mongoose web server and
jsmn JSON parser.

arvid@rasterbar.com

