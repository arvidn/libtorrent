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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vector>
#include <utility>
#include <numeric>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/extensions/metadata_transfer.hpp"

using boost::posix_time::second_clock;

namespace libtorrent { namespace
{
	int div_round_up(int numerator, int denominator)
	{
		return (numerator + denominator - 1) / denominator;
	}

	std::pair<int, int> req_to_offset(std::pair<int, int> req, int total_size)
	{
		assert(req.first >= 0);
		assert(req.second > 0);
		assert(req.second <= 256);
		assert(req.first + req.second <= 256);

		int start = div_round_up(req.first * total_size, 256);
		int size = div_round_up((req.first + req.second) * total_size, 256) - start;
		return std::make_pair(start, size);
	}

	std::pair<int, int> offset_to_req(std::pair<int, int> offset, int total_size)
	{
		int start = offset.first * 256 / total_size;
		int size = (offset.first + offset.second) * 256 / total_size - start;

		std::pair<int, int> ret(start, size);
	
		assert(start >= 0);
		assert(size > 0);
		assert(start <= 256);
		assert(start + size <= 256);

		// assert the identity of this function
#ifndef NDEBUG
		std::pair<int, int> identity = req_to_offset(ret, total_size);
		assert(offset == identity);
#endif
		return ret;
	}


	struct metadata_plugin : torrent_plugin
	{
		metadata_plugin(torrent& t)
			: m_torrent(t)
			, m_metadata_progress(0)
			, m_metadata_size(0)
		{
			m_requested_metadata.resize(256, 0);
		}
	
		virtual boost::shared_ptr<peer_plugin> new_connection(
			peer_connection* pc);
		
		std::vector<char> const& metadata() const
		{
			if (m_metadata.empty())
			{
				bencode(std::back_inserter(m_metadata)
					, m_torrent.torrent_file().create_info_metadata());

				assert(hasher(&m_metadata[0], m_metadata.size()).final()
					== m_torrent.torrent_file().info_hash());
			}
			assert(!m_metadata.empty());
			return m_metadata;
		}

		bool received_metadata(char const* buf, int size, int offset, int total_size)
		{
			if (m_torrent.valid_metadata()) return false;

			if ((int)m_metadata.size() < total_size)
				m_metadata.resize(total_size);

			std::copy(
				buf
				, buf + size
				, &m_metadata[offset]);

			if (m_have_metadata.empty())
				m_have_metadata.resize(256, false);

			std::pair<int, int> req = offset_to_req(std::make_pair(offset, size)
				, total_size);

			assert(req.first + req.second <= (int)m_have_metadata.size());

			std::fill(
				m_have_metadata.begin() + req.first
				, m_have_metadata.begin() + req.first + req.second
				, true);
		
			bool have_all = std::count(
				m_have_metadata.begin()
				, m_have_metadata.end()
				, true) == 256;

			if (!have_all) return false;

			hasher h;
			h.update(&m_metadata[0], (int)m_metadata.size());
			sha1_hash info_hash = h.final();

			if (info_hash != m_torrent.torrent_file().info_hash())
			{
				std::fill(
					m_have_metadata.begin()
					, m_have_metadata.begin() + req.first + req.second
					, false);
				m_metadata_progress = 0;
				m_metadata_size = 0;
				// TODO: allow plugins to post alerts
/*
				if (m_ses.m_alerts.should_post(alert::info))
				{
					m_ses.m_alerts.post_alert(metadata_failed_alert(
						get_handle(), "invalid metadata received from swarm"));
				}
*/
				return false;
			}

			entry metadata = bdecode(m_metadata.begin(), m_metadata.end());
			m_torrent.set_metadata(metadata);

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
				assert(m_requested_metadata[i] > 0);
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
		}

	private:
		torrent& m_torrent;

		// this buffer is filled with the info-section of
		// the metadata file while downloading it from
		// peers, and while sending it.
		// it is mutable because it's generated lazily
		mutable std::vector<char> m_metadata;

		int m_metadata_progress;
		int m_metadata_size;

