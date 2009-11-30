/*

Copyright (c) 2008, Arvid Norberg
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

	bool send_tracker(announce_entry const& e)
	{
		// max_fails == 0 means that it's one
		// of the trackers from the trackers
		// from the torrent file
		return e.fail_limit == 0 || e.verified;
	}

	struct lt_tracker_plugin : torrent_plugin
	{
		lt_tracker_plugin(torrent& t)
			: m_torrent(t)
			, m_updates(0)
			, m_2_minutes(110)
		{
			m_old_trackers = t.trackers();
			update_list_hash();
		}

		virtual boost::shared_ptr<peer_plugin> new_connection(
			peer_connection* pc);
		
		virtual void tick()
		{
			if (m_2_minutes++ < 120) return;
			m_2_minutes = 0;

			// build tracker diff
			entry tex;
			entry::list_type& added = tex["added"].list();
			std::vector<announce_entry> const& trackers = m_torrent.trackers();
			for (std::vector<announce_entry>::const_iterator i = trackers.begin()
				, end(trackers.end()); i != end; ++i)
			{
				std::vector<announce_entry>::const_iterator k = std::find_if(
					m_old_trackers.begin(), m_old_trackers.end()
					, boost::bind(&announce_entry::url, _1) == i->url);
				if (k != m_old_trackers.end()) continue;
				if (!send_tracker(*i)) continue;
				m_old_trackers.push_back(*i);
				++m_updates;
				added.push_back(i->url);
			}
			m_lt_trackers_msg.clear();
			bencode(std::back_inserter(m_lt_trackers_msg), tex);
			if (m_updates > 0) update_list_hash();
		}

		void update_list_hash()
		{
			std::vector<std::string> canonical_list;
			std::transform(m_old_trackers.begin(), m_old_trackers.end(), back_inserter(canonical_list)
				, boost::bind(&announce_entry::url, _1));
			std::sort(canonical_list.begin(), canonical_list.end());

			hasher h;
			std::for_each(canonical_list.begin(), canonical_list.end()
				, boost::bind(&hasher::update, &h, _1));
			m_list_hash = h.final();
		}

		int num_updates() const { return m_updates; }

		std::vector<char> const& get_lt_tex_msg() const { return m_lt_trackers_msg; }

		sha1_hash const& list_hash() const { return m_list_hash; }

		std::vector<announce_entry> const& trackers() const { return m_old_trackers; }

	private:
		torrent& m_torrent;
		std::vector<announce_entry> m_old_trackers;
		int m_updates;
		int m_2_minutes;
		std::vector<char> m_lt_trackers_msg;
		sha1_hash m_list_hash;
	};


	struct lt_tracker_peer_plugin : peer_plugin
	{
		lt_tracker_peer_plugin(torrent& t, bt_peer_connection& pc, lt_tracker_plugin& tp)
			: m_message_index(0)
			, m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
			, m_2_minutes(115)
			, m_full_list(true)
		{}

		// can add entries to the extension handshake
		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages["lt_tex"] = 3;
			h["tr"] = m_tp.list_hash().to_string();
		}

		// called when the extension handshake from the other end is received
		virtual bool on_extension_handshake(lazy_entry const& h)
		{
			m_message_index = 0;
			if (h.type() != lazy_entry::dict_t) return false;
			lazy_entry const* messages = h.dict_find("m");
			if (!messages || messages->type() != lazy_entry::dict_t) return false;

			int index = messages->dict_find_int_value("lt_tex", -1);
			if (index == -1) return false;
			m_message_index = index;

			// if we have the same tracker list, don't bother sending the
			// full list. Just send deltas
			std::string tracker_list_hash = h.dict_find_string_value("tr");
			if (tracker_list_hash.size() == 20
				&& sha1_hash(tracker_list_hash) == m_tp.list_hash())
			{
				m_full_list = false;
			}
			return true;
		}

		virtual bool on_extended(int length
			, int extended_msg, buffer::const_interval body)
		{
			if (extended_msg != 3) return false;
			if (m_message_index == 0) return false;
			if (!m_pc.packet_finished()) return true;

			lazy_entry msg;
			int ret = lazy_bdecode(body.begin, body.end, msg);
			if (ret != 0 || msg.type() != lazy_entry::dict_t)
			{
				m_pc.disconnect(errors::invalid_lt_tracker_message, 2);
				return true;
			}

			lazy_entry const* added = msg.dict_find_list("added");

#ifdef TORRENT_VERBOSE_LOGGING
			std::stringstream log_line;
			log_line << time_now_string() << " <== LT_TEX [ "
				"added: ";
#endif

			// invalid tex message
			if (added == 0)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_pc.m_logger) << time_now_string() << " <== LT_TEX [ NOT A DICTIONARY ]\n";
#endif
				return true;
			}

			for (int i = 0; i < added->list_size(); ++i)
			{
				announce_entry e(added->list_string_value_at(i));
				if (e.url.empty()) continue;
				e.fail_limit = 3;
				e.send_stats = false;
				e.source = announce_entry::source_tex;
				m_torrent.add_tracker(e);
#ifdef TORRENT_VERBOSE_LOGGING
				log_line << e.url << " ";
#endif
			}
#ifdef TORRENT_VERBOSE_LOGGING
			log_line << "]\n";
			(*m_pc.m_logger) << log_line.str();
#endif
			return true;
		}

		virtual void tick()
		{
			if (!m_message_index) return;	// no handshake yet
			if (++m_2_minutes <= 120) return;
			m_2_minutes = 0;

			if (m_full_list)
			{
				send_full_tex_list();
				m_full_list = false;
			}
			else
			{
				send_lt_tex_diff();
			}
		}

	private:

		void send_lt_tex_diff()
		{
			// if there's no change in out tracker set, don't send anything
			if (m_tp.num_updates() == 0) return;

			std::vector<char> const& tex_msg = m_tp.get_lt_tex_msg();

			buffer::interval i = m_pc.allocate_send_buffer(6 + tex_msg.size());

			detail::write_uint32(1 + 1 + tex_msg.size(), i.begin);
			detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
			detail::write_uint8(m_message_index, i.begin);
			std::copy(tex_msg.begin(), tex_msg.end(), i.begin);
			i.begin += tex_msg.size();

			TORRENT_ASSERT(i.begin == i.end);
			m_pc.setup_send();
		}

		void send_full_tex_list() const
		{
			if (m_tp.trackers().empty()) return;

#ifdef TORRENT_VERBOSE_LOGGING
			std::stringstream log_line;
			log_line << time_now_string() << " ==> LT_TEX [ "
				"added: ";
#endif
			entry tex;
			entry::list_type& added = tex["added"].list();
			for (std::vector<announce_entry>::const_iterator i = m_tp.trackers().begin()
				, end(m_tp.trackers().end()); i != end; ++i)
			{
				if (!send_tracker(*i)) continue;
				added.push_back(i->url);
#ifdef TORRENT_VERBOSE_LOGGING
				log_line << i->url << " ";
#endif
			}
			std::vector<char> tex_msg;
			bencode(std::back_inserter(tex_msg), tex);

#ifdef TORRENT_VERBOSE_LOGGING
			log_line << "]\n";
			(*m_pc.m_logger) << log_line.str();
#endif

			buffer::interval i = m_pc.allocate_send_buffer(6 + tex_msg.size());

			detail::write_uint32(1 + 1 + tex_msg.size(), i.begin);
			detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
			detail::write_uint8(m_message_index, i.begin);
			std::copy(tex_msg.begin(), tex_msg.end(), i.begin);
			i.begin += tex_msg.size();

			TORRENT_ASSERT(i.begin == i.end);
			m_pc.setup_send();
		}

		// this is the message index the remote peer uses
		// for metadata extension messages.
		int m_message_index;

		torrent& m_torrent;
		bt_peer_connection& m_pc;
		lt_tracker_plugin& m_tp;

		int m_2_minutes;
		bool m_full_list;
	};

	boost::shared_ptr<peer_plugin> lt_tracker_plugin::new_connection(
		peer_connection* pc)
	{
		bt_peer_connection* c = dynamic_cast<bt_peer_connection*>(pc);
		if (!c) return boost::shared_ptr<peer_plugin>();
		return boost::shared_ptr<peer_plugin>(new lt_tracker_peer_plugin(m_torrent, *c, *this));
	}

} }

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> TORRENT_EXPORT create_lt_trackers_plugin(torrent* t, void*)
	{
		if (t->valid_metadata() && t->torrent_file().priv()) return boost::shared_ptr<torrent_plugin>();
		return boost::shared_ptr<torrent_plugin>(new lt_tracker_plugin(*t));
	}

}


