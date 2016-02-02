/*

Copyright (c) 2006-2016, Arvid Norberg
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

#ifndef TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include <vector>
#include <utility>
#include <numeric>
#include <algorithm> // count

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/extensions/metadata_transfer.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/io.hpp"

namespace libtorrent { namespace
{
	int div_round_up(int numerator, int denominator)
	{
		return (numerator + denominator - 1) / denominator;
	}

	std::pair<int, int> req_to_offset(std::pair<int, int> req, int total_size)
	{
		TORRENT_ASSERT(req.first >= 0);
		TORRENT_ASSERT(req.second > 0);
		TORRENT_ASSERT(req.second <= 256);
		TORRENT_ASSERT(req.first + req.second <= 256);

		int start = div_round_up(req.first * total_size, 256);
		int size = div_round_up((req.first + req.second) * total_size, 256) - start;
		return std::make_pair(start, size);
	}

	std::pair<int, int> offset_to_req(std::pair<int, int> offset, int total_size)
	{
		int start = offset.first * 256 / total_size;
		int size = (offset.first + offset.second) * 256 / total_size - start;

		std::pair<int, int> ret(start, size);

		TORRENT_ASSERT(start >= 0);
		TORRENT_ASSERT(size > 0);
		TORRENT_ASSERT(start <= 256);
		TORRENT_ASSERT(start + size <= 256);

		// assert the identity of this function
#if TORRENT_USE_ASSERTS
		std::pair<int, int> identity = req_to_offset(ret, total_size);
		TORRENT_ASSERT(offset == identity);
#endif
		return ret;
	}

	struct metadata_plugin TORRENT_FINAL
		: torrent_plugin
	{
		metadata_plugin(torrent& t)
			: m_torrent(t)
			, m_metadata_progress(0)
			, m_metadata_size(0)
		{
			m_requested_metadata.resize(256, 0);
		}

/*
		bool need_loaded()
		{ return m_torrent.need_loaded(); }
*/
		virtual void on_unload() TORRENT_OVERRIDE
		{
			m_metadata.reset();
		}

		virtual void on_load() TORRENT_OVERRIDE
		{
			// initialize m_metadata_size
			TORRENT_ASSERT(m_torrent.is_loaded());
			metadata();
		}

		virtual void on_files_checked() TORRENT_OVERRIDE
		{
			// if the torrent is a seed, make a reference to
			// the metadata from the torrent before it is deallocated
			if (m_torrent.is_seed()) metadata();
		}

		virtual boost::shared_ptr<peer_plugin> new_connection(
			peer_connection_handle const& pc) TORRENT_OVERRIDE;

		buffer::const_interval metadata() const
		{
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

		bool received_metadata(char const* buf, int size, int offset, int total_size)
		{
			if (m_torrent.valid_metadata()) return false;

			if (!m_metadata || m_metadata_size < total_size)
			{
				m_metadata.reset(new char[total_size]);
				m_metadata_size = total_size;
			}
			std::copy(buf, buf + size, &m_metadata[offset]);

			if (m_have_metadata.empty())
				m_have_metadata.resize(256, false);

			std::pair<int, int> req = offset_to_req(std::make_pair(offset, size)
				, total_size);

			TORRENT_ASSERT(req.first + req.second <= int(m_have_metadata.size()));

			std::fill(
				m_have_metadata.begin() + req.first
				, m_have_metadata.begin() + req.first + req.second
				, true);

			bool have_all = std::count(
				m_have_metadata.begin()
				, m_have_metadata.end()
				, true) == 256;

			if (!have_all) return false;

			if (!m_torrent.set_metadata(&m_metadata[0], m_metadata_size))
			{
				std::fill(
					m_have_metadata.begin()
					, m_have_metadata.begin() + req.first + req.second
					, false);
				m_metadata_progress = 0;
				m_metadata_size = 0;
				return false;
			}

			// clear the storage for the bitfield
			std::vector<bool>().swap(m_have_metadata);
			std::vector<int>().swap(m_requested_metadata);

			return true;
		}

		// returns a range of the metadata that
		// we should request.
		std::pair<int, int> metadata_request();

		void cancel_metadata_request(std::pair<int, int> req)
		{
			for (int i = req.first; i < req.first + req.second; ++i)
			{
				TORRENT_ASSERT(m_requested_metadata[i] > 0);
				if (m_requested_metadata[i] > 0)
					--m_requested_metadata[i];
			}
		}

		// this is called from the peer_connection for
		// each piece of metadata it receives
		void metadata_progress(int total_size, int received)
		{
			m_metadata_progress += received;
			m_metadata_size = total_size;
			m_torrent.set_progress_ppm(boost::int64_t(m_metadata_progress) * 1000000 / m_metadata_size);
		}

		void on_piece_pass(int) TORRENT_OVERRIDE
		{
			// if we became a seed, copy the metadata from
			// the torrent before it is deallocated
			if (m_torrent.is_seed())
				metadata();
		}

/*
		int metadata_size() const { return m_metadata_size; }
*/

	private:
		torrent& m_torrent;

		// this buffer is filled with the info-section of
		// the metadata file while downloading it from
		// peers, and while sending it.
		// it is mutable because it's generated lazily
		mutable boost::shared_array<char> m_metadata;

		int m_metadata_progress;
		mutable int m_metadata_size;

		// this is a bitfield of size 256, each bit represents
		// a piece of the metadata. It is set to one if we
		// have that piece. This vector may be empty
		// (size 0) if we haven't received any metadata
		// or if we already have all metadata
		std::vector<bool> m_have_metadata;
		// this vector keeps track of how many times each meatdata
		// block has been requested
		std::vector<int> m_requested_metadata;

		// explicitly disallow assignment, to silence msvc warning
		metadata_plugin& operator=(metadata_plugin const&);
	};

	struct metadata_peer_plugin TORRENT_FINAL
		: peer_plugin
	{
		metadata_peer_plugin(torrent& t, peer_connection& pc
			, metadata_plugin& tp)
			: m_waiting_metadata_request(false)
			, m_message_index(0)
			, m_metadata_progress(0)
			, m_no_metadata(min_time())
			, m_metadata_request(min_time())
			, m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
		{}

		virtual char const* type() const TORRENT_OVERRIDE { return "LT_metadata"; }

		// can add entries to the extension handshake
		virtual void add_handshake(entry& h) TORRENT_OVERRIDE
		{
			entry& messages = h["m"];
			messages["LT_metadata"] = 14;
		}

		// called when the extension handshake from the other end is received
		virtual bool on_extension_handshake(bdecode_node const& h) TORRENT_OVERRIDE
		{
			m_message_index = 0;
			if (h.type() != bdecode_node::dict_t) return false;
			bdecode_node messages = h.dict_find("m");
			if (!messages || messages.type() != bdecode_node::dict_t) return false;

			int index = int(messages.dict_find_int_value("LT_metadata", -1));
			if (index == -1) return false;
			m_message_index = index;
			return true;
		}

		void write_metadata_request(std::pair<int, int> req)
		{
			TORRENT_ASSERT(req.first >= 0);
			TORRENT_ASSERT(req.second > 0);
			TORRENT_ASSERT(req.first + req.second <= 256);
			TORRENT_ASSERT(!m_pc.associated_torrent().expired());
			TORRENT_ASSERT(!m_pc.associated_torrent().lock()->valid_metadata());

			int start = req.first;
			int size = req.second;

			// abort if the peer doesn't support the metadata extension
			if (m_message_index == 0) return;

#ifndef TORRENT_DISABLE_LOGGING
			m_pc.peer_log(peer_log_alert::outgoing_message, "METADATA_REQUEST"
				, "start: %d size: %d", start, size);
#endif

			char msg[9];
			char* ptr = msg;

			detail::write_uint32(1 + 1 + 3, ptr);
			detail::write_uint8(bt_peer_connection::msg_extended, ptr);
			detail::write_uint8(m_message_index, ptr);
			// means 'request data'
			detail::write_uint8(0, ptr);
			detail::write_uint8(start, ptr);
			detail::write_uint8(size - 1, ptr);
			m_pc.send_buffer(msg, sizeof(msg));
			m_pc.setup_send();
		}

		void write_metadata(std::pair<int, int> req)
		{
			TORRENT_ASSERT(req.first >= 0);
			TORRENT_ASSERT(req.second > 0);
			TORRENT_ASSERT(req.second <= 256);
			TORRENT_ASSERT(req.first + req.second <= 256);
			TORRENT_ASSERT(!m_pc.associated_torrent().expired());

			// abort if the peer doesn't support the metadata extension
			if (m_message_index == 0) return;

			if (m_torrent.valid_metadata())
			{
				std::pair<int, int> offset
					= req_to_offset(req, int(m_tp.metadata().left()));

				char msg[15];
				char* ptr = msg;

#ifndef TORRENT_DISABLE_LOGGING
				m_pc.peer_log(peer_log_alert::outgoing_message, "METADATA"
					, "start: %d total_size: %d offset: %d data_size: %d"
					, req.first, req.second, offset.first, offset.second);
#endif
				// yes, we have metadata, send it
				detail::write_uint32(11 + offset.second, ptr);
				detail::write_uint8(bt_peer_connection::msg_extended, ptr);
				detail::write_uint8(m_message_index, ptr);
				// means 'data packet'
				detail::write_uint8(1, ptr);
				detail::write_uint32(int(m_tp.metadata().left()), ptr);
				detail::write_uint32(offset.first, ptr);
				m_pc.send_buffer(msg, sizeof(msg));

				// TODO: this is not safe. The torrent could be unloaded while
				// we're still sending the metadata
				char const* metadata = m_tp.metadata().begin;
				m_pc.append_const_send_buffer(metadata + offset.first, offset.second);
			}
			else
			{
#ifndef TORRENT_DISABLE_LOGGING
				m_pc.peer_log(peer_log_alert::outgoing_message, "METADATA"
					, "don't have metadata");
#endif
				char msg[4+3];
				char* ptr = msg;

				// we don't have the metadata, reply with
				// don't have-message
				detail::write_uint32(1 + 2, ptr);
				detail::write_uint8(bt_peer_connection::msg_extended, ptr);
				detail::write_uint8(m_message_index, ptr);
				// means 'have no data'
				detail::write_uint8(2, ptr);
				m_pc.send_buffer(msg, sizeof(msg));
			}
			m_pc.setup_send();
		}

		virtual bool on_extended(int length
			, int msg, buffer::const_interval body) TORRENT_OVERRIDE
		{
			if (msg != 14) return false;
			if (m_message_index == 0) return false;

			if (length > 500 * 1024)
			{
				m_pc.disconnect(errors::metadata_too_large, op_bittorrent, 2);
				return true;
			}

			if (body.left() < 1) return true;
			int type = detail::read_uint8(body.begin);

			switch (type)
			{
			case 0: // request
				{
					if (body.left() < 2) return true;
					int start = detail::read_uint8(body.begin);
					int size = detail::read_uint8(body.begin) + 1;

#ifndef TORRENT_DISABLE_LOGGING
					m_pc.peer_log(peer_log_alert::incoming_message, "METADATA_REQUEST"
						, "start: %d size: %d", start, size);
#endif

					if (length != 3)
					{
						// invalid metadata request
						m_pc.disconnect(errors::invalid_metadata_request, op_bittorrent, 2);
						return true;
					}

					write_metadata(std::make_pair(start, size));
				}
				break;
			case 1: // data
				{
					if (body.left() < 8) return true;

					int total_size = detail::read_int32(body.begin);
					int offset = detail::read_int32(body.begin);
					int data_size = length - 9;

#ifndef TORRENT_DISABLE_LOGGING
					m_pc.peer_log(peer_log_alert::incoming_message, "METADATA"
						, "total_size: %d | offset: %d | data_size: %d"
						,total_size, offset, data_size);
#endif

					if (total_size > m_torrent.session().settings().get_int(settings_pack::max_metadata_size))
					{
						m_pc.disconnect(errors::metadata_too_large, op_bittorrent, 2);
						return true;
					}
					if (total_size <= 0)
					{
						m_pc.disconnect(errors::invalid_metadata_size, op_bittorrent, 2);
						return true;
					}
					if (offset > total_size || offset < 0)
					{
						m_pc.disconnect(errors::invalid_metadata_offset, op_bittorrent, 2);
						return true;
					}
					if (offset + data_size > total_size)
					{
						m_pc.disconnect(errors::invalid_metadata_message, op_bittorrent, 2);
						return true;
					}

					m_tp.metadata_progress(total_size
						, body.left() - m_metadata_progress);
					m_metadata_progress = body.left();

					if (body.left() < data_size) return true;

					m_waiting_metadata_request = false;
					m_tp.received_metadata(body.begin, data_size
						, offset, total_size);
					m_metadata_progress = 0;
				}
				break;
			case 2: // have no data
				m_no_metadata = aux::time_now();
				if (m_waiting_metadata_request)
					m_tp.cancel_metadata_request(m_last_metadata_request);
				m_waiting_metadata_request = false;
#ifndef TORRENT_DISABLE_LOGGING
				m_pc.peer_log(peer_log_alert::incoming_message, "METADATA"
					, "don't have metadata");
#endif
				break;
			default:
				{
					m_pc.disconnect(errors::invalid_metadata_message, op_bittorrent, 2);
				}
			}
			return true;
		}

		virtual void tick() TORRENT_OVERRIDE
		{
			if (m_pc.is_disconnecting()) return;

			// if we don't have any metadata, and this peer
			// supports the request metadata extension
			// and we aren't currently waiting for a request
			// reply. Then, send a request for some metadata.
			if (!m_torrent.valid_metadata()
				&& m_message_index != 0
				&& !m_waiting_metadata_request
				&& has_metadata())
			{
				m_last_metadata_request = m_tp.metadata_request();
				write_metadata_request(m_last_metadata_request);
				m_waiting_metadata_request = true;
				m_metadata_request = aux::time_now();
			}
		}

		bool has_metadata() const
		{
			return aux::time_now() - minutes(5) > m_no_metadata;
		}

	private:

		// this is set to true when we send a metadata
		// request to this peer, and reset to false when
		// we receive a reply to our request.
		bool m_waiting_metadata_request;

		// this is the message index the remote peer uses
		// for metadata extension messages.
		int m_message_index;

		// the number of bytes of metadata we have received
		// so far from this per, only counting the current
		// request. Any previously finished requests
		// that have been forwarded to the torrent object
		// do not count.
		int m_metadata_progress;

		// this is set to the current time each time we get a
		// "I don't have metadata" message.
		time_point m_no_metadata;

		// this is set to the time when we last sent
		// a request for metadata to this peer
		time_point m_metadata_request;

		// if we're waiting for a metadata request
		// this was the request we sent
		std::pair<int, int> m_last_metadata_request;

		torrent& m_torrent;
		peer_connection& m_pc;
		metadata_plugin& m_tp;

		// explicitly disallow assignment, to silence msvc warning
		metadata_peer_plugin& operator=(metadata_peer_plugin const&);
	};

	boost::shared_ptr<peer_plugin> metadata_plugin::new_connection(
		peer_connection_handle const& pc)
	{
		if (pc.type() != peer_connection::bittorrent_connection)
			return boost::shared_ptr<peer_plugin>();

		return boost::shared_ptr<peer_plugin>(new metadata_peer_plugin(m_torrent, *pc.native_handle().get(), *this));
	}

	std::pair<int, int> metadata_plugin::metadata_request()
	{
		// the number of blocks to request
		int num_blocks = 256 / 4;
		TORRENT_ASSERT(num_blocks <= 128);

		int min_element = (std::numeric_limits<int>::max)();
		int best_index = 0;
		for (int i = 0; i < 256 - num_blocks + 1; ++i)
		{
			int min = *std::min_element(m_requested_metadata.begin() + i
				, m_requested_metadata.begin() + i + num_blocks);
			min += std::accumulate(m_requested_metadata.begin() + i
				, m_requested_metadata.begin() + i + num_blocks, int(0));

			if (min_element > min)
			{
				best_index = i;
				min_element = min;
			}
		}

		std::pair<int, int> ret(best_index, num_blocks);
		for (int i = ret.first; i < ret.first + ret.second; ++i)
			m_requested_metadata[i]++;

		TORRENT_ASSERT(ret.first >= 0);
		TORRENT_ASSERT(ret.second > 0);
		TORRENT_ASSERT(ret.second <= 256);
		TORRENT_ASSERT(ret.first + ret.second <= 256);

		return ret;
	}

} }

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> create_metadata_plugin(torrent_handle const& th, void*)
	{
		torrent* t = th.native_handle().get();
		// don't add this extension if the torrent is private
		if (t->valid_metadata() && t->torrent_file().priv()) return boost::shared_ptr<torrent_plugin>();
		return boost::shared_ptr<torrent_plugin>(new metadata_plugin(*t));
	}

}

#endif
#endif

