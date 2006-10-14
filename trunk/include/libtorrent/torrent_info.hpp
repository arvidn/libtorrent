/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_TORRENT_INFO_HPP_INCLUDE
#define TORRENT_TORRENT_INFO_HPP_INCLUDE


#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/entry.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{

	struct TORRENT_EXPORT file_entry
	{
		boost::filesystem::path path;
		size_type offset; // the offset of this file inside the torrent
		size_type size; // the size of this file
		// if the path was incorrectly encoded, this is
		// the origianal corrupt encoded string. It is
		// preserved in order to be able to reproduce
		// the correct info-hash
		boost::shared_ptr<const boost::filesystem::path> orig_path;
	};

	struct TORRENT_EXPORT file_slice
	{
		int file_index;
		size_type offset;
		size_type size;
	};

	struct TORRENT_EXPORT announce_entry
	{
		announce_entry(std::string const& u): url(u), tier(0) {}
		std::string url;
		int tier;
	};

	struct TORRENT_EXPORT invalid_torrent_file: std::exception
	{
		virtual const char* what() const throw() { return "invalid torrent file"; }
	};

	class TORRENT_EXPORT torrent_info
	{
	public:

		torrent_info();
		torrent_info(sha1_hash const& info_hash);
		torrent_info(entry const& torrent_file);
		~torrent_info();

		entry create_torrent() const;
		entry create_info_metadata() const;
		void set_comment(char const* str);
		void set_creator(char const* str);
		void set_piece_size(int size);
		void set_hash(int index, sha1_hash const& h);
		void add_tracker(std::string const& url, int tier = 0);
		void add_file(boost::filesystem::path file, size_type size);
		void add_url_seed(std::string const& url);

		std::vector<file_slice> map_block(int piece, size_type offset, int size) const;
		peer_request map_file(int file, size_type offset, int size) const;
		
		std::vector<std::string> const& url_seeds() const { return m_url_seeds; }

		typedef std::vector<file_entry>::const_iterator file_iterator;
		typedef std::vector<file_entry>::const_reverse_iterator reverse_file_iterator;

		// list the files in the torrent file
		file_iterator begin_files() const { return m_files.begin(); }
		file_iterator end_files() const { return m_files.end(); }
		reverse_file_iterator rbegin_files() const { return m_files.rbegin(); }
		reverse_file_iterator rend_files() const { return m_files.rend(); }

		int num_files() const
		{ assert(m_piece_length > 0); return (int)m_files.size(); }
		const file_entry& file_at(int index) const
		{ assert(index >= 0 && index < (int)m_files.size()); return m_files[index]; }
		
		const std::vector<announce_entry>& trackers() const { return m_urls; }

		size_type total_size() const { assert(m_piece_length > 0); return m_total_size; }
		size_type piece_length() const { assert(m_piece_length > 0); return m_piece_length; }
		int num_pieces() const { assert(m_piece_length > 0); return (int)m_piece_hash.size(); }
		const sha1_hash& info_hash() const { return m_info_hash; }
		const std::string& name() const { assert(m_piece_length > 0); return m_name; }
		void print(std::ostream& os) const;
		bool is_valid() const { return m_piece_length > 0; }

		bool priv() const { return m_private; }
		void set_priv(bool v) { m_private = v; }

		void convert_file_names();

		size_type piece_size(int index) const;

		const sha1_hash& hash_for_piece(int index) const
		{
			assert(index >= 0);
			assert(index < (int)m_piece_hash.size());
			return m_piece_hash[index];
		}

		boost::optional<boost::posix_time::ptime>
		creation_date() const;

		const std::string& creator() const
		{ return m_created_by; }

		const std::string& comment() const
		{ return m_comment; }

		// dht nodes to add to the routing table/bootstrap from
		typedef std::vector<std::pair<std::string, int> > nodes_t;
		
		nodes_t const& nodes() const
		{ return m_nodes; }
		
		void add_node(std::pair<std::string, int> const& node);

		void parse_info_section(entry const& e);

	private:

		void read_torrent_info(const entry& libtorrent);

		// the urls to the trackers
		std::vector<announce_entry> m_urls;

		std::vector<std::string> m_url_seeds;

		// the length of one piece
		// if this is 0, the torrent_info is
		// in an uninitialized state
		size_type m_piece_length;

		// the sha-1 hashes of each piece
		std::vector<sha1_hash> m_piece_hash;

		// the list of files that this torrent consists of
		std::vector<file_entry> m_files;

		nodes_t m_nodes;

		// the sum of all filesizes
		size_type m_total_size;

		// the hash that identifies this torrent
		// is mutable because it's calculated
		// lazily
		mutable sha1_hash m_info_hash;

		std::string m_name;
		
		// if a creation date is found in the torrent file
		// this will be set to that, otherwise it'll be
		// 1970, Jan 1
		boost::posix_time::ptime m_creation_date;

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

		// contains any non-parsed entries from the info-section
		// these are kept in order to be able to accurately
		// reproduce the info-section when sending the metadata
		// to peers.
		entry m_extra_info;
	};

}

#endif // TORRENT_TORRENT_INFO_HPP_INCLUDED

