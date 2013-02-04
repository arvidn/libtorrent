/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include "torrent_history.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/alert_handler.hpp"

namespace libtorrent
{
	torrent_history::torrent_history(alert_handler* h)
		: m_alerts(h)
		, m_frame(0)
	{
		m_alerts->subscribe(this, 0, add_torrent_alert::alert_type
			, torrent_removed_alert::alert_type
			, state_update_alert::alert_type, 0);
	}

	torrent_history::~torrent_history()
	{
		m_alerts->unsubscribe(this);
	}

	void torrent_history::handle_alert(alert const* a)
	{
		add_torrent_alert const* ta = alert_cast<add_torrent_alert>(a);
		torrent_removed_alert const* td = alert_cast<torrent_removed_alert>(a);
		state_update_alert const* su = alert_cast<state_update_alert>(a);
		if (ta)
		{
			printf("added torrent: %s\n", ta->handle.name().c_str());
			mutex::scoped_lock l(m_mutex);
			m_queue.left.push_front(std::make_pair(m_frame, ta->handle.status()));
		}
		else if (td)
		{
			mutex::scoped_lock l(m_mutex);

			m_removed.push_front(std::make_pair(m_frame, td->info_hash));
			torrent_status st;
			st.handle = td->handle;
			m_queue.right.erase(st);
			// weed out torrents that were removed a long time ago
			while (m_removed.size() > 50 && m_removed.back().first < m_frame - 10)
			{
				m_removed.pop_back();
			}
		}
		else if (su)
		{
			mutex::scoped_lock l(m_mutex);

			std::vector<torrent_status> const& st = su->status;
			for (std::vector<torrent_status>::const_iterator i = st.begin()
				, end(st.end()); i != end; ++i)
			{
				queue_t::right_iterator it = m_queue.right.find(*i);
				if (it == m_queue.right.end()) continue;
				m_queue.right.replace_key(it, *i);
				m_queue.right.replace_data(it, m_frame);
				// bump this torrent to the beginning of the list
				m_queue.left.relocate(m_queue.left.begin(), m_queue.project_left(it));
			}
			++m_frame;
/*
			printf("===== frame: %d =====\n", m_frame);
			for (queue_t::left_iterator i = m_queue.left.begin()
				, end(m_queue.left.end()); i != end; ++i)
			{
				printf("%3d: %s\n", i->first, i->second.handle.name().c_str());
			}
*/
		}
	}

	void torrent_history::removed_since(int frame, std::vector<sha1_hash>& torrents) const
	{
		torrents.clear();
		mutex::scoped_lock l(m_mutex);
		for (std::deque<std::pair<int, sha1_hash> >::const_iterator i = m_removed.begin()
			, end(m_removed.end()); i != end; ++i)
		{
			if (i->first < frame) break;
			torrents.push_back(i->second);
		}
	}

	void torrent_history::updated_since(int frame, std::vector<torrent_status>& torrents) const
	{
		mutex::scoped_lock l(m_mutex);
		for (queue_t::left_const_iterator i = m_queue.left.begin()
			, end(m_queue.left.end()); i != end; ++i)
		{
			if (i->first < frame) break;
			torrents.push_back(i->second);
		}
	}
}

