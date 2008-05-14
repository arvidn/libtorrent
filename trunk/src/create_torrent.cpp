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

#include "libtorrent/create_torrent.hpp"
#include "libtorrent/hasher.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/bind.hpp>

namespace gr = boost::gregorian;

namespace libtorrent
{
	create_torrent::create_torrent()
		: m_piece_length(0)
		, m_total_size(0)
		, m_num_pieces(0)
		, m_info_hash()
		, m_name()
		, m_creation_date(pt::second_clock::universal_time())
		, m_multifile(false)
		, m_private(false)
	{}

	entry create_torrent::generate() const
	{
		TORRENT_ASSERT(m_piece_length > 0);

		if (m_files.empty())
		{
			// TODO: throw something here
			// throw
			return entry();
		}

		entry dict;

		if (!m_urls.empty()) dict["announce"] = m_urls.front().first;
		
		if (!m_nodes.empty())
		{
			entry& nodes = dict["nodes"];
			entry::list_type& nodes_list = nodes.list();
			for (nodes_t::const_iterator i = m_nodes.begin()
				, end(m_nodes.end()); i != end; ++i)
			{
				entry::list_type node;
				node.push_back(entry(i->first));
				node.push_back(entry(i->second));
				nodes_list.push_back(entry(node));
			}
		}

		if (m_urls.size() > 1)
		{
			entry trackers(entry::list_t);
			entry tier(entry::list_t);
			int current_tier = m_urls.front().second;
			for (std::vector<announce_entry>::const_iterator i = m_urls.begin();
				i != m_urls.end(); ++i)
			{
				if (i->second != current_tier)
				{
					current_tier = i->second;
					trackers.list().push_back(tier);
					tier.list().clear();
				}
				tier.list().push_back(entry(i->first));
			}
			trackers.list().push_back(tier);
			dict["announce-list"] = trackers;
		}

		if (!m_comment.empty())
			dict["comment"] = m_comment;

		dict["creation date"] =
			(m_creation_date - pt::ptime(gr::date(1970, gr::Jan, 1))).total_seconds();

		if (!m_created_by.empty())
			dict["created by"] = m_created_by;
			
		if (!m_url_seeds.empty())
		{
			if (m_url_seeds.size() == 1)
			{
				dict["url-list"] = m_url_seeds.front();
			}
			else
			{
				entry& list = dict["url-list"];
				for (std::vector<std::string>::const_iterator i
					= m_url_seeds.begin(); i != m_url_seeds.end(); ++i)
				{
					list.list().push_back(entry(*i));
				}
			}
		}

		entry& info = dict["info"];

		info["name"] = m_name;


		if (m_private) info["private"] = 1;

		if (!m_multifile)
		{
			info["length"] = m_files.front().second;
		}
		else
		{
			if (!info.find_key("files"))
			{
				entry& files = info["files"];

				for (std::vector<file_entry>::const_iterator i = m_files.begin();
					i != m_files.end(); ++i)
				{
					files.list().push_back(entry());
					entry& file_e = files.list().back();
					file_e["length"] = i->second;
					entry& path_e = file_e["path"];

					TORRENT_ASSERT(i->first.has_branch_path());
					TORRENT_ASSERT(*i->first.begin() == m_name);

					for (fs::path::iterator j = boost::next(i->first.begin());
						j != i->first.end(); ++j)
					{
						path_e.list().push_back(entry(*j));
					}
				}
			}
		}

		info["piece length"] = m_piece_length;
		entry& pieces = info["pieces"];

		std::string& p = pieces.string();

		for (std::vector<sha1_hash>::const_iterator i = m_piece_hash.begin();
			i != m_piece_hash.end(); ++i)
		{
			p.append((char*)i->begin(), (char*)i->end());
		}

		std::vector<char> buf;
		bencode(std::back_inserter(buf), info);
		m_info_hash = hasher(&buf[0], buf.size()).final();

		return dict;
	
	}

	int create_torrent::piece_size(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			int size = int(m_total_size
				- (num_pieces() - 1) * piece_length());
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

	void create_torrent::add_tracker(std::string const& url, int tier)
	{
		m_urls.push_back(announce_entry(url, tier));

		using boost::bind;
		std::sort(m_urls.begin(), m_urls.end()
			, bind(&announce_entry::second, _1) < bind(&announce_entry::second, _2));
	}

	void create_torrent::set_piece_size(int size)
	{
		// make sure the size is an even power of 2
#ifndef NDEBUG
		for (int i = 0; i < 32; ++i)
		{
			if (size & (1 << i))
			{
				TORRENT_ASSERT((size & ~(1 << i)) == 0);
				break;
			}
		}
#endif
		m_piece_length = size;

		m_num_pieces = static_cast<int>(
			(m_total_size + m_piece_length - 1) / m_piece_length);
		int old_num_pieces = static_cast<int>(m_piece_hash.size());

		m_piece_hash.resize(m_num_pieces);
		for (int i = old_num_pieces; i < m_num_pieces; ++i)
		{
			m_piece_hash[i].clear();
		}
	}

	void create_torrent::set_hash(int index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_hash.size());
		m_piece_hash[index] = h;
	}

	void create_torrent::add_file(fs::path file, size_type size)
	{
//		TORRENT_ASSERT(file.begin() != file.end());

		if (!file.has_branch_path())
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT(m_files.empty());
			TORRENT_ASSERT(!m_multifile);
			m_name = file.string();
		}
		else
		{
#ifndef NDEBUG
			if (!m_files.empty())
				TORRENT_ASSERT(m_name == *file.begin());
#endif
			m_multifile = true;
			m_name = *file.begin();
		}

		m_files.push_back(file_entry(file, size));

		m_total_size += size;
		
		if (m_piece_length == 0)
			m_piece_length = 256 * 1024;

		m_num_pieces = int((m_total_size + m_piece_length - 1) / m_piece_length);
		int old_num_pieces = int(m_piece_hash.size());

		m_piece_hash.resize(m_num_pieces);
		if (m_num_pieces > old_num_pieces)
			std::for_each(m_piece_hash.begin() + old_num_pieces
				, m_piece_hash.end(), boost::bind(&sha1_hash::clear, _1));
	}

	void create_torrent::add_node(std::pair<std::string, int> const& node)
	{
		m_nodes.push_back(node);
	}

	void create_torrent::add_url_seed(std::string const& url)
	{
		m_url_seeds.push_back(url);
	}

	void create_torrent::set_comment(char const* str)
	{
		m_comment = str;
	}

	void create_torrent::set_creator(char const* str)
	{
		m_created_by = str;
	}
}

