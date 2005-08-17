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

#include <ctime>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/next_prior.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"

using namespace libtorrent;
using namespace boost::filesystem;

namespace
{
	void extract_single_file(const entry& dict, file_entry& target
		, std::string const& root_dir)
	{
		target.size = dict["length"].integer();
		target.path = root_dir;


		// prefer the name.utf-8
		// because if it exists, it is more
		// likely to be correctly encoded

		const entry::list_type* list = 0;
		if (entry const* p = dict.find_key("path.utf-8"))
		{
			list = &p->list();
		}
		else
		{
			list = &dict["path"].list();
		}

		for (entry::list_type::const_iterator i = list->begin();
			i != list->end(); ++i)
		{
			if (i->string() != "..")
			target.path /= i->string();
		}
		if (target.path.is_complete()) throw std::runtime_error("torrent contains "
			"a file with an absolute path: '"
			+ target.path.native_file_string() + "'");
	}

	void extract_files(const entry::list_type& list, std::vector<file_entry>& target
		, std::string const& root_dir)
	{
		for (entry::list_type::const_iterator i = list.begin(); i != list.end(); ++i)
		{
			target.push_back(file_entry());
			extract_single_file(*i, target.back(), root_dir);
		}
	}

	void remove_dir(path& p)
	{
		assert(p.begin() != p.end());
		path tmp;
		for (path::iterator i = boost::next(p.begin()); i != p.end(); ++i)
			tmp /= *i;
		p = tmp;
	}
}

namespace libtorrent
{

