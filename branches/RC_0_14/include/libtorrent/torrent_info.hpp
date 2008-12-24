/*

Copyright (c) 2003-2008, Arvid Norberg
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

#ifndef TORRENT_TORRENT_INFO_HPP_INCLUDED
#define TORRENT_TORRENT_INFO_HPP_INCLUDED

#include <string>
#include <vector>
#include <iosfwd>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/shared_array.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/entry.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/file_storage.hpp"

namespace libtorrent
{
	namespace pt = boost::posix_time;
	namespace gr = boost::gregorian;
	namespace fs = boost::filesystem;

	struct TORRENT_EXPORT announce_entry
	{
		announce_entry(std::string const& u): url(u), tier(0) {}
		std::string url;
		int tier;
	};

#ifndef BOOST_NO_EXCEPTIONS
	struct TORRENT_EXPORT invalid_torrent_file: std::exception
	{
		virtual const char* what() const throw() { return "invalid torrent file"; }
	};
#endif

	int TORRENT_EXPORT load_file(fs::path const& filename, std::vector<char>& v);

	class TORRENT_EXPORT torrent_info : public intrusive_ptr_base<torrent_info>
	{
	public:

		torrent_info(sha1_hash const& info_hash);
		torrent_info(lazy_entry const& torrent_file);
		torrent_info(char const* buffer, int size);
		torrent_info(fs::path const& filename);
		~torrent_info();

		file_storage const& files() const { return m_files; }
		file_storage const& orig_files() const { return m_orig_files ? *m_orig_files : m_files; }

		void rename_file(int index, std::string const& new_filename)
		{
			copy_on_write();
			m_files.rename_file(index, new_filename);
		}

		void add_tracker(std::string const& url, int tier = 0);
		std::vector<announce_entry> const& trackers() const { return m_urls; }

		std::vector<std::string> const& url_seeds() const
		{ return m_url_seeds; }
		void add_url_seed(std::string const& url)
		{ m_url_seeds.push_back(url); }

		size_type total_size() const { return m_files.total_size(); }
		int piece_length() const { return m_files.piece_length(); }
		int num_pieces() const { return m_files.num_pieces(); }
		const sha1_hash& info_hash() const { return m_info_hash; }
		const std::string& name() const { return m_files.name(); }

		typedef file_storage::iterator file_iterator;
		typedef file_storage::reverse_iterator reverse_file_iterator;

		file_iterator begin_files() const { return m_files.begin(); }
		file_iterator end_files() const { return m_files.end(); }
		reverse_file_iterator rbegin_files() const { return m_files.rbegin(); }
		reverse_file_iterator rend_files() const { return m_files.rend(); }
		int num_files() const { return m_files.num_files(); }
		file_entry const& file_at(int index) const { return m_files.at(index); }

		file_iterator file_at_offset(size_type offset) const
		{ return m_files.file_at_offset(offset); }
		std::vector<file_slice> map_block(int piece, size_type offset, int size) const
		{ return m_files.map_block(piece, offset, size); }
		peer_request map_file(int file, size_type offset, int size) const
		{ return m_files.map_file(file, offset, size); }
		
#ifndef TORRENT_NO_DEPRECATE
// ------- start deprecation -------
// these functions will be removed in a future version
		torrent_info(entry const& torrent_file) TORRENT_DEPRECATED;
		void print(std::ostream& os) const TORRENT_DEPRECATED;
		file_storage& files() TORRENT_DEPRECATED { return m_files; }
// ------- end deprecation -------
#endif

		bool is_valid() const { return m_files.is_valid(); }

		bool priv() const { return m_private; }

		int piece_size(int index) const { return m_files.piece_size(index); }

		sha1_hash hash_for_piece(int index) const
		{ return sha1_hash(hash_for_piece_ptr(index)); }

		char const* hash_for_piece_ptr(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < m_files.num_pieces());
			TORRENT_ASSERT(m_piece_hashes);
			TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
			TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
			return &m_piece_hashes[index*20];
		}

		boost::optional<pt::ptime> creation_date() const;

		const std::string& creator() const
		{ return m_created_by; }

		const std::string& comment() const
		{ return m_comment; }

		// dht nodes to add to the routing table/bootstrap from
		typedef std::vector<std::pair<std::string, int> > nodes_t;
		
		nodes_t const& nodes() const
		{ return m_nodes; }
		void add_node(std::pair<std::string, int> const& node)
		{ m_nodes.push_back(node); }
		
		bool parse_info_section(lazy_entry const& e, std::string& error);

		lazy_entry const* info(char const* key) const
		{
			if (m_info_dict.type() == lazy_entry::none_t)
				lazy_bdecode(m_info_section.get(), m_info_section.get()
					+ m_info_section_size, m_info_dict);
			return m_info_dict.dict_find(key);
		}

		void swap(torrent_info& ti);

		boost::shared_array<char> metadata() const
		{ return m_info_section; }

		int metadata_size() const { return m_info_section_size; }

	private:

		void copy_on_write();
		bool parse_torrent_file(lazy_entry const& libtorrent, std::string& error);

		file_storage m_files;

		// if m_files is modified, it is first copied into
		// m_orig_files so that the original name and
		// filenames are preserved.
		boost::shared_ptr<const file_storage> m_orig_files;

		// the urls to the trackers
		std::vector<announce_entry> m_urls;
		std::vector<std::string> m_url_seeds;
		nodes_t m_nodes;

		// the hash that identifies this torrent
		sha1_hash m_info_hash;

		// if a creation date is found in the torrent file
		// this will be set to that, otherwise it'll be
		// 1970, Jan 1
		pt::ptime m_creation_date;

		// if a comment is found in the torrent file
		// this will be set to that comment
		std::string m_comment;

		// an optional string naming the software used
		// to create the torrent file
		std::string m_created_by;

		// this is used when creating a torrent. If there's
		// only one file there are cases where it's impossible
		// to know if it should be written as a multifile torrent
		// or not. e.g. test/test  there's one file and one directory
		// and they have the same name.
		bool m_multifile;
		
		// this is true if the torrent is private. i.e., is should not
		// be announced on the dht
		bool m_private;

		// this is a copy of the info section from the torrent.
		// it use maintained in this flat format in order to
		// make it available through the metadata extension
		boost::shared_array<char> m_info_section;
		int m_info_section_size;

		// this is a pointer into the m_info_section buffer
		// pointing to the first byte of the first sha-1 hash
		char const* m_piece_hashes;

		// the info section parsed. points into m_info_section
		// parsed lazily
		mutable lazy_entry m_info_dict;
	};

}

#endif // TORRENT_TORRENT_INFO_HPP_INCLUDED

