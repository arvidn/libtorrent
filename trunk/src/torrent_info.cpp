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
#include <boost/date_time/time.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/filesystem/path.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"

using namespace libtorrent;

namespace
{
	void extract_single_file(const entry& dict, file_entry& target)
	{
		target.size = dict["length"].integer();
		const entry::list_type& list = dict["path"].list();
		for (entry::list_type::const_iterator i = list.begin();
			i != list.end();
			++i)
		{
			target.path /= i->string();
		}
	}

	void extract_files(const entry::list_type& list, std::vector<file_entry>& target)
	{
		for (entry::list_type::const_iterator i = list.begin(); i != list.end(); ++i)
		{
			target.push_back(file_entry());
			extract_single_file(*i, target.back());
		}
	}

	size_type to_seconds(const boost::posix_time::time_duration& d)
	{
		return d.hours() * 60 * 60
			+ d.minutes() * 60
			+ d.seconds();
	}
}

namespace libtorrent
{

	using namespace boost::gregorian;
	using namespace boost::posix_time;

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(const entry& torrent_file)
		: m_creation_date(date(not_a_date_time))
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
	torrent_info::torrent_info(
		int piece_size
		, const char* name
		, sha1_hash const& info_hash)
		: m_piece_length(piece_size)
		, m_total_size(0)
		, m_info_hash(info_hash)
		, m_name(name)
		, m_creation_date(second_clock::local_time())
	{
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
		m_name = info["name"].string();

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
			extract_files(i->list(), m_files);
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
					announce_entry e;
					e.tier = (int)std::distance(l.begin(), j);
					e.url = k->string();
					m_urls.push_back(e);
				}
			}

			if (m_urls.size() == 0) throw invalid_torrent_file();
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
			i = torrent_file.find_key("announce");
			if (i == 0) throw invalid_torrent_file();
			announce_entry e;
			e.tier = 0;
			e.url = i->string();
			m_urls.push_back(e);
		}

		// extract creation date
		i = torrent_file.find_key("creation date");
		if (i != 0 && i->type() == entry::int_t)
		{
			m_creation_date
				= ptime(date(1970, Jan, 1))
				+ seconds((long)i->integer());
		}

		// extract comment
		i = torrent_file.find_key("comment");
		if (i != 0 && i->type() == entry::string_t)
		{
			m_comment = i->string();
		}

		// extract comment
		i = torrent_file.find_key("created by");
		if (i != 0 && i->type() == entry::string_t)
		{
			m_created_by = i->string();
		}

		i = torrent_file.find_key("info");
		if (i == 0) throw invalid_torrent_file();
		entry const& info = *i;

		parse_info_section(info);
	}

	boost::optional<boost::posix_time::ptime>
	torrent_info::creation_date() const
	{
		if (m_creation_date !=
			boost::posix_time::ptime(
			boost::gregorian::date(
				boost::date_time::not_a_date_time)))
		{
			return boost::optional<boost::posix_time::ptime>(m_creation_date);
		}
		return boost::optional<boost::posix_time::ptime>();
	}

	void torrent_info::add_tracker(std::string const& url, int tier)
	{
		announce_entry e;
		e.url = url;
		e.tier = tier;
		m_urls.push_back(e);
	}

	void torrent_info::add_file(boost::filesystem::path file, size_type size)
	{
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
			i != m_piece_hash.end();
			++i)
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

		entry info(entry::dictionary_t);

		info["length"] = m_total_size;

		if (m_files.size() == 1)
		{
			info["name"] = m_files.front().path.string();
		}
		else
		{
			info["name"] = m_name;
		}

		if (m_files.size() > 1)
		{
			entry& files = info["files"];
			files = entry(entry::list_t);

			for (std::vector<file_entry>::const_iterator i = m_files.begin();
				i != m_files.end();
				++i)
			{
				files.list().push_back(entry(entry::dictionary_t));
				entry& file_e = files.list().back();
				file_e["length"] = i->size;
				entry& path_e = file_e["path"];
				path_e = entry(entry::list_t);

				fs::path file_path(i->path);

				for (fs::path::iterator j = file_path.begin();
					j != file_path.end();
					++j)
				{
					path_e.list().push_back(*j);
				}
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
		return info;
	}

	entry torrent_info::create_torrent() const
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
		
		if (!m_comment.empty())
			dict["comment"] = m_comment;

		dict["creation date"] =
			to_seconds(m_creation_date - ptime(date(1970, Jan, 1)));

		if (!m_created_by.empty())
			dict["created by"] = m_created_by;

		dict["info"] = create_info_metadata();

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

	int torrent_info::prioritize_tracker(int index)
	{
		assert(index >= 0);
		if (index >= (int)m_urls.size()) return (int)m_urls.size()-1;

		while (index > 0 && m_urls[index].tier == m_urls[index-1].tier)
		{
			std::swap(m_urls[index].url, m_urls[index-1].url);
			--index;
		}
		return index;
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
		if (m_creation_date != boost::posix_time::ptime(boost::gregorian::date(1970, boost::gregorian::Jan, 1)))
			os << "creation date: " << boost::posix_time::to_simple_string(m_creation_date) << "\n";
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
