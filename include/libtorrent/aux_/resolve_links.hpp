/*

Copyright (c) 2015-2020, Arvid Norberg
Copyright (c) 2016, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
#include "libtorrent/fwd.hpp"

namespace lt::aux {

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