		// this is a bitfield of size 256, each bit represents
		// a piece of the metadata. It is set to one if we
		// have that piece. This vector may be empty
		// (size 0) if we haven't received any metadata
		// or if we already have all metadata
		std::vector<bool> m_have_metadata;
		// this vector keeps track of how many times each meatdata
		// block has been requested
		std::vector<int> m_requested_metadata;
	};


	struct metadata_peer_plugin : peer_plugin
	{
		metadata_peer_plugin(torrent& t, peer_connection& pc
			, metadata_plugin& tp)
			: m_waiting_metadata_request(false)
			, m_message_index(0)
			, m_metadata_progress(0)
			, m_no_metadata(
				boost::gregorian::date(1970, boost::date_time::Jan, 1)
				, boost::posix_time::seconds(0))
			, m_metadata_request(
				boost::gregorian::date(1970, boost::date_time::Jan, 1)
				, boost::posix_time::seconds(0))
			, m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
		{}

		// can add entries to the extension handshake
		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages["LT_metadata"] = 14;
		}

		// called when the extension handshake from the other end is received
		virtual bool on_extension_handshake(entry const& h)
		{
			entry const& messages = h["m"];
			if (entry const* index = messages.find_key("LT_metadata"))
			{
				m_message_index = index->integer();
				return true;
			}
			else
			{
				m_message_index = 0;
				return false;
			}
		}

		void write_metadata_request(std::pair<int, int> req)
		{
			assert(req.first >= 0);
			assert(req.second > 0);
			assert(req.first + req.second <= 256);
			assert(!m_pc.associated_torrent().expired());
			assert(!m_pc.associated_torrent().lock()->valid_metadata());

			int start = req.first;
			int size = req.second;

			// abort if the peer doesn't support the metadata extension
			if (m_message_index == 0) return;

			buffer::interval i = m_pc.allocate_send_buffer(9);

			detail::write_uint32(1 + 1 + 3, i.begin);
			detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
			detail::write_uint8(m_message_index, i.begin);
			// means 'request data'
			detail::write_uint8(0, i.begin);
			detail::write_uint8(start, i.begin);
			detail::write_uint8(size - 1, i.begin);
			assert(i.begin == i.end);
			m_pc.setup_send();
		}

		void write_metadata(std::pair<int, int> req)
		{
			assert(req.first >= 0);
			assert(req.second > 0);
			assert(req.second <= 256);
			assert(req.first + req.second <= 256);
			assert(!m_pc.associated_torrent().expired());

			// abort if the peer doesn't support the metadata extension
			if (m_message_index == 0) return;

			// only send metadata if the torrent is non-private
			if (m_torrent.valid_metadata() && !m_torrent.torrent_file().priv())
			{
				std::pair<int, int> offset
					= req_to_offset(req, (int)m_tp.metadata().size());

				buffer::interval i = m_pc.allocate_send_buffer(15 + offset.second);

				// yes, we have metadata, send it
				detail::write_uint32(11 + offset.second, i.begin);
				detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
				detail::write_uint8(m_message_index, i.begin);
				// means 'data packet'
				detail::write_uint8(1, i.begin);
				detail::write_uint32((int)m_tp.metadata().size(), i.begin);
				detail::write_uint32(offset.first, i.begin);
				std::vector<char> const& metadata = m_tp.metadata();
				std::copy(metadata.begin() + offset.first
					, metadata.begin() + offset.first + offset.second, i.begin);
				i.begin += offset.second;
				assert(i.begin == i.end);
			}
			else
			{
				buffer::interval i = m_pc.allocate_send_buffer(4 + 3);
				// we don't have the metadata, reply with
				// don't have-message
				detail::write_uint32(1 + 2, i.begin);
				detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
				detail::write_uint8(m_message_index, i.begin);
				// means 'have no data'
				detail::write_uint8(2, i.begin);
				assert(i.begin == i.end);
			}
			m_pc.setup_send();
		}

