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

#include "libtorrent/pch.hpp"

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vector>

#include "libtorrent/extensions/logger.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/peer_connection.hpp"

namespace libtorrent {

namespace fs = boost::filesystem;

namespace
{

	struct logger_peer_plugin : peer_plugin
	{
		logger_peer_plugin(std::string const& filename)
		{
			fs::path dir(fs::complete("libtorrent_ext_logs"));
			if (!fs::exists(dir)) fs::create_directories(dir);
			m_file.open((dir / filename).string().c_str(), std::ios_base::out | std::ios_base::out);
			m_file << "\n\n\n";
			log_timestamp();
			m_file << "*** starting log ***\n";
		}

		void log_timestamp()
		{
			m_file << time_now_string() << ": ";
		}

		// can add entries to the extension handshake
		virtual void add_handshake(entry&) {}
		
		// called when the extension handshake from the other end is received
		virtual bool on_extension_handshake(lazy_entry const& h)
		{
			log_timestamp();
			m_file << "<== EXTENSION_HANDSHAKE\n" << h;
			return true;
		}

		// returning true from any of the message handlers
		// indicates that the plugin has handeled the message.
		// it will break the plugin chain traversing and not let
		// anyone else handle the message, including the default
		// handler.

		virtual bool on_choke()
		{
			log_timestamp();
			m_file << "<== CHOKE\n";
			m_file.flush();
			return false;
		}

		virtual bool on_unchoke()
		{
			log_timestamp();
			m_file << "<== UNCHOKE\n";
			m_file.flush();
			return false;
		}

		virtual bool on_interested()
		{
			log_timestamp();
			m_file << "<== INTERESTED\n";
			m_file.flush();
			return false;
		}

		virtual bool on_not_interested()
		{
			log_timestamp();
			m_file << "<== NOT_INTERESTED\n";
			m_file.flush();
			return false;
		}

		virtual bool on_have(int index)
		{
			log_timestamp();
			m_file << "<== HAVE [" << index << "]\n";
			m_file.flush();
			return false;
		}

		virtual bool on_bitfield(std::vector<bool> const& bitfield)
		{
			log_timestamp();
			m_file << "<== BITFIELD\n";
			m_file.flush();
			return false;
		}

		virtual bool on_request(peer_request const& r)
		{
			log_timestamp();
			m_file << "<== REQUEST [ piece: " << r.piece << " | s: " << r.start
				<< " | l: " << r.length << " ]\n";
			m_file.flush();
			return false;
		}

		virtual bool on_piece(peer_request const& r, char const*)
		{
			log_timestamp();
			m_file << "<== PIECE [ piece: " << r.piece << " | s: " << r.start
				<< " | l: " << r.length << " ]\n";
			m_file.flush();
			return false;
		}

		virtual bool on_cancel(peer_request const& r)
		{
			log_timestamp();
			m_file << "<== CANCEL [ piece: " << r.piece << " | s: " << r.start
				<< " | l: " << r.length << " ]\n";
			m_file.flush();
			return false;
		}
	
		// called when an extended message is received. If returning true,
		// the message is not processed by any other plugin and if false
		// is returned the next plugin in the chain will receive it to
		// be able to handle it
		virtual bool on_extended(int length
			, int msg, buffer::const_interval body)
		{ return false; }

		virtual bool on_unknown_message(int length, int msg
			, buffer::const_interval body)
		{
			if (body.left() < length) return false;
			log_timestamp();
			m_file << "<== UNKNOWN [ msg: " << msg
				<< " | l: " << length << " ]\n";
			m_file.flush();
			return false;
		}

		virtual void on_piece_pass(int index)
		{
			log_timestamp();
			m_file << "*** HASH PASSED *** [ piece: " << index << " ]\n";
			m_file.flush();
		}

		virtual void on_piece_failed(int index)
		{
			log_timestamp();
			m_file << "*** HASH FAILED *** [ piece: " << index << " ]\n";
			m_file.flush();
		}

	private:
		std::ofstream m_file;
	};

	struct logger_plugin : torrent_plugin
	{
		virtual boost::shared_ptr<peer_plugin> new_connection(
			peer_connection* pc)
		{
			error_code ec;
			return boost::shared_ptr<peer_plugin>(new logger_peer_plugin(
				pc->remote().address().to_string(ec) + "_"
				+ to_string(pc->remote().port()).elems + ".log"));
		}
	};

} }

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> create_logger_plugin(torrent*)
	{
		return boost::shared_ptr<torrent_plugin>(new logger_plugin());
	}

}


