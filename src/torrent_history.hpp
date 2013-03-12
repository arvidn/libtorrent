/*

Copyright (c) 2012, Arvid Norberg
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

#ifndef TORRENT_TORRENT_HISTORY_HPP
#define TORRENT_TORRENT_HISTORY_HPP

#include "libtorrent/alert_observer.hpp"
#include "libtorrent/torrent_handle.hpp"
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <deque>

namespace libtorrent
{
	struct alert_handler;

	struct torrent_history : alert_observer
	{

		torrent_history(alert_handler* h);
		~torrent_history();

		// returns the info-hashes of the torrents that have been
		// removed since the specified frame number
		void removed_since(int frame, std::vector<sha1_hash>& torrents) const;

		// returns the torrent_status structures for the torrents
		// that have changed since the specified frame number
		void updated_since(int frame, std::vector<torrent_status>& torrents) const;

		// the current frame number
		int frame() const;

		virtual void handle_alert(alert const* a);

	private:	

		// first is the frame this torrent was last
		// seen modified in, second is the information
		// about the torrent that was modified
		typedef boost::bimap<boost::bimaps::list_of<int>
			, boost::bimaps::unordered_set_of<torrent_status> > queue_t;

		mutable mutex m_mutex;

		queue_t m_queue;

		std::deque<std::pair<int, sha1_hash> > m_removed;

		alert_handler* m_alerts;

		// frame counter. This is incremented every
		// time we get a status update for torrents
		mutable int m_frame;

		// if we haven't gotten any status updates
		// but we have received add or delete alerts,
		// we increment the frame counter on access,
		// in order to maek added and deleted event also
		// fall into distinct time-slots, instead of being
		// potentially returned twice, once when they
		// happen and once after we've received an
		// update and increment the frame counter
		mutable bool m_deferred_frame_count;
	};
}

#endif

