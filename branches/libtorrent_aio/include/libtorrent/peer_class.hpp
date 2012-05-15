/*

Copyright (c) 2011, Arvid Norberg
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

#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/assert.hpp"

#include <vector>
#include <string>
#include <boost/intrusive_ptr.hpp>
#include <boost/cstdint.hpp>

namespace libtorrent
{
	typedef boost::uint32_t peer_class_t;

	struct peer_class_info
	{
		bool ignore_unchoke_slots;
		std::string label;
		int upload_limit;
		int download_limit;
	};

	struct peer_class : intrusive_ptr_base<peer_class>
	{
		friend struct peer_class_pool;

		peer_class(std::string const& label)
			: ignore_unchoke_slots(false)
			, label(label)
			, references(1)
		{}

		void set_info(peer_class_info const* pci);
		void get_info(peer_class_info* pci) const;

		void set_upload_limit(int limit);
		void set_download_limit(int limit);

		// the bandwidth channels, upload and download
		// keeps track of the current quotas
		bandwidth_channel channel[2];

		bool ignore_unchoke_slots;

		// the name of this peer class
		std::string label;

	private:
		int references;

	};

	struct peer_class_pool
	{
	
		peer_class_t new_peer_class(std::string const& label);
		void decref(peer_class_t c);
		void incref(peer_class_t c);
		peer_class* at(peer_class_t c);
		peer_class const* at(peer_class_t c) const;

	private:

		// state for peer classes (a peer can belong to multiple classes)
		// this can control
		std::vector<boost::intrusive_ptr<peer_class> > m_peer_classes;

		// indices in m_peer_classes that are no longer used
		std::vector<int> m_free_list;
	};
}

#endif // TORRENT_PEER_CLASS_HPP_INCLUDED