		virtual bool on_extended(int length
			, int msg, buffer::const_interval body)
		{
			if (msg != 14) return false;
			if (m_message_index == 0) return false;

			if (length > 500 * 1024)
				throw protocol_error("LT_metadata message larger than 500 kB");

			if (body.left() < 1) return true;
			int type = detail::read_uint8(body.begin);

			switch (type)
			{
			case 0: // request
				{
					if (body.left() < 2) return true;
					int start = detail::read_uint8(body.begin);
					int size = detail::read_uint8(body.begin) + 1;

					if (length != 3)
					{
						// invalid metadata request
						throw protocol_error("invalid metadata request");
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

					if (total_size > 500 * 1024)
						throw protocol_error("metadata size larger than 500 kB");
					if (total_size <= 0)
						throw protocol_error("invalid metadata size");
					if (offset > total_size || offset < 0)
						throw protocol_error("invalid metadata offset");
					if (offset + data_size > total_size)
						throw protocol_error("invalid metadata message");

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
				m_no_metadata = second_clock::universal_time();
				if (m_waiting_metadata_request)
					m_tp.cancel_metadata_request(m_last_metadata_request);
				m_waiting_metadata_request = false;
				break;
			default:
				throw protocol_error("unknown metadata extension message: "
					+ boost::lexical_cast<std::string>(type));
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
				&& !m_waiting_metadata_request
				&& has_metadata())
			{
				m_last_metadata_request = m_tp.metadata_request();
				write_metadata_request(m_last_metadata_request);
				m_waiting_metadata_request = true;
				m_metadata_request = second_clock::universal_time();
			}
		}

		bool has_metadata() const
		{
			using namespace boost::posix_time;
			return second_clock::universal_time() - m_no_metadata > minutes(5);
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
		boost::posix_time::ptime m_no_metadata;

		// this is set to the time when we last sent
		// a request for metadata to this peer
		boost::posix_time::ptime m_metadata_request;

		// if we're waiting for a metadata request
		// this was the request we sent
		std::pair<int, int> m_last_metadata_request;

		torrent& m_torrent;
		peer_connection& m_pc;
		metadata_plugin& m_tp;
	};

	boost::shared_ptr<peer_plugin> metadata_plugin::new_connection(
		peer_connection* pc)
	{
		return boost::shared_ptr<peer_plugin>(new metadata_peer_plugin(m_torrent, *pc, *this));
	}

	std::pair<int, int> metadata_plugin::metadata_request()
	{
		// count the number of peers that supports the
		// extension and that has metadata
		int peers = 0;
#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::map<tcp::endpoint, peer_connection*> conn_map;
		for (conn_map::iterator i = m_torrent.begin()
			, end(m_torrent.end()); i != end; ++i)
		{
			bt_peer_connection* c = dynamic_cast<bt_peer_connection*>(i->second);
			if (c == 0) continue;
			metadata_peer_plugin* p
				= c->supports_extension<metadata_peer_plugin>();
			if (p == 0) continue;
			if (!p->has_metadata()) continue;
			++peers;
		}
#endif

		// the number of blocks to request
		int num_blocks = 256 / (peers + 1);
		if (num_blocks < 1) num_blocks = 1;
		assert(num_blocks <= 128);

		int min_element = std::numeric_limits<int>::max();
		int best_index = 0;
		for (int i = 0; i < 256 - num_blocks + 1; ++i)
		{
			int min = *std::min_element(m_requested_metadata.begin() + i
				, m_requested_metadata.begin() + i + num_blocks);
			min += std::accumulate(m_requested_metadata.begin() + i
				, m_requested_metadata.begin() + i + num_blocks, (int)0);

			if (min_element > min)
			{
				best_index = i;
				min_element = min;
			}
		}

		std::pair<int, int> ret(best_index, num_blocks);
		for (int i = ret.first; i < ret.first + ret.second; ++i)
			m_requested_metadata[i]++;

		assert(ret.first >= 0);
		assert(ret.second > 0);
		assert(ret.second <= 256);
		assert(ret.first + ret.second <= 256);

		return ret;
	}

} }

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> create_metadata_plugin(torrent* t)
	{
		return boost::shared_ptr<torrent_plugin>(new metadata_plugin(*t));
	}

}


