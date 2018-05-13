/*

Copyright (c) 2014-2018, Arvid Norberg
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

#ifndef TORRENT_RESOLVE_LINKS_HPP
#define TORRENT_RESOLVE_LINKS_HPP

#include <vector>
#include <utility>
#include <unordered_map>
#include <memory>
#include <string>

#include "libtorrent/aux_/export.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/vector.hpp"

namespace libtorrent {

	class torrent_info;

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	// this class is used for mutable torrents, to discover identical files
	// in other torrents.
	struct TORRENT_EXTRA_EXPORT resolve_links
	{
		struct TORRENT_EXTRA_EXPORT link_t
		{
			std::shared_ptr<const torrent_info> ti;
			std::string save_path;
			file_index_t file_idx;
		};

		explicit resolve_links(std::shared_ptr<torrent_info> ti);

		// check to see if any files are shared with this torrent
		void match(std::shared_ptr<const torrent_info> const& ti
			, std::string const& save_path);

		aux::vector<link_t, file_index_t> const& get_links() const
		{ return m_links; }

	private:
		// this is the torrent we're trying to find files for.
		std::shared_ptr<torrent_info> m_torrent_file;

		// each file in m_torrent_file has an entry in this vector. Any file
		// that also exists somewhere else, is filled in with the corresponding
		// torrent_info object and file index
		aux::vector<link_t, file_index_t> m_links;

		// maps file size to file index, in m_torrent_file
		std::unordered_multimap<std::int64_t, file_index_t> m_file_sizes;
	};
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

}

#endif