	using namespace boost::gregorian;
	using namespace boost::posix_time;

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(const entry& torrent_file)
		: m_creation_date(date(not_a_date_time))
		, m_multifile(false)
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

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash)
		: m_piece_length(256 * 1024)
		, m_total_size(0)
		, m_info_hash(info_hash)
		, m_name()
		, m_creation_date(second_clock::universal_time())
		, m_multifile(false)
	{
	}

	torrent_info::torrent_info()
		: m_piece_length(256 * 1024)
		, m_total_size(0)
		, m_info_hash(0)
		, m_name()
		, m_creation_date(second_clock::universal_time())
		, m_multifile(false)
	{
	}

	torrent_info::~torrent_info()
	{}

	void torrent_info::set_piece_size(int size)
	{
		// make sure the size is an even power of 2
#ifndef NDEBUG
		for (int i = 0; i < 32; ++i)
		{
			if (size & (1 << i))
			{
				assert((size & ~(1 << i)) == 0);
				break;
			}
		}
#endif
		m_piece_length = size;


		int num_pieces = static_cast<int>(
			(m_total_size + m_piece_length - 1) / m_piece_length);
		int old_num_pieces = static_cast<int>(m_piece_hash.size());

		m_piece_hash.resize(num_pieces);
		for (int i = old_num_pieces; i < num_pieces; ++i)
		{
			m_piece_hash[i].clear();
		}
	}

	void torrent_info::parse_info_section(entry const& info)
	{
		// encode the info-field in order to calculate it's sha1-hash
		std::vector<char> buf;
		bencode(std::back_inserter(buf), info);
		hasher h;
		h.update(&buf[0], (int)buf.size());
		m_info_hash = h.final();

		// extract piece length
		m_piece_length = (int)info["piece length"].integer();

		// extract file name (or the directory name if it's a multifile libtorrent)
		if (entry const* e = info.find_key("name.utf-8"))
		{
			m_name = e->string();
		}
		else
		{
			m_name = info["name"].string();
		}
		
		path tmp = m_name;
		if (tmp.is_complete()) throw std::runtime_error("torrent contains "
			"a file with an absolute path: '" + m_name + "'");
		if (tmp.has_branch_path()) throw std::runtime_error(
			"torrent contains name with directories: '" + m_name + "'");
	
		// extract file list
		entry const* i = info.find_key("files");
		if (i == 0)
		{
			// if there's no list of files, there has to be a length
			// field.
			file_entry e;
			e.path = m_name;
			e.size = info["length"].integer();
			m_files.push_back(e);
		}
		else
		{
			extract_files(i->list(), m_files, m_name);
			m_multifile = true;
		}

		// calculate total size of all pieces
		m_total_size = 0;
		for (std::vector<file_entry>::iterator i = m_files.begin(); i != m_files.end(); ++i)
			m_total_size += i->size;

		// extract sha-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		int num_pieces = static_cast<int>((m_total_size + m_piece_length - 1) / m_piece_length);
		m_piece_hash.resize(num_pieces);
		const std::string& hash_string = info["pieces"].string();

		if ((int)hash_string.length() != num_pieces * 20)
			throw invalid_torrent_file();

		for (int i = 0; i < num_pieces; ++i)
			std::copy(
				hash_string.begin() + i*20
				, hash_string.begin() + (i+1)*20
				, m_piece_hash[i].begin());
	}

	// extracts information from a libtorrent file and fills in the structures in
	// the torrent object
	void torrent_info::read_torrent_info(const entry& torrent_file)
	{
		// extract the url of the tracker
		entry const* i = torrent_file.find_key("announce-list");
		if (i)
		{
			const entry::list_type& l = i->list();
			for (entry::list_type::const_iterator j = l.begin(); j != l.end(); ++j)
			{
				const entry::list_type& ll = j->list();
				for (entry::list_type::const_iterator k = ll.begin(); k != ll.end(); ++k)
				{
					announce_entry e(k->string());
					e.tier = (int)std::distance(l.begin(), j);
					m_urls.push_back(e);
				}
			}

			if (m_urls.size() == 0)
			{
				// the announce-list is empty
				// fall back to look for announce
				m_urls.push_back(announce_entry(
					torrent_file["announce"].string()));
			}
			// shuffle each tier
			std::vector<announce_entry>::iterator i = m_urls.begin();
			std::vector<announce_entry>::iterator j;
			int current_tier = m_urls.front().tier;
			for (j = m_urls.begin(); j != m_urls.end(); ++j)
			{
				if (j->tier != current_tier)
				{
					std::random_shuffle(i, j);
					i = j;
					current_tier = j->tier;
				}
			}
			std::random_shuffle(i, j);
		}
		else
		{
			m_urls.push_back(announce_entry(
				torrent_file["announce"].string()));
		}

		// extract creation date
		try
		{
			m_creation_date = ptime(date(1970, Jan, 1))
				+ seconds(torrent_file["creation date"].integer());
		}
		catch (type_error) {}

		// extract comment
		try
		{
			m_comment = torrent_file["comment"].string();
		}
		catch (type_error) {}
		
		// extract comment
		try
		{
			m_created_by = torrent_file["created by"].string();
		}
		catch (type_error) {}
	
		parse_info_section(torrent_file["info"]);
	}

	boost::optional<ptime>
	torrent_info::creation_date() const
	{
		if (m_creation_date != ptime(date(not_a_date_time)))
		{
			return boost::optional<ptime>(m_creation_date);
		}
		return boost::optional<ptime>();
	}

	void torrent_info::add_tracker(std::string const& url, int tier)
	{
		announce_entry e(url);
		e.tier = tier;
		m_urls.push_back(e);

		using boost::bind;
		std::sort(m_urls.begin(), m_urls.end(), bind(std::less<int>()
			, bind(&announce_entry::tier, _1), bind(&announce_entry::tier, _2)));
	}

	void torrent_info::add_file(boost::filesystem::path file, size_type size)
	{
		assert(file.begin() != file.end());

		if (!file.has_branch_path())
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			assert(m_files.empty());
			assert(!m_multifile);
			m_name = file.string();
		}
		else
		{
#ifndef NDEBUG
			if (!m_files.empty())
				assert(m_name == *file.begin());
#endif
			m_multifile = true;
			m_name = *file.begin();
		}

		file_entry e;
		e.path = file;
		e.size = size;
		m_files.push_back(e);

		m_total_size += size;

		int num_pieces = static_cast<int>(
			(m_total_size + m_piece_length - 1) / m_piece_length);
		int old_num_pieces = static_cast<int>(m_piece_hash.size());

		m_piece_hash.resize(num_pieces);
		for (std::vector<sha1_hash>::iterator i = m_piece_hash.begin() + old_num_pieces;
			i != m_piece_hash.end(); ++i)
		{
			i->clear();
		}

	}

	void torrent_info::set_comment(char const* str)
	{
		m_comment = str;
	}

	void torrent_info::set_creator(char const* str)
	{
		m_created_by = str;
	}

	entry torrent_info::create_info_metadata() const
	{
		namespace fs = boost::filesystem;

		// you have to add files to the torrent first
		assert(!m_files.empty());
	
		entry info(entry::dictionary_t);

		info["name"] = m_name;
		if (!m_multifile)
		{
			info["length"] = m_files.front().size;
		}
		else
		{
			entry& files = info["files"];
			files = entry(entry::list_t);

			for (std::vector<file_entry>::const_iterator i = m_files.begin();
				i != m_files.end(); ++i)
			{
				files.list().push_back(entry(entry::dictionary_t));
				entry& file_e = files.list().back();
				file_e["length"] = i->size;
				entry& path_e = file_e["path"];
				path_e = entry(entry::list_t);

				fs::path const& file_path(i->path);
				assert(file_path.has_branch_path());
				assert(*file_path.begin() == m_name);

				for (fs::path::iterator j = boost::next(file_path.begin());
					j != file_path.end(); ++j)
				{
					path_e.list().push_back(*j);
				}
				file_e.sort();
			}
		}

		info["piece length"] = piece_length();
		entry& pieces = info["pieces"];
		pieces = entry(entry::string_t);

		std::string& p = pieces.string();

		for (std::vector<sha1_hash>::const_iterator i = m_piece_hash.begin();
			i != m_piece_hash.end();
			++i)
		{
			p.append((char*)i->begin(), (char*)i->end());
		}

		info.sort();

		return info;
	}

	entry torrent_info::create_torrent()
	{
		assert(m_piece_length > 0);

		using namespace boost::gregorian;
		using namespace boost::posix_time;

		namespace fs = boost::filesystem;

		entry dict(entry::dictionary_t);

		if (m_urls.empty() || m_files.empty())
		{
			// TODO: throw something here
			// throw
			return entry();
		}

		dict["announce"] = m_urls.front().url;

		if (m_urls.size() > 1)
		{
			entry trackers(entry::list_t);
			entry tier(entry::list_t);
			int current_tier = m_urls.front().tier;
			for (std::vector<announce_entry>::const_iterator i = m_urls.begin();
				i != m_urls.end(); ++i)
			{
				if (i->tier != current_tier)
				{
					current_tier = i->tier;
					trackers.list().push_back(tier);
					tier.list().clear();
				}
				tier.list().push_back(i->url);
			}
			trackers.list().push_back(tier);
			dict["announce-list"] = trackers;
		}

		if (!m_comment.empty())
			dict["comment"] = m_comment;

		dict["creation date"] =
			(m_creation_date - ptime(date(1970, Jan, 1))).total_seconds();

		if (!m_created_by.empty())
			dict["created by"] = m_created_by;

		dict["info"] = create_info_metadata();
		dict.sort();

		entry const& info_section = dict["info"];
		std::vector<char> buf;
		bencode(std::back_inserter(buf), info_section);
		m_info_hash = hasher(&buf[0], buf.size()).final();

		return dict;
	}

	void torrent_info::set_hash(int index, const sha1_hash& h)
	{
		assert(index >= 0);
		assert(index < (int)m_piece_hash.size());
		m_piece_hash[index] = h;
	}

	void torrent_info::convert_file_names()
	{
		assert(false);
	}

	void torrent_info::print(std::ostream& os) const
	{
		os << "trackers:\n";
		for (std::vector<announce_entry>::const_iterator i = trackers().begin();
			i != trackers().end();
			++i)
		{
			os << i->tier << ": " << i->url << "\n";
		}
		if (!m_comment.empty())
			os << "comment: " << m_comment << "\n";
		if (m_creation_date != ptime(date(not_a_date_time)))
			os << "creation date: " << to_simple_string(m_creation_date) << "\n";
		os << "number of pieces: " << num_pieces() << "\n";
		os << "piece length: " << piece_length() << "\n";
		os << "files:\n";
		for (file_iterator i = begin_files(); i != end_files(); ++i)
			os << "  " << std::setw(11) << i->size << "  " << i->path.string() << "\n";
	}

	size_type torrent_info::piece_size(int index) const
	{
		assert(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			size_type size = total_size()
				- (num_pieces() - 1) * piece_length();
			assert(size > 0);
			assert(size <= piece_length());
			return size;
		}
		else
			return piece_length();
	}
}
