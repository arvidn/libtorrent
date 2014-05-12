/*

Copyright (c) 2006-2014, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_EXTENSIONS_HPP_INCLUDED
#define TORRENT_EXTENSIONS_HPP_INCLUDED

// OVERVIEW
// 
// libtorrent has a plugin interface for implementing extensions to the protocol.
// These can be general extensions for transferring metadata or peer exchange
// extensions, or it could be used to provide a way to customize the protocol
// to fit a particular (closed) network.
// 
// In short, the plugin interface makes it possible to:
// 
// * register extension messages (sent in the extension handshake), see
//   extensions_.
// * add data and parse data from the extension handshake.
// * send extension messages and standard bittorrent messages.
// * override or block the handling of standard bittorrent messages.
// * save and restore state via the session state
// * see all alerts that are posted
// 
// .. _extensions: extension_protocol.html
// 
// a word of caution
// -----------------
// 
// Writing your own plugin is a very easy way to introduce serious bugs such as
// dead locks and race conditions. Since a plugin has access to internal
// structures it is also quite easy to sabotage libtorrent's operation.
// 
// All the callbacks in this interface are called with the main libtorrent thread
// mutex locked. And they are always called from the libtorrent network thread. In
// case portions of your plugin are called from other threads, typically the main
// thread, you cannot use any of the member functions on the internal structures
// in libtorrent, since those require the mutex to be locked. Futhermore, you would
// also need to have a mutex on your own shared data within the plugin, to make
// sure it is not accessed at the same time from the libtorrent thread (through a
// callback). See `boost thread's mutex`_. If you need to send out a message from
// another thread, it is advised to use an internal queue, and do the actual
// sending in ``tick()``.
// 
// Since the plugin interface gives you easy access to internal structures, it
// is not supported as a stable API. Plugins should be considered spcific to a
// specific version of libtorrent. Although, in practice the internals mostly
// don't change that dramatically.
// 
// .. _`boost thread's mutex`: http://www.boost.org/doc/html/mutex.html
// 
// 
// plugin-interface
// ================
// 
// The plugin interface consists of three base classes that the plugin may
// implement. These are called plugin, torrent_plugin and peer_plugin.
// They are found in the ``<libtorrent/extensions.hpp>`` header.
// 
// These plugins are instantiated for each session, torrent and possibly each peer,
// respectively.
// 
// For plugins that only need per torrent state, it is enough to only implement
// ``torrent_plugin`` and pass a constructor function or function object to
// ``session::add_extension()`` or ``torrent_handle::add_extension()`` (if the
// torrent has already been started and you want to hook in the extension at
// run-time).
// 
// The signature of the function is::
// 
// 	boost::shared_ptr<torrent_plugin> (*)(torrent*, void*);
// 
// The first argument is the internal torrent object, the second argument
// is the userdata passed to ``session::add_torrent()`` or
// ``torrent_handle::add_extension()``.
// 
// The function should return a ``boost::shared_ptr<torrent_plugin>`` which
// may or may not be 0. If it is a null pointer, the extension is simply ignored
// for this torrent. If it is a valid pointer (to a class inheriting
// ``torrent_plugin``), it will be associated with this torrent and callbacks
// will be made on torrent events.
// 
// For more elaborate plugins which require session wide state, you would
// implement ``plugin``, construct an object (in a ``boost::shared_ptr``) and pass
// it in to ``session::add_extension()``.
// 
// custom alerts
// =============
// 
// Since plugins are running within internal libtorrent threads, one convenient
// way to communicate with the client is to post custom alerts.
// 
// The expected interface of any alert, apart from deriving from the alert
// base class, looks like this:
// 
// .. parsed-literal::
// 
// 	const static int alert_type = *<unique alert ID>*;
// 	virtual int type() const { return alert_type; }
// 
// 	virtual std::string message() const;
// 
// 	virtual std::auto_ptr<alert> clone() const
// 	{ return std::auto_ptr<alert>(new name(\*this)); }
// 
// 	const static int static_category = *<bitmask of alert::category_t flags>*;
// 	virtual int category() const { return static_category; }
// 
// 	virtual char const* what() const { return *<string literal of the name of this alert>*; }
// 
// The ``alert_type`` is used for the type-checking in ``alert_cast``. It must
// not collide with any other alert. The built-in alerts in libtorrent will
// not use alert type IDs greater than ``user_alert_id``. When defining your
// own alert, make sure it's greater than this constant.
// 
// ``type()`` is the run-time equivalence of the ``alert_type``.
// 
// The ``message()`` virtual function is expected to construct a useful
// string representation of the alert and the event or data it represents.
// Something convenient to put in a log file for instance.
// 
// ``clone()`` is used internally to copy alerts. The suggested implementation
// of simply allocating a new instance as a copy of ``*this`` is all that's
// expected.
// 
// The static category is required for checking wether or not the category
// for a specific alert is enabled or not, without instantiating the alert.
// The ``category`` virtual function is the run-time equivalence.
// 
// The ``what()`` virtual function may simply be a string literal of the class
// name of your alert.
// 
// For more information, see the `alert section`_.
// 
// .. _`alert section`: reference-Alerts.html


#ifndef TORRENT_DISABLE_EXTENSIONS

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/weak_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vector>
#include "libtorrent/config.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/policy.hpp" // for policy::peer

namespace libtorrent
{
	namespace aux { struct session_impl; }

	struct peer_plugin;
	class bt_peer_connection;
	struct peer_request;
	class peer_connection;
	class entry;
	struct lazy_entry;
	struct disk_buffer_holder;
	struct bitfield;
	class alert;
	struct torrent_plugin;
	class torrent;
	struct torrent_peer;

	// this is the base class for a session plugin. One primary feature
	// is that it is notified of all torrents that are added to the session,
	// and can add its own torrent_plugins.
	struct TORRENT_EXPORT plugin
	{
		// hidden
		virtual ~plugin() {}

		// this is called by the session every time a new torrent is added.
		// The ``torrent*`` points to the internal torrent object created
		// for the new torrent. The ``void*`` is the userdata pointer as
		// passed in via add_torrent_params.
		//
		// If the plugin returns a torrent_plugin instance, it will be added
		// to the new torrent. Otherwise, return an empty shared_ptr to a
		// torrent_plugin (the default).
		virtual boost::shared_ptr<torrent_plugin> new_torrent(torrent*, void*)
		{ return boost::shared_ptr<torrent_plugin>(); }

		// called when plugin is added to a session
		virtual void added(aux::session_impl*) {}

		// called when an alert is posted
		// alerts that are filtered are not
		// posted
		virtual void on_alert(alert const*) {}

		// called once per second
		virtual void on_tick() {}

		// called when choosing peers to optimisticly unchoke
		// peer's will be unchoked in the order they appear in the given
		// vector which is initiallity sorted by when they were last
		// optimistically unchoked.
		// if the plugin returns true then the ordering provided will be
		// used and no other plugin will be allowed to change it.
		virtual bool on_optimistic_unchoke(std::vector<policy::peer*>& /* peers */)
		{ return false; }

		// called when saving settings state
		virtual void save_state(entry&) const {}

		// called when loading settings state
		virtual void load_state(lazy_entry const&) {}
	};

	// Torrent plugins are associated with a single torrent and have a number
	// of functions called at certain events. Many of its functions have the
	// ability to change or override the default libtorrent behavior.
	struct TORRENT_EXPORT torrent_plugin
	{
		// hidden
		virtual ~torrent_plugin() {}

		// This function is called each time a new peer is connected to the torrent. You
		// may choose to ignore this by just returning a default constructed
		// ``shared_ptr`` (in which case you don't need to override this member
		// function).
		// 
		// If you need an extension to the peer connection (which most plugins do) you
		// are supposed to return an instance of your peer_plugin class. Which in
		// turn will have its hook functions called on event specific to that peer.
		// 
		// The ``peer_connection`` will be valid as long as the ``shared_ptr`` is being
		// held by the torrent object. So, it is generally a good idea to not keep a
		// ``shared_ptr`` to your own peer_plugin. If you want to keep references to it,
		// use ``weak_ptr``.
		// 
		// If this function throws an exception, the connection will be closed.
		virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection*)
		{ return boost::shared_ptr<peer_plugin>(); }

		// These hooks are called when a piece passes the hash check or fails the hash
		// check, respectively. The ``index`` is the piece index that was downloaded.
		// It is possible to access the list of peers that participated in sending the
		// piece through the ``torrent`` and the ``piece_picker``.
		virtual void on_piece_pass(int /*index*/) {}
		virtual void on_piece_failed(int /*index*/) {}

		// This hook is called approximately once per second. It is a way of making it
		// easy for plugins to do timed events, for sending messages or whatever.
		virtual void tick() {}

		// These hooks are called when the torrent is paused and unpaused respectively.
		// The return value indicates if the event was handled. A return value of
		// ``true`` indicates that it was handled, and no other plugin after this one
		// will have this hook function called, and the standard handler will also not be
		// invoked. So, returning true effectively overrides the standard behavior of
		// pause or unpause.
		// 
		// Note that if you call ``pause()`` or ``resume()`` on the torrent from your
		// handler it will recurse back into your handler, so in order to invoke the
		// standard handler, you have to keep your own state on whether you want standard
		// behavior or overridden behavior.
		virtual bool on_pause() { return false; }
		virtual bool on_resume() { return false; }

		// This function is called when the initial files of the torrent have been
		// checked. If there are no files to check, this function is called immediately.
		// 
		// i.e. This function is always called when the torrent is in a state where it
		// can start downloading.
		virtual void on_files_checked() {}

		// called when the torrent changes state
		// the state is one of torrent_status::state_t
		// enum members
		virtual void on_state(int /*s*/) {}

		enum flags_t {
			// this is the first time we see this peer
			first_time = 1,
			// this peer was not added because it was
			// filtered by the IP filter
			filtered = 2
		};

		// called every time a new peer is added to the peer list.
		// This is before the peer is connected to. For ``flags``, see
		// torrent_plugin::flags_t. The ``source`` argument refers to
		// the source where we learned about this peer from. It's a
		// bitmask, because many sources may have told us about the same
		// peer. For peer source flags, see peer_info::peer_source_flags.
		virtual void on_add_peer(tcp::endpoint const&,
			int /*src*/, int /*flags*/) {}
	};

	// peer plugins are associated with a specific peer. A peer could be
	// both a regular bittorrent peer (``bt_peer_connection``) or one of the
	// web seed connections (``web_peer_connection`` or ``http_seed_connection``).
	// In order to only attach to certain peers, make your
	// torrent_plugin::new_connection only return a plugin for certain peer
	// connection types
	struct TORRENT_EXPORT peer_plugin
	{
		// hidden
		virtual ~peer_plugin() {}

		// This function is expected to return the name of
		// the plugin.
		virtual char const* type() const { return ""; }

		// can add entries to the extension handshake
		// this is not called for web seeds
		virtual void add_handshake(entry&) {}
		
		// called when the peer is being disconnected.
		virtual void on_disconnect(error_code const& /*ec*/) {}

		// called when the peer is successfully connected. Note that
		// incoming connections will have been connected by the time
		// the peer plugin is attached to it, and won't have this hook
		// called.
		virtual void on_connected() {}

		// throwing an exception from any of the handlers (except add_handshake)
		// closes the connection
		
		// this is called when the initial BT handshake is received. Returning false
		// means that the other end doesn't support this extension and will remove
		// it from the list of plugins.
		// this is not called for web seeds
		virtual bool on_handshake(char const* /*reserved_bits*/) { return true; }
		
		// called when the extension handshake from the other end is received
		// if this returns false, it means that this extension isn't
		// supported by this peer. It will result in this peer_plugin
		// being removed from the peer_connection and destructed. 
		// this is not called for web seeds
		virtual bool on_extension_handshake(lazy_entry const&) { return true; }

		// returning true from any of the message handlers
		// indicates that the plugin has handeled the message.
		// it will break the plugin chain traversing and not let
		// anyone else handle the message, including the default
		// handler.
		virtual bool on_choke() { return false; }
		virtual bool on_unchoke() { return false; }
		virtual bool on_interested() { return false; }
		virtual bool on_not_interested() { return false; }
		virtual bool on_have(int /*index*/) { return false; }
		virtual bool on_dont_have(int /*index*/) { return false; }
		virtual bool on_bitfield(bitfield const& /*bitfield*/) { return false; }
		virtual bool on_have_all() { return false; }
		virtual bool on_have_none() { return false; }
		virtual bool on_allowed_fast(int /*index*/) { return false; }
		virtual bool on_request(peer_request const&) { return false; }
		virtual bool on_piece(peer_request const& /*piece*/
			, disk_buffer_holder& /*data*/) { return false; }
		virtual bool on_cancel(peer_request const&) { return false; }
		virtual bool on_reject(peer_request const&) { return false; }
		virtual bool on_suggest(int /*index*/) { return false; }

		// called after a choke message has been sent to the peer
		virtual void sent_unchoke() {}

		// called when libtorrent think this peer should be disconnected.
		// if the plugin returns false, the peer will not be disconnected.
		virtual bool can_disconnect(error_code const& /*ec*/) { return true; }

		// called when an extended message is received. If returning true,
		// the message is not processed by any other plugin and if false
		// is returned the next plugin in the chain will receive it to
		// be able to handle it
		// this is not called for web seeds
		virtual bool on_extended(int /*length*/, int /*msg*/,
			buffer::const_interval /*body*/)
		{ return false; }

		// this is not called for web seeds
		virtual bool on_unknown_message(int /*length*/, int /*msg*/,
			buffer::const_interval /*body*/)
		{ return false; }

		// called when a piece that this peer participated in either
		// fails or passes the hash_check
		virtual void on_piece_pass(int /*index*/) {}
		virtual void on_piece_failed(int /*index*/) {}

		// called aproximately once every second
		virtual void tick() {}

		// called each time a request message is to be sent. If true
		// is returned, the original request message won't be sent and
		// no other plugin will have this function called.
		virtual bool write_request(peer_request const&) { return false; }
	};

}

#endif

#endif // TORRENT_EXTENSIONS_HPP_INCLUDED

