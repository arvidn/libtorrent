libtorrent-webui provides useful primitives when building bittorrent clients, or bittorrent functionality, on top of libtorrent.

Despite its name, it provides more than just web interfaces. It also provides the followng key features:

* save/load settings (``save_settings`` class).
* auto-load director (``auto_load`` class).
* load plain text configuration (``load_config``).
* utorrent compatible web UI (``utorrent_webui``).
* transmission compatible web UI (``transmission_webui``).
* deluge compatible remote interface (``deluge``).
* general purpose web server (based on mongoose, ``webui``).
* authentication module with varying levels of access permissions (``auth_interface``, ``auth``, ``permissions_interface``).
* PAM baed authentication module (``pam_auth``)
* save/restore torren states, i.e. resume data (``save_resume``).
* mechanism to keep a history of torrent updates, to efficiently provide "send updates since X" (``torrent_history``). Used by the web UIs.
* HTTP post support to add .torrent files (``torrent_post``).

