/*

Copyright (c) 2006, Arvid Norberg
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

#ifndef TORRENT_DISABLE_EXTENSIONS

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vector>
#include "libtorrent/config.hpp"
#include "libtorrent/buffer.hpp"

namespace libtorrent
{
	struct peer_plugin;
	class bt_peer_connection;
	struct peer_request;
	class peer_connection;
	class entry;
	struct lazy_entry;
	struct disk_buffer_holder;
	struct bitfield;

	struct TORRENT_EXPORT torrent_plugin
	{
		virtual ~torrent_plugin() {}
		// throwing an exception closes the connection
		// returning a 0 pointer is valid and will not add
		// the peer_plugin to the peer_connection
		virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection*)
		{ return boost::shared_ptr<peer_plugin>(); }

		virtual void on_piece_pass(int index) {}
		virtual void on_piece_failed(int index) {}

		// called aproximately once every second
		virtual void tick() {}

		// if true is returned, it means the handler handled the event,
		// and no other plugins will have their handlers called, and the
		// default behavior will be skipped
		virtual bool on_pause() { return false; }
		virtual bool on_resume() { return false;}

		// this is called when the initial checking of
		// files is completed.
		virtual void on_files_checked() {}
	};

	struct TORRENT_EXPORT peer_plugin
	{
		virtual ~peer_plugin() {}

		// can add entries to the extension handshake
		// this is not called for web seeds
		virtual void add_handshake(entry&) {}
		
		// throwing an exception from any of the handlers (except add_handshake)
		// closes the connection
		
		// this is called when the initial BT handshake is received. Returning false
		// means that the other end doesn't support this extension and will remove
		// it from the list of plugins.
		// this is not called for web seeds
		virtual bool on_handshake(char const* reserved_bits) { return true; }
		
		// called when the extension handshake from the other end is received
		// if this returns false, it means that this extension isn't
		// supported by this peer. It will result in this peer_plugin
		// being removed from the peer_connection and destructed. 
		// this is not called for web seeds
		virtual bool on_extension_handshake(lazy_entry const& h) { return true; }

		// returning true from any of the message handlers
		// indicates that the plugin has handeled the message.
		// it will break the plugin chain traversing and not let
		// anyone else handle the message, including the default
		// handler.

		virtual bool on_choke()
		{ return false; }

		virtual bool on_unchoke()
		{ return false; }

		virtual bool on_interested()
		{ return false; }

		virtual bool on_not_interested()
		{ return false; }

		virtual bool on_have(int index)
		{ return false; }

		virtual bool on_bitfield(bitfield const& bitfield)
		{ return false; }

		virtual bool on_have_all()
		{ return false; }

		virtual bool on_have_none()
		{ return false; }

		virtual bool on_allowed_fast(int index)
		{ return false; }

		virtual bool on_request(peer_request const& req)
		{ return false; }

		virtual bool on_piece(peer_request const& piece, disk_buffer_holder& data)
		{ return false; }

		virtual bool on_cancel(peer_request const& req)
		{ return false; }
	
		virtual bool on_reject(peer_request const& req)
		{ return false; }

		virtual bool on_suggest(int index)
		{ return false; }

		// called when an extended message is received. If returning true,
		// the message is not processed by any other plugin and if false
		// is returned the next plugin in the chain will receive it to
		// be able to handle it
		// this is not called for web seeds
		virtual bool on_extended(int length
			, int msg, buffer::const_interval body)
		{ return false; }

		// this is not called for web seeds
		virtual bool on_unknown_message(int length, int msg
			, buffer::const_interval body)
		{ return false; }

		// called when a piece that this peer participated in either
		// fails or passes the hash_check
		virtual void on_piece_pass(int index) {}
		virtual void on_piece_failed(int index) {}

		// called aproximately once every second
		virtual void tick() {}

		// called each time a request message is to be sent. If true
		// is returned, the original request message won't be sent and
		// no other plugin will have this function called.
		virtual bool write_request(peer_request const& r) { return false; }
	};

}

#endif

#endif // TORRENT_EXTENSIONS_HPP_INCLUDED

