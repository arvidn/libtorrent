/*

Copyright (c) 2007, Arvid Norberg
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vector>
#include <utility>
#include <numeric>
#include <cstdio>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/alert_types.hpp"
#ifdef TORRENT_STATS
#include "libtorrent/aux_/session_impl.hpp"
#endif

namespace libtorrent { namespace
{
	int div_round_up(int numerator, int denominator)
	{
		return (numerator + denominator - 1) / denominator;
	}

	void nop(char*) {}

	struct ut_metadata_plugin : torrent_plugin
	{
		ut_metadata_plugin(torrent& t)
			: m_torrent(t)
			, m_metadata_progress(0)
			, m_metadata_size(0)
		{
		}

		virtual void on_files_checked()
		{
			// if the torrent is a seed, copy the metadata from
			// the torrent before it is deallocated
			if (m_torrent.is_seed())
				metadata();
		}

		virtual boost::shared_ptr<peer_plugin> new_connection(
			peer_connection* pc);
		
		buffer::const_interval metadata() const
		{
			TORRENT_ASSERT(m_torrent.valid_metadata());
			if (!m_metadata)
			{
				m_metadata = m_torrent.torrent_file().metadata();
				m_metadata_size = m_torrent.torrent_file().metadata_size();
				TORRENT_ASSERT(hasher(m_metadata.get(), m_metadata_size).final()
					== m_torrent.torrent_file().info_hash());
			}
			return buffer::const_interval(m_metadata.get(), m_metadata.get()
				+ m_metadata_size);
		}

		bool received_metadata(char const* buf, int size, int piece, int total_size)
		{
			if (m_torrent.valid_metadata()) return false;
			
			if (!m_metadata)
			{
				// verify the total_size
				if (total_size <= 0 || total_size > 500 * 1024) return false;

				m_metadata.reset(new char[total_size]);
				m_requested_metadata.resize(div_round_up(total_size, 16 * 1024), 0);
				m_metadata_size = total_size;
			}

			if (piece < 0 || piece >= int(m_requested_metadata.size()))
				return false;

			if (total_size != m_metadata_size)
			{
				// they disagree about the size!
				return false;
			}

			if (piece * 16 * 1024 + size > m_metadata_size)
			{
				// this piece is invalid 
				return false;
			}

			std::memcpy(&m_metadata[piece * 16 * 1024], buf, size);
			// mark this piece has 'have'
			m_requested_metadata[piece] = (std::numeric_limits<int>::max)();

			bool have_all = std::count(m_requested_metadata.begin()
				, m_requested_metadata.end(), (std::numeric_limits<int>::max)())
				== int(m_requested_metadata.size());

			if (!have_all) return false;

			hasher h;
			h.update(&m_metadata[0], m_metadata_size);
			sha1_hash info_hash = h.final();

			if (info_hash != m_torrent.torrent_file().info_hash())
			{
				std::fill(m_requested_metadata.begin(), m_requested_metadata.end(), 0);

				if (m_torrent.alerts().should_post<metadata_failed_alert>())
				{
					m_torrent.alerts().post_alert(metadata_failed_alert(
						m_torrent.get_handle()));
				}

				return false;
			}

			lazy_entry metadata;
			int ret = lazy_bdecode(m_metadata.get(), m_metadata.get() + m_metadata_size, metadata);
			std::string error;
			if (!m_torrent.set_metadata(metadata, error))
			{
				// this means the metadata is correct, since we
				// verified it against the info-hash, but we
				// failed to parse it. Pause the torrent
				// TODO: Post an alert!
				m_torrent.pause();
				return false;
			}

			// clear the storage for the bitfield
			std::vector<int>().swap(m_requested_metadata);

			return true;
		}

		// returns a piece of the metadata that
		// we should request.
		int metadata_request();

		// this is called from the peer_connection for
		// each piece of metadata it receives
		void metadata_progress(int total_size, int received)
		{
			m_metadata_progress += received;
			m_metadata_size = total_size;
		}

		void on_piece_pass(int)
		{
			// if we became a seed, copy the metadata from
			// the torrent before it is deallocated
			if (m_torrent.is_seed())
				metadata();
		}

		void metadata_size(int size)
		{
			if (m_metadata_size > 0 || size <= 0 || size > 500 * 1024) return;
			m_metadata_size = size;
			m_metadata.reset(new char[size]);
			m_requested_metadata.resize(div_round_up(size, 16 * 1024), 0);
		}

	private:
		torrent& m_torrent;

		// this buffer is filled with the info-section of
		// the metadata file while downloading it from
		// peers, and while sending it.
		// it is mutable because it's generated lazily
		mutable boost::shared_array<char> m_metadata;

		int m_metadata_progress;
		mutable int m_metadata_size;

		// this vector keeps track of how many times each meatdata
		// block has been requested
		// std::numeric_limits<int>::max() means we have the piece
		std::vector<int> m_requested_metadata;
	};


	struct ut_metadata_peer_plugin : peer_plugin
	{
		ut_metadata_peer_plugin(torrent& t, bt_peer_connection& pc
			, ut_metadata_plugin& tp)
			: m_message_index(0)
			, m_no_metadata(min_time())
			, m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
		{}

		// can add entries to the extension handshake
		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages["ut_metadata"] = 15;
			if (m_torrent.valid_metadata())
				h["metadata_size"] = m_tp.metadata().left();
		}

		// called when the extension handshake from the other end is received
		virtual bool on_extension_handshake(lazy_entry const& h)
		{
			m_message_index = 0;
			if (h.type() != lazy_entry::dict_t) return false;
			lazy_entry const* messages = h.dict_find("m");
			if (!messages || messages->type() != lazy_entry::dict_t) return false;

			int index = messages->dict_find_int_value("ut_metadata", -1);
			if (index == -1) return false;
			m_message_index = index;

			int metadata_size = h.dict_find_int_value("metadata_size");
			if (metadata_size > 0)
				m_tp.metadata_size(metadata_size);
			return true;
		}

		void write_metadata_packet(int type, int piece)
		{
			TORRENT_ASSERT(type >= 0 && type <= 2);
			TORRENT_ASSERT(piece >= 0);
			TORRENT_ASSERT(!m_pc.associated_torrent().expired());

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_pc.m_logger) << time_now_string() << " ==> UT_METADATA [ "
				"type: " << type << " | piece: " << piece << " ]\n";
#endif
			// abort if the peer doesn't support the metadata extension
			if (m_message_index == 0) return;

			entry e;
			e["msg_type"] = type;
			e["piece"] = piece;

			char const* metadata = 0;
			int metadata_piece_size = 0;

			if (type == 1)
			{
				TORRENT_ASSERT(m_pc.associated_torrent().lock()->valid_metadata());
				e["total_size"] = m_tp.metadata().left();
				int offset = piece * 16 * 1024;
				metadata = m_tp.metadata().begin + offset;
				metadata_piece_size = (std::min)(
					int(m_tp.metadata().left() - offset), 16 * 1024);
				TORRENT_ASSERT(metadata_piece_size > 0);
				TORRENT_ASSERT(offset >= 0);
				TORRENT_ASSERT(offset + metadata_piece_size <= int(m_tp.metadata().left()));
			}

			char msg[200];
			char* header = msg;
			char* p = &msg[6];
			int len = bencode(p, e);
			int total_size = 2 + len + metadata_piece_size;
			namespace io = detail;
			io::write_uint32(total_size, header);
			io::write_uint8(bt_peer_connection::msg_extended, header);
			io::write_uint8(m_message_index, header);

			m_pc.send_buffer(msg, len + 6);
			if (metadata_piece_size) m_pc.append_send_buffer(
				(char*)metadata, metadata_piece_size, &nop);
		}

		virtual bool on_extended(int length
			, int extended_msg, buffer::const_interval body)
		{
			if (extended_msg != 15) return false;
			if (m_message_index == 0) return false;

			if (length > 17 * 1024)
			{
				m_pc.disconnect("ut_metadata message larger than 17 kB", 2);
				return true;
			}

			if (!m_pc.packet_finished()) return true;

			int len;
			entry msg = bdecode(body.begin, body.end, len);
			if (msg.type() == entry::undefined_t)
			{
				m_pc.disconnect("invalid bencoding in ut_metadata message", 2);
				return true;
			}

			int type = msg["msg_type"].integer();
			int piece = msg["piece"].integer();

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_pc.m_logger) << time_now_string() << " <== UT_METADATA [ "
				"type: " << type << " | piece: " << piece << " ]\n";
#endif

			switch (type)
			{
			case 0: // request
				{
					if (!m_torrent.valid_metadata())
					{
						write_metadata_packet(2, piece);
						return true;
					}
					// TODO: put the request on the queue in some cases
					write_metadata_packet(1, piece);
				}
				break;
			case 1: // data
				{
					std::vector<int>::iterator i = std::find(m_sent_requests.begin()
						, m_sent_requests.end(), piece);

					// unwanted piece?
					if (i == m_sent_requests.end()) return true;

					m_sent_requests.erase(i);
					entry const* total_size = msg.find_key("total_size");
					m_tp.received_metadata(body.begin + len, body.left() - len, piece
						, (total_size && total_size->type() == entry::int_t) ? total_size->integer() : 0);
				}
				break;
			case 2: // have no data
				{
					m_no_metadata = time_now();
					std::vector<int>::iterator i = std::find(m_sent_requests.begin()
						, m_sent_requests.end(), piece);
					// unwanted piece?
					if (i == m_sent_requests.end()) return true;
					m_sent_requests.erase(i);
				}
				break;
			default:
				{
					std::stringstream msg;
					msg << "unknown ut_metadata extension message: " << type;
					m_pc.disconnect(msg.str().c_str(), 2);
				}
			}
			return true;
		}

		virtual void tick()
		{
			// if we don't have any metadata, and this peer
			// supports the request metadata extension
			// and we aren't currently waiting for a request
			// reply. Then, send a request for some metadata.
			if (!m_torrent.valid_metadata()
				&& m_message_index != 0
				&& m_sent_requests.size() < 2
				&& has_metadata())
			{
				int piece = m_tp.metadata_request();
				m_sent_requests.push_back(piece);
				write_metadata_packet(0, piece);
			}
		}

		bool has_metadata() const
		{
			return time_now() - m_no_metadata > minutes(1);
		}

	private:

		// this is the message index the remote peer uses
		// for metadata extension messages.
		int m_message_index;

		// this is set to the current time each time we get a
		// "I don't have metadata" message.
		ptime m_no_metadata;

		// request queues
		std::vector<int> m_sent_requests;
		std::vector<int> m_incoming_requests;
		
		torrent& m_torrent;
		bt_peer_connection& m_pc;
		ut_metadata_plugin& m_tp;
	};

	boost::shared_ptr<peer_plugin> ut_metadata_plugin::new_connection(
		peer_connection* pc)
	{
		bt_peer_connection* c = dynamic_cast<bt_peer_connection*>(pc);
		if (!c) return boost::shared_ptr<peer_plugin>();
		return boost::shared_ptr<peer_plugin>(new ut_metadata_peer_plugin(m_torrent, *c, *this));
	}

	int ut_metadata_plugin::metadata_request()
	{
		std::vector<int>::iterator i = std::min_element(
			m_requested_metadata.begin(), m_requested_metadata.end());

		if (m_requested_metadata.empty())
		{
			// if we don't know how many pieces there are
			// just ask for piece 0
			m_requested_metadata.resize(1, 1);
			return 0;
		}

		int piece = i - m_requested_metadata.begin();
		m_requested_metadata[piece] = piece;
		return piece;
	}

} }

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> create_ut_metadata_plugin(torrent* t, void*)
	{
		// don't add this extension if the torrent is private
		if (t->valid_metadata() && t->torrent_file().priv()) return boost::shared_ptr<torrent_plugin>();
		return boost::shared_ptr<torrent_plugin>(new ut_metadata_plugin(*t));
	}

}


