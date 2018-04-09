/*

Copyright (c) 2011-2018, Arvid Norberg
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

#ifndef TORRENT_PEER_CLASS_HPP_INCLUDED
#define TORRENT_PEER_CLASS_HPP_INCLUDED

#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/assert.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <deque>
#include <string>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	typedef int peer_class_t;

	// holds settings for a peer class. Used in set_peer_class() and
	// get_peer_class() calls.
	struct TORRENT_EXPORT peer_class_info
	{
		// ``ignore_unchoke_slots`` determines whether peers should always
		// unchoke a peer, regardless of the choking algorithm, or if it should
		// honor the unchoke slot limits. It's used for local peers by default.
		// If *any* of the peer classes a peer belongs to has this set to true,
		// that peer will be unchoked at all times.
		bool ignore_unchoke_slots;

		// adjusts the connection limit (global and per torrent) that applies to
		// this peer class. By default, local peers are allowed to exceed the
		// normal connection limit for instance. This is specified as a percent
		// factor. 100 makes the peer class apply normally to the limit. 200
		// means as long as there are fewer connections than twice the limit, we
		// accept this peer. This factor applies both to the global connection
		// limit and the per-torrent limit. Note that if not used carefully one
		// peer class can potentially completely starve out all other over time.
		int connection_limit_factor;

		// not used by libtorrent. It's intended as a potentially user-facing
		// identifier of this peer class.
		std::string label;

		// transfer rates limits for the whole peer class. They are specified in
		// bytes per second and apply to the sum of all peers that are members of
		// this class.
		int upload_limit;
		int download_limit;

		// relative priorities used by the bandwidth allocator in the rate
		// limiter. If no rate limits are in use, the priority is not used
		// either. Priorities start at 1 (0 is not a valid priority) and may not
		// exceed 255.
		int upload_priority;
		int download_priority;
	};

	struct TORRENT_EXTRA_EXPORT peer_class
	{
		friend struct peer_class_pool;

		peer_class(std::string const& l)
			: in_use(true)
			, ignore_unchoke_slots(false)
			, connection_limit_factor(100)
			, label(l)
			, references(1)
		{
			priority[0] = 1;
			priority[1] = 1;
		}

		void clear()
		{
			in_use = false;
			label.clear();
		}

		void set_info(peer_class_info const* pci);
		void get_info(peer_class_info* pci) const;

		void set_upload_limit(int limit);
		void set_download_limit(int limit);

		// the bandwidth channels, upload and download
		// keeps track of the current quotas
		bandwidth_channel channel[2];

		// this is set to false when this slot is not in use for a peer_class
		bool in_use;

		bool ignore_unchoke_slots;
		int connection_limit_factor;

		// priority for bandwidth allocation
		// in rate limiter. One for upload and one
		// for download
		int priority[2];

		// the name of this peer class
		std::string label;

	private:
		int references;
	};

	struct TORRENT_EXTRA_EXPORT peer_class_pool
	{
		peer_class_t new_peer_class(std::string const& label);
		void decref(peer_class_t c);
		void incref(peer_class_t c);
		peer_class* at(peer_class_t c);
		peer_class const* at(peer_class_t c) const;

	private:

		// state for peer classes (a peer can belong to multiple classes)
		// this can control
		std::deque<peer_class> m_peer_classes;

		// indices in m_peer_classes that are no longer used
		std::vector<peer_class_t> m_free_list;
	};
}

#endif // TORRENT_PEER_CLASS_HPP_INCLUDED

