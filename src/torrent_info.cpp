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

#include <boost/lexical_cast.hpp>

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"

using namespace libtorrent;

namespace
{
	void extract_single_file(const entry::dictionary_type& dict, file& target)
	{
		entry::dictionary_type::const_iterator i = dict.find("length");
		if (i == dict.end()) throw invalid_torrent_file();
		target.size = i->second.integer();

		i = dict.find("path");
		if (i == dict.end()) throw invalid_torrent_file();

		const entry::list_type& list = i->second.list();
		for (entry::list_type::const_iterator i = list.begin(); i != list.end()-1; ++i)
		{
			target.path += '/';
			target.path += i->string();
		}
		target.path += '/';
		target.filename = list.back().string();
	}

	void extract_files(const entry::list_type& list, std::vector<file>& target, const std::string& root_directory)
	{
		for (entry::list_type::const_iterator i = list.begin(); i != list.end(); ++i)
		{
			target.push_back(file());
			target.back().path = root_directory;
			extract_single_file(i->dict(), target.back());
		}
	}
}

namespace libtorrent
{

	// extracts information from a libtorrent file and fills in the structures in
	// the torrent object
	void torrent_info::read_torrent_info(const entry& torrent_file)
	{
		// extract the url of the tracker
		const entry::dictionary_type& dict = torrent_file.dict();
		entry::dictionary_type::const_iterator i = dict.find("announce-list");
		if (i != dict.end())
		{
			const entry::list_type& l = i->second.list();
			for (entry::list_type::const_iterator j = l.begin(); j != l.end(); ++j)
			{
				const entry::list_type& ll = j->list();
				for (entry::list_type::const_iterator k = ll.begin(); k != ll.end(); ++k)
				{
					announce_entry e;
					e.tier = j - l.begin();
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
			i = dict.find("announce");
			if (i == dict.end()) throw invalid_torrent_file();
			announce_entry e;
			e.tier = 0;
			e.url = i->second.string();
			m_urls.push_back(e);
		}

		i = dict.find("info");
		if (i == dict.end()) throw invalid_torrent_file();
		entry info = i->second;

		// encode the info-field in order to calculate it's sha1-hash
		std::vector<char> buf;
		bencode(std::back_insert_iterator<std::vector<char> >(buf), info);
		hasher h;
		h.update(&buf[0], buf.size());
		h.final(m_info_hash);

		// extract piece length
		i = info.dict().find("piece length");
		if (i == info.dict().end()) throw invalid_torrent_file();
		m_piece_length = i->second.integer();

		// extract file name (or the directory name if it's a multifile libtorrent)
		i = info.dict().find("name");
		if (i == info.dict().end()) throw invalid_torrent_file();
		std::string filename = i->second.string();

		// extract file list
		i = info.dict().find("files");
		if (i == info.dict().end())
		{
			// if there's no list of files, there has to be a length
			// field.
			i = info.dict().find("length");
			if (i == info.dict().end()) throw invalid_torrent_file();

			m_files.push_back(file());
			m_files.back().filename = filename;
			m_files.back().size = i->second.integer();
		}
		else
		{
			extract_files(i->second.list(), m_files, filename);
		}

		// calculate total size of all pieces
		m_total_size = 0;
		for (std::vector<file>::iterator i = m_files.begin(); i != m_files.end(); ++i)
			m_total_size += i->size;

		// extract sha-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition
		entry::integer_type num_pieces = (m_total_size + m_piece_length - 1) / m_piece_length;
		i = info.dict().find("pieces");
		if (i == info.dict().end()) throw invalid_torrent_file();

		m_piece_hash.resize(num_pieces);

		const std::string& hash_string = i->second.string();
		if (hash_string.length() != num_pieces * 20) throw invalid_torrent_file();
		for (int i = 0; i < num_pieces; ++i)
			std::copy(hash_string.begin() + i*20, hash_string.begin() + (i+1)*20, m_piece_hash[i].begin());
	}

	int torrent_info::prioritize_tracker(int index)
	{
		if (index > m_urls.size()) return m_urls.size()-1;

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
		os << "number of pieces: " << num_pieces() << "\n";
		os << "piece length: " << piece_length() << "\n";
		os << "files:\n";
		for (file_iterator i = begin_files(); i != end_files(); ++i)
			os << "  " << std::setw(11) << i->size << "  " << i->path << " " << i->filename << "\n";
	}

}