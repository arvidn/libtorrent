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
#include <boost/date_time/posix_time/posix_time.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/size_type.hpp"


/*
 * This file declares the following functions:
 *
 *
 *
 *
 */

namespace libtorrent
{

	struct file_entry
	{
		std::string path;
		std::string filename;
		size_type size;
	};

	struct announce_entry
	{
		std::string url;
		int tier;
	};

	struct invalid_torrent_file: std::exception
	{
		virtual const char* what() const throw() { return "invalid torrent file"; }
	};

	// TODO: add a check to see if filenames are accepted on the
	// current platform.
	// TODO: add a filename converter function that will transform
	// invalid filenames to valid filenames on the current platform

	class torrent_info
	{
	public:

		torrent_info(const entry& torrent_file)
			: m_creation_date(boost::gregorian::date(1970
				, boost::gregorian::Jan
				, 1))
		{
			try
			{
				read_torrent_info(torrent_file);
			}
			catch(type_error&)
			{
				throw invalid_torrent_file();
			}
		}

		typedef std::vector<file_entry>::const_iterator file_iterator;
		typedef std::vector<file_entry>::const_reverse_iterator reverse_file_iterator;

		// list the files in the torrent file
		file_iterator begin_files() const { return m_files.begin(); }
		file_iterator end_files() const { return m_files.end(); }
		reverse_file_iterator rbegin_files() const { return m_files.rbegin(); }
		reverse_file_iterator rend_files() const { return m_files.rend(); }

		std::size_t num_files() const { return m_files.size(); }
		const file_entry& file_at(int index) const { assert(index >= 0 && index < (int)m_files.size()); return m_files[index]; }

		const std::vector<announce_entry>& trackers() const { return m_urls; }

		// this will move the tracker with the given index
		// to a prioritized position in the list (move it towards
		// the begining) and return the new index to the tracker.
		int prioritize_tracker(int index);

		size_type total_size() const { return m_total_size; }
		size_type piece_length() const { return m_piece_length; }
		int num_pieces() const { return (int)m_piece_hash.size(); }
		const sha1_hash& info_hash() const { return m_info_hash; }
		const std::string& name() const { return m_name; }
		void print(std::ostream& os) const;

		void convert_file_names();

		size_type piece_size(int index) const
		{
			assert(index >= 0 && index < num_pieces());
			if (index == num_pieces()-1)
			{
				size_type s = total_size() - (size_type)(num_pieces() - 1)*piece_length();
				assert(s > 0);
				assert(s <= piece_length());
				return s;
			}
			else
				return piece_length();
		}

		const sha1_hash& hash_for_piece(int index) const
		{
			assert(index >= 0 && index < (int)m_piece_hash.size());
			return m_piece_hash[index];
		}

		boost::posix_time::ptime creation_date() const
		{ return m_creation_date; }

		const std::string& comment() const
		{ return m_comment; }

	private:

		void read_torrent_info(const entry& libtorrent);

		// the urls to the trackers
		std::vector<announce_entry> m_urls;

		// the length of one piece
		size_type m_piece_length;

		// the sha-1 hashes of each piece
		std::vector<sha1_hash> m_piece_hash;

		// the list of files that this torrent consists of
		std::vector<file_entry> m_files;

		// the sum of all filesizes
		size_type m_total_size;

		// the hash that identifies this torrent
		sha1_hash m_info_hash;

		std::string m_name;
		
		// if a creation date is found in the torrent file
		// this will be set to that, otherwise it'll be
		// 1970, Jan 1
		boost::posix_time::ptime m_creation_date;

		// if a comment is found in the torrent file
		// this will be set to that comment
		std::string m_comment;
	};

}

#endif // TORRENT_TORRENT_INFO_HPP_INCLUDED

