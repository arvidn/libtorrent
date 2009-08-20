:Author: Arvid Norberg, arvid@rasterbar.com

libtorrent plugins
==================

.. contents::

libtorrent has a plugin interface for implementing extensions to the protocol.
These can be general extensions for transferring metadata or peer exchange
extensions, or it could be used to provide a way to customize the protocol
to fit a particular (closed) network.

In short, the plugin interface makes it possible to:

* register extension messages (sent in the extension handshake), see
  extensions_.
* add data and parse data from the extension handshake.
* send extension messages and standard bittorrent messages.
* override or block the handling of standard bittorrent messages.

.. _extensions: extension_protocol.html

a word of caution
-----------------

Writing your own plugin is a very easy way to introduce serious bugs such as
dead locks and race conditions. Since a plugin has access to internal
structures it is also quite easy to sabotage libtorrent's operation.

All the callbacks in this interface are called with the main libtorrent thread
mutex locked. And they are always called from the libtorrent main thread. In
case portions of your plugin are called from other threads, typically the main
thread, you cannot use any of the member functions on the internal structures
in libtorrent, since those require the mutex to be locked. Futhermore, you would
also need to have a mutex on your own shared data within the plugin, to make
sure it is not accessed at the same time from the libtorrent thread (through a
callback). See `boost thread's mutex`_. If you need to send out a message from
another thread, use an internal queue, and do the actual sending in ``tick()``.

.. _`boost thread's mutex`: http://www.boost.org/doc/html/mutex.html


plugin interface
================

The plugin interface consists of two base classes that the plugin may
implement. These are called ``torrent_plugin`` and ``peer_plugin``. They are
both found in the ``<libtorrent/extensions.hpp>`` header.

These plugins are instantiated for each torrent and possibly each peer,
respectively.

This is done by passing in a function or function object to
``session::add_extension()`` or ``torrent_handle::add_extension()`` (if the
torrent has already been started and you want to hook in the extension at
run-time).

The signature of the function is::

	boost::shared_ptr<torrent_plugin> (*)(torrent*, void*);

The first argument is the internal torrent object, the second argument
is the userdata passed to ``session::add_torrent()`` or
``torrent_handle::add_extension()``.

The function should return a ``boost::shared_ptr<torrent_plugin>`` which
may or may not be 0. If it is a null pointer, the extension is simply ignored
for this torrent. If it is a valid pointer (to a class inheriting
``torrent_plugin``), it will be associated with this torrent and callbacks
will be made on torrent events.


torrent_plugin
==============

The synopsis for ``torrent_plugin`` follows::

	struct torrent_plugin
	{
		virtual ~torrent_plugin();
		virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection*);

		virtual void on_piece_pass(int index);
		virtual void on_piece_failed(int index);

		virtual void tick();

		virtual bool on_pause();
		virtual bool on_resume();

		virtual void on_files_checked();
	};

This is the base class for a torrent_plugin. Your derived class is (if added
as an extension) instantiated for each torrent in the session. The callback
hook functions are defined as follows.


new_connection()
----------------

::

	boost::shared_ptr<peer_plugin> new_connection(peer_connection*);

This function is called each time a new peer is connected to the torrent. You
may choose to ignore this by just returning a default constructed
``shared_ptr`` (in which case you don't need to override this member
function).

If you need an extension to the peer connection (which most plugins do) you
are supposed to return an instance of your ``peer_plugin`` class. Which in
turn will have its hook functions called on event specific to that peer.

The ``peer_connection`` will be valid as long as the ``shared_ptr`` is being
held by the torrent object. So, it is generally a good idea to not keep a
``shared_ptr`` to your own peer_plugin. If you want to keep references to it,
use ``weak_ptr``.

If this function throws an exception, the connection will be closed.

on_piece_pass() on_piece_fail()
-------------------------------

::

	void on_piece_pass(int index);
	void on_piece_failed(int index);

These hooks are called when a piece passes the hash check or fails the hash
check, respectively. The ``index`` is the piece index that was downloaded.
It is possible to access the list of peers that participated in sending the
piece through the ``torrent`` and the ``piece_picker``.

tick()
------

::

	void tick();

This hook is called approximately once per second. It is a way of making it
easy for plugins to do timed events, for sending messages or whatever.


on_pause() on_resume()
----------------------

::

	bool on_pause();
	bool on_resume();

These hooks are called when the torrent is paused and unpaused respectively.
The return value indicates if the event was handled. A return value of
``true`` indicates that it was handled, and no other plugin after this one
will have this hook function called, and the standard handler will also not be
invoked. So, returning true effectively overrides the standard behavior of
pause or unpause.

Note that if you call ``pause()`` or ``resume()`` on the torrent from your
handler it will recurse back into your handler, so in order to invoke the
standard handler, you have to keep your own state on whether you want standard
behavior or overridden behavior.

on_files_checked()
------------------

::

	void on_files_checked();

This function is called when the initial files of the torrent have been
checked. If there are no files to check, this function is called immediately.

i.e. This function is always called when the torrent is in a state where it
can start downloading.


peer_plugin
===========

::

	struct peer_plugin
	{
		virtual ~peer_plugin();

		virtual void add_handshake(entry&);
		virtual bool on_handshake(char const* reserved_bits);
		virtual bool on_extension_handshake(lazy_entry const& h);

		virtual bool on_choke();
		virtual bool on_unchoke();
		virtual bool on_interested();
		virtual bool on_not_interested();
		virtual bool on_have(int index);
		virtual bool on_bitfield(bitfield const& bits);
		virtual bool on_have_all();
		virtual bool on_have_none();
		virtual bool on_allowed_fast(int index);
		virtual bool on_request(peer_request const& req);
		virtual bool on_piece(peer_request const& piece, disk_buffer_holder& buffer);
		virtual bool on_cancel(peer_request const& req);
		virtual bool on_reject(peer_request const& req);
		virtual bool on_suggest(int index);
		virtual bool on_extended(int length
			, int msg, buffer::const_interval body);
		virtual bool on_unknown_message(int length, int msg
			, buffer::const_interval body);
		virtual void on_piece_pass(int index);
		virtual void on_piece_failed(int index);

		virtual void tick();

		virtual bool write_request(peer_request const& r);
	};

disk_buffer_holder
==================

::

	struct disk_buffer_holder
	{
		disk_buffer_holder(aux::session_impl& s, char* b);
		~disk_buffer_holder();
		char* release();
		char* buffer();
	};

The disk buffer holder acts like a ``scoped_ptr`` that frees a disk buffer
when it's destructed, unless it's released. ``release`` returns the disk
buffer and transferres ownership and responsibility to free it to the caller.

A disk buffer is freed by passing it to ``session_impl::free_disk_buffer()``.

``buffer()`` returns the pointer without transferring responsibility. If
this buffer has been released, ``buffer()`` will return 0.

