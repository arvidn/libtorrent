/*

Copyright (c) 2007-2016, Arvid Norberg
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

#ifndef TORRENT_DISABLE_EXTENSIONS

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
#include "libtorrent/random.hpp"
#ifdef TORRENT_STATS
#include "libtorrent/aux_/session_impl.hpp"
#endif

namespace libtorrent { namespace
{
	enum
	{
		// this is the max number of bytes we'll
		// queue up in the send buffer. If we exceed this,
		// we'll wait another second before checking
		// the send buffer size again. So, this may limit
		// the rate at which we can server metadata to
		// 160 kiB/s
		send_buffer_limit = 0x4000 * 10,

		// this is the max number of requests we'll queue
		// up. If we get more requests tha this, we'll
		// start rejecting them, claiming we don't have
		// metadata. If the torrent is greater than 16 MiB,
		// we may hit this case (and the client requesting
		// doesn't throttle its requests)
		max_incoming_requests = 1024
	};

	int div_round_up(int numerator, int denominator)
	{
		return (numerator + denominator - 1) / denominator;
	}

	struct ut_metadata_peer_plugin;

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

		bool received_metadata(ut_metadata_peer_plugin& source
			, char const* buf, int size, int piece, int total_size);

		// returns a piece of the metadata that
		// we should request.
		// returns -1 if we should hold off the request
		int metadata_request(bool has_metadata);

		// this is called from the peer_connection for
		// each piece of metadata it receives
		void metadata_progress(int total_size, int received)
		{
			m_metadata_progress += received;
			m_metadata_size = total_size;
			m_torrent.set_progress_ppm(boost::int64_t(m_metadata_progress) * 1000000 / m_metadata_size);
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
			if (m_metadata_size > 0 || size <= 0 || size > 4 * 1024 * 1024) return;
			m_metadata_size = size;
			m_metadata.reset(new char[size]);
			m_requested_metadata.resize(div_round_up(size, 16 * 1024));
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

		struct metadata_piece
		{
			metadata_piece(): num_requests(0), last_request(0) {}
			int num_requests;
			time_t last_request;
			boost::weak_ptr<ut_metadata_peer_plugin> source;
			bool operator<(metadata_piece const& rhs) const
			{ return num_requests < rhs.num_requests; }
		};

		// this vector keeps track of how many times each meatdata
		// block has been requested and who we ended up getting it from
		// std::numeric_limits<int>::max() means we have the piece
		std::vector<metadata_piece> m_requested_metadata;
	};


	struct ut_metadata_peer_plugin : peer_plugin, boost::enable_shared_from_this<ut_metadata_peer_plugin>
	{
		friend struct ut_metadata_plugin;

		ut_metadata_peer_plugin(torrent& t, bt_peer_connection& pc
			, ut_metadata_plugin& tp)
			: m_message_index(0)
			, m_request_limit(min_time())
			, m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
		{}

		virtual char const* type() const { return "ut_metadata"; }

		// can add entries to the extension handshake
		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages["ut_metadata"] = 2;
			if (m_torrent.valid_metadata())
				h["metadata_size"] = m_tp.metadata().left();
		}

		// called when the extension handshake from the other end is received
		virtual bool on_extension_handshake(lazy_entry const& h)
		{
			m_message_index = 0;
			if (h.type() != lazy_entry::dict_t) return false;
			lazy_entry const* messages = h.dict_find_dict("m");
			if (!messages) return false;

			int index = messages->dict_find_int_value("ut_metadata", -1);
			if (index == -1) return false;
			m_message_index = index;

			int metadata_size = h.dict_find_int_value("metadata_size");
			if (metadata_size > 0)
				m_tp.metadata_size(metadata_size);
			else
				m_pc.set_has_metadata(false);

			maybe_send_request();
			return true;
		}

		void write_metadata_packet(int type, int piece)
		{
			TORRENT_ASSERT(type >= 0 && type <= 2);
			TORRENT_ASSERT(!m_pc.associated_torrent().expired());

#ifdef TORRENT_VERBOSE_LOGGING
			char const* names[] = {"request", "data", "dont-have"};
			char const* n = "";
			if (type >= 0 && type < 3) n = names[type];
			m_pc.peer_log("==> UT_METADATA [ type: %d (%s) | piece: %d ]", type, n, piece);
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

				if (piece < 0 || piece >= int(m_tp.metadata().left() + 16 * 1024 - 1)/(16*1024))
				{
#ifdef TORRENT_VERBOSE_LOGGING
					m_pc.peer_log("*** UT_METADATA [ invalid piece %d metadata size: %d ]"
						, piece, int(m_tp.metadata().left()));
#endif
					m_pc.disconnect(errors::invalid_metadata_message, 2);
					return;
				}

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
			if (metadata_piece_size) m_pc.append_const_send_buffer(
				metadata, metadata_piece_size);
		}

		virtual bool on_extended(int length
			, int extended_msg, buffer::const_interval body)
		{
			if (extended_msg != 2) return false;
			if (m_message_index == 0) return false;

			if (length > 17 * 1024)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				m_pc.peer_log("<== UT_METADATA [ packet too big %d ]", length);
#endif
				m_pc.disconnect(errors::invalid_metadata_message, 2);
				return true;
			}

			if (!m_pc.packet_finished()) return true;

			int len;
			entry msg = bdecode(body.begin, body.end, len);
			if (msg.type() != entry::dictionary_t)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				m_pc.peer_log("<== UT_METADATA [ not a dictionary ]");
#endif
				m_pc.disconnect(errors::invalid_metadata_message, 2);
				return true;
			}

			entry const* type_ent = msg.find_key("msg_type");
			entry const* piece_ent = msg.find_key("piece");
			if (type_ent == 0 || type_ent->type() != entry::int_t
				|| piece_ent == 0 || piece_ent->type() != entry::int_t)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				m_pc.peer_log("<== UT_METADATA [ missing or invalid keys ]");
#endif
				m_pc.disconnect(errors::invalid_metadata_message, 2);
				return true;
			}
			int type = type_ent->integer();
			int piece = piece_ent->integer();

#ifdef TORRENT_VERBOSE_LOGGING
			m_pc.peer_log("<== UT_METADATA [ type: %d | piece: %d ]", type, piece);
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
					if (m_pc.send_buffer_size() < send_buffer_limit)
						write_metadata_packet(1, piece);
					else if (m_incoming_requests.size() < max_incoming_requests)
						m_incoming_requests.push_back(piece);
					else
						write_metadata_packet(2, piece);
				}
				break;
			case 1: // data
				{
					std::vector<int>::iterator i = std::find(m_sent_requests.begin()
						, m_sent_requests.end(), piece);

					// unwanted piece?
					if (i == m_sent_requests.end())
					{
#ifdef TORRENT_VERBOSE_LOGGING
						m_pc.peer_log("*** UT_METADATA [ UNWANTED / TIMED OUT ]");				
#endif
						return true;
					}

					m_sent_requests.erase(i);
					entry const* total_size = msg.find_key("total_size");
					m_tp.received_metadata(*this, body.begin + len, body.left() - len, piece
						, (total_size && total_size->type() == entry::int_t) ? total_size->integer() : 0);
					maybe_send_request();
				}
				break;
			case 2: // have no data
				{
					m_request_limit = (std::max)(time_now() + minutes(1), m_request_limit);
					std::vector<int>::iterator i = std::find(m_sent_requests.begin()
						, m_sent_requests.end(), piece);
					// unwanted piece?
					if (i == m_sent_requests.end()) return true;
					m_sent_requests.erase(i);
				}
				break;
			default:
				// unknown message, ignore
				break;
			}
			return true;
		}

		virtual void tick()
		{
			maybe_send_request();
			while (!m_incoming_requests.empty()
				&& m_pc.send_buffer_size() < send_buffer_limit)
			{
				int piece = m_incoming_requests.front();
				m_incoming_requests.erase(m_incoming_requests.begin());
				write_metadata_packet(1, piece);
			}
		}

		void maybe_send_request()
		{
			if (m_pc.is_disconnecting()) return;

			// if we don't have any metadata, and this peer
			// supports the request metadata extension
			// and we aren't currently waiting for a request
			// reply. Then, send a request for some metadata.
			if (!m_torrent.valid_metadata()
				&& m_message_index != 0
				&& m_sent_requests.size() < 2
				&& has_metadata())
			{
				int piece = m_tp.metadata_request(m_pc.has_metadata());
				if (piece == -1) return;

				m_sent_requests.push_back(piece);
				write_metadata_packet(0, piece);
			}
		}

		bool has_metadata() const
		{
			return m_pc.has_metadata() || (time_now() > m_request_limit);
		}

		void failed_hash_check(ptime const& now)
		{
			m_request_limit = now + seconds(20 + (boost::int64_t(random()) * 50) / UINT_MAX);
		}

	private:

		// this is the message index the remote peer uses
		// for metadata extension messages.
		int m_message_index;

		// this is set to the next time we can request pieces
		// again. It is updated every time we get a
		// "I don't have metadata" message, but also when
		// we receive metadata that fails the infohash check
		ptime m_request_limit;

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
		if (pc->type() != peer_connection::bittorrent_connection)
			return boost::shared_ptr<peer_plugin>();

		bt_peer_connection* c = static_cast<bt_peer_connection*>(pc);
		return boost::shared_ptr<peer_plugin>(new ut_metadata_peer_plugin(m_torrent, *c, *this));
	}

	// has_metadata is false if the peer making the request has not announced
	// that it has metadata. In this case, it shouldn't prevent other peers
	// from requesting this block by setting a timeout on it.
	int ut_metadata_plugin::metadata_request(bool has_metadata)
	{
		std::vector<metadata_piece>::iterator i = std::min_element(
			m_requested_metadata.begin(), m_requested_metadata.end());

		if (m_requested_metadata.empty())
		{
			// if we don't know how many pieces there are
			// just ask for piece 0
			m_requested_metadata.resize(1);
			i = m_requested_metadata.begin();
		}

		int piece = i - m_requested_metadata.begin();

		// don't request the same block more than once every 3 seconds
		time_t now = time(0);
		if (now - m_requested_metadata[piece].last_request < 3) return -1;

		++m_requested_metadata[piece].num_requests;

		// only set the timeout on this block, only if the peer
		// has metadata. This is to prevent peers with no metadata
		// to starve out sending requests to peers with metadata
		if (has_metadata)
			m_requested_metadata[piece].last_request = now;

		return piece;
	}

	inline bool ut_metadata_plugin::received_metadata(
		ut_metadata_peer_plugin& source
		, char const* buf, int size, int piece, int total_size)
	{
		if (m_torrent.valid_metadata())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			source.m_pc.peer_log("*** UT_METADATA [ ALREADY HAVE METADATA ]");				
#endif
			m_torrent.add_redundant_bytes(size, torrent::piece_unknown);
			return false;
		}
		
		if (!m_metadata)
		{
			// verify the total_size
			if (total_size <= 0 || total_size > m_torrent.session().settings().max_metadata_size)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				source.m_pc.peer_log("*** UT_METADATA [ metadata size too big: %d ]", total_size);				
#endif
// #error post alert
				return false;
			}

			m_metadata.reset(new char[total_size]);
			m_requested_metadata.resize(div_round_up(total_size, 16 * 1024));
			m_metadata_size = total_size;
		}

		if (piece < 0 || piece >= int(m_requested_metadata.size()))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			source.m_pc.peer_log("*** UT_METADATA [ piece: %d INVALID ]", piece);				
#endif
			return false;
		}

		if (total_size != m_metadata_size)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			source.m_pc.peer_log("*** UT_METADATA [ total_size: %d INCONSISTENT WITH: %d ]"
				, total_size, m_metadata_size);				
#endif
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
		m_requested_metadata[piece].num_requests = (std::numeric_limits<int>::max)();
		m_requested_metadata[piece].source = source.shared_from_this();

		bool have_all = std::count_if(m_requested_metadata.begin()
			, m_requested_metadata.end(), boost::bind(&metadata_piece::num_requests, _1)
			== (std::numeric_limits<int>::max)()) == int(m_requested_metadata.size());

		if (!have_all) return false;

		if (!m_torrent.set_metadata(&m_metadata[0], m_metadata_size))
		{
			if (!m_torrent.valid_metadata())
			{
				ptime now = time_now();
				// any peer that we downloaded metadata from gets a random time
				// penalty, from 5 to 30 seconds or so. During this time we don't
				// make any metadata requests from those peers (to mix it up a bit
				// of which peers we use)
				// if we only have one block, and thus requested it from a single
				// peer, we bump up the retry time a lot more to try other peers
				bool single_peer = m_requested_metadata.size() == 1;
				for (int i = 0; i < int(m_requested_metadata.size()); ++i)
				{
					m_requested_metadata[i].num_requests = 0;
					boost::shared_ptr<ut_metadata_peer_plugin> peer
						= m_requested_metadata[i].source.lock();
					if (!peer) continue;

					peer->failed_hash_check(single_peer ? now + minutes(5) : now);
				}
			}
			return false;
		}

		// clear the storage for the bitfield
		std::vector<metadata_piece>().swap(m_requested_metadata);

		return true;
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

#endif

