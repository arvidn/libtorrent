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

#include "libtorrent/pch.hpp"

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
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <boost/next_prior.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"

namespace gr = boost::gregorian;

using namespace libtorrent;

namespace
{
	
	namespace fs = boost::filesystem;

	void convert_to_utf8(std::string& str, unsigned char chr)
	{
		str += 0xc0 | ((chr & 0xff) >> 6);
		str += 0x80 | (chr & 0x3f);
	}

	void verify_encoding(file_entry& target)
	{
		std::string tmp_path;
		std::string file_path = target.path.string();
		bool valid_encoding = true;
		for (std::string::iterator i = file_path.begin()
			, end(file_path.end()); i != end; ++i)
		{
			// valid ascii-character
			if ((*i & 0x80) == 0)
			{
				tmp_path += *i;
				continue;
			}
			
			if (std::distance(i, end) < 2)
			{
				convert_to_utf8(tmp_path, *i);
				valid_encoding = false;
				continue;
			}
			
			// valid 2-byte utf-8 character
			if ((i[0] & 0xe0) == 0xc0
				&& (i[1] & 0xc0) == 0x80)
			{
				tmp_path += i[0];
				tmp_path += i[1];
				i += 1;
				continue;
			}

			if (std::distance(i, end) < 3)
			{
				convert_to_utf8(tmp_path, *i);
				valid_encoding = false;
				continue;
			}

			// valid 3-byte utf-8 character
			if ((i[0] & 0xf0) == 0xe0
				&& (i[1] & 0xc0) == 0x80
				&& (i[2] & 0xc0) == 0x80)
			{
				tmp_path += i[0];
				tmp_path += i[1];
				tmp_path += i[2];
				i += 2;
				continue;
			}

			if (std::distance(i, end) < 4)
			{
				convert_to_utf8(tmp_path, *i);
				valid_encoding = false;
				continue;
			}

			// valid 4-byte utf-8 character
			if ((i[0] & 0xf0) == 0xe0
				&& (i[1] & 0xc0) == 0x80
				&& (i[2] & 0xc0) == 0x80
				&& (i[3] & 0xc0) == 0x80)
			{
				tmp_path += i[0];
				tmp_path += i[1];
				tmp_path += i[2];
				tmp_path += i[3];
				i += 3;
				continue;
			}

			convert_to_utf8(tmp_path, *i);
			valid_encoding = false;
		}
		// the encoding was not valid utf-8
		// save the original encoding and replace the
		// commonly used path with the correctly
		// encoded string
		if (!valid_encoding)
		{
			target.orig_path.reset(new fs::path(target.path));
			target.path = tmp_path;
		}
	}

	bool extract_single_file(lazy_entry const& dict, file_entry& target
		, std::string const& root_dir)
	{
		lazy_entry const* length = dict.dict_find("length");
		if (length == 0 || length->type() != lazy_entry::int_t)
			return false;
		target.size = length->int_value();
		target.path = root_dir;
		target.file_base = 0;

		// prefer the name.utf-8
		// because if it exists, it is more
		// likely to be correctly encoded

		lazy_entry const* p = dict.dict_find("path.utf-8");
		if (p == 0 || p->type() != lazy_entry::list_t)
			p = dict.dict_find("path");
		if (p == 0 || p->type() != lazy_entry::list_t)
			return false;

		for (int i = 0, end(p->list_size()); i < end; ++i)
		{
			if (p->list_at(i)->type() != lazy_entry::string_t)
				return false;
			std::string path_element = p->list_at(i)->string_value();
			if (path_element != "..")
				target.path /= path_element;
		}
		verify_encoding(target);
		if (target.path.is_complete())
			return false;
		return true;
	}

	bool extract_files(lazy_entry const& list, std::vector<file_entry>& target
		, std::string const& root_dir)
	{
		size_type offset = 0;
		if (list.type() != lazy_entry::list_t) return false;
		for (int i = 0, end(list.list_size()); i < end; ++i)
		{
			target.push_back(file_entry());
			if (!extract_single_file(*list.list_at(i), target.back(), root_dir))
				return false;
			target.back().offset = offset;
			offset += target.back().size;
		}
		return true;
	}
/*
	void remove_dir(fs::path& p)
	{
		TORRENT_ASSERT(p.begin() != p.end());
		path tmp;
		for (path::iterator i = boost::next(p.begin()); i != p.end(); ++i)
			tmp /= *i;
		p = tmp;
	}
*/
}

namespace libtorrent
{

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
		: m_num_pieces(0)
		, m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, torrent_file);

		lazy_entry e;
		lazy_bdecode(&tmp[0], &tmp[0] + tmp.size(), e);
		std::string error;
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, error))
			throw invalid_torrent_file();
#else
		read_torrent_info(e, error);
#endif
	}

	torrent_info::torrent_info(lazy_entry const& torrent_file)
		: m_num_pieces(0)
		, m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		std::string error;
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(torrent_file, error))
			throw invalid_torrent_file();
#else
		read_torrent_info(torrent_file, error);
#endif
	}

	torrent_info::torrent_info(char const* buffer, int size)
		: m_num_pieces(0)
		, m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		std::string error;
		lazy_entry e;
		lazy_bdecode(buffer, buffer + size, e);
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, error))
			throw invalid_torrent_file();
#else
		read_torrent_info(e, error);
#endif
	}

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash)
		: m_piece_length(0)
		, m_total_size(0)
		, m_num_pieces(0)
		, m_info_hash(info_hash)
		, m_name()
		, m_creation_date(pt::second_clock::universal_time())
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
	}

	torrent_info::torrent_info(char const* filename)
		: m_num_pieces(0)
		, m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
	{
		size_type s = fs::file_size(fs::path(filename));
		// don't load torrent files larger than 2 MB
		if (s > 2000000) return;
		std::vector<char> buf(s);
		std::ifstream f(filename);
		f.read(&buf[0], s);

		std::string error;
		lazy_entry e;
		lazy_bdecode(&buf[0], &buf[0] + buf.size(), e);
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, error))
			throw invalid_torrent_file();
#else
		read_torrent_info(e, error);
#endif
	}

	torrent_info::~torrent_info()
	{}

	void torrent_info::swap(torrent_info& ti)
	{
		using std::swap;
		m_urls.swap(ti.m_urls);
		m_url_seeds.swap(ti.m_url_seeds);
		m_files.swap(ti.m_files);
		m_files.swap(ti.m_remapped_files);
		m_nodes.swap(ti.m_nodes);
		swap(m_num_pieces, ti.m_num_pieces);
		swap(m_info_hash, ti.m_info_hash);
		m_name.swap(ti.m_name);
		swap(m_creation_date, ti.m_creation_date);
		m_comment.swap(ti.m_comment);
		m_created_by.swap(ti.m_created_by);
		swap(m_multifile, ti.m_multifile);
		swap(m_private, ti.m_private);
		swap(m_info_section, ti.m_info_section);
		swap(m_info_section_size, ti.m_info_section_size);
		swap(m_piece_hashes, ti.m_piece_hashes);
		swap(m_info_dict, ti.m_info_dict);
	}

	bool torrent_info::parse_info_section(lazy_entry const& info, std::string& error)
	{
		if (info.type() != lazy_entry::dict_t)
		{
			error = "'info' entry is not a dictionary";
			return false;
		}

		// hash the info-field to calculate info-hash
		hasher h;
		std::pair<char const*, int> section = info.data_section();
		h.update(section.first, section.second);
		m_info_hash = h.final();

		// copy the info section
		m_info_section_size = section.second;
		m_info_section.reset(new char[m_info_section_size]);
		memcpy(m_info_section.get(), section.first, m_info_section_size);
		TORRENT_ASSERT(section.first[0] == 'd');
		TORRENT_ASSERT(section.first[m_info_section_size-1] == 'e');

		// extract piece length
		m_piece_length = info.dict_find_int_value("piece length", -1);
		if (m_piece_length <= 0)
		{
			error = "invalid or missing 'piece length' entry in torrent file";
			return false;
		}

		// extract file name (or the directory name if it's a multifile libtorrent)
		m_name = info.dict_find_string_value("name.utf-8");
		if (m_name.empty()) m_name = info.dict_find_string_value("name");

		if (m_name.empty())
		{
			error = "invalid name in torrent file";
			return false;
		}

		fs::path tmp = m_name;
		if (tmp.is_complete())
		{
			m_name = tmp.leaf();
		}
		else if (tmp.has_branch_path())
		{
			fs::path p;
			for (fs::path::iterator i = tmp.begin()
				, end(tmp.end()); i != end; ++i)
			{
				if (*i == "." || *i == "..") continue;
				p /= *i;
			}
			m_name = p.string();
		}
		if (m_name == ".." || m_name == ".")
		{
			error = "invalid 'name' of torrent (possible exploit attempt)";
			return false;
		}
	
		// extract file list
		lazy_entry const* i = info.dict_find_list("files");
		if (i == 0)
		{
			// if there's no list of files, there has to be a length
			// field.
			file_entry e;
			e.path = m_name;
			e.offset = 0;
			e.size = info.dict_find_int_value("length", -1);
			if (e.size < 0)
			{
				error = "invalid length of torrent";
				return false;
			}
			m_files.push_back(e);
			m_multifile = false;
		}
		else
		{
			if (!extract_files(*i, m_files, m_name))
			{
				error = "failed to parse files from torrent file";
				return false;
			}
			m_multifile = true;
		}

		// calculate total size of all pieces
		m_total_size = 0;
		for (std::vector<file_entry>::iterator i = m_files.begin(); i != m_files.end(); ++i)
			m_total_size += i->size;

		// extract sha-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		m_num_pieces = static_cast<int>((m_total_size + m_piece_length - 1) / m_piece_length);

		lazy_entry const* pieces = info.dict_find("pieces");
		if (pieces == 0 || pieces->type() != lazy_entry::string_t)
		{
			error = "invalid or missing 'pieces' entry in torrent file";
			return false;
		}
		
		if (pieces->string_length() != m_num_pieces * 20)
		{
			error = "incorrect number of piece hashes in torrent file";
			return false;
		}

		m_piece_hashes = m_info_section.get() + (pieces->string_ptr() - section.first);
		TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
		TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);

		m_private = info.dict_find_int_value("private", 0);
		return true;
	}

	bool torrent_info::parse_torrent_file(lazy_entry const& torrent_file, std::string& error)
	{
		if (torrent_file.type() != lazy_entry::dict_t)
		{
			error = "torrent file is not a dictionary";
			return false;
		}

		// extract the url of the tracker
		lazy_entry const* i = torrent_file.dict_find_list("announce-list");
		if (i)
		{
			m_urls.reserve(i->list_size());
			for (int j = 0, end(i->list_size()); j < end; ++j)
			{
				lazy_entry const* tier = i->list_at(j);
				if (tier->type() != lazy_entry::list_t) continue;
				for (int k = 0, end(tier->list_size()); k < end; ++k)
				{
					announce_entry e(tier->list_string_value_at(k));
					if (e.url.empty()) continue;
					e.tier = j;
					m_urls.push_back(e);
				}
			}

			// shuffle each tier
			std::vector<announce_entry>::iterator start = m_urls.begin();
			std::vector<announce_entry>::iterator stop;
			int current_tier = m_urls.front().tier;
			for (stop = m_urls.begin(); stop != m_urls.end(); ++stop)
			{
				if (stop->tier != current_tier)
				{
					std::random_shuffle(start, stop);
					start = stop;
					current_tier = stop->tier;
				}
			}
			std::random_shuffle(start, stop);
		}
		

		if (m_urls.empty())
		{
			announce_entry e(torrent_file.dict_find_string_value("announce"));
			if (!e.url.empty()) m_urls.push_back(e);
		}

		lazy_entry const* nodes = torrent_file.dict_find_list("nodes");
		if (nodes)
		{
			for (int i = 0, end(nodes->list_size()); i < end; ++i)
			{
				lazy_entry const* n = nodes->list_at(i);
				if (n->type() != lazy_entry::list_t
					|| n->list_size() < 2
					|| n->list_at(0)->type() != lazy_entry::string_t
					|| n->list_at(1)->type() != lazy_entry::int_t)
					continue;
				m_nodes.push_back(std::make_pair(
					n->list_at(0)->string_value()
					, int(n->list_at(1)->int_value())));
			}
		}

		// extract creation date
		size_type cd = torrent_file.dict_find_int_value("creation date", -1);
		if (cd >= 0)
		{
			m_creation_date = pt::ptime(gr::date(1970, gr::Jan, 1))
				+ pt::seconds(long(cd));
		}

		// if there are any url-seeds, extract them
		lazy_entry const* url_seeds = torrent_file.dict_find("url-list");
		if (url_seeds && url_seeds->type() == lazy_entry::string_t)
		{
			m_url_seeds.push_back(url_seeds->string_value());
		}
		else if (url_seeds && url_seeds->type() == lazy_entry::list_t)
		{
			for (int i = 0, end(url_seeds->list_size()); i < end; ++i)
			{
				lazy_entry const* url = url_seeds->list_at(i);
				if (url->type() != lazy_entry::string_t) continue;
				m_url_seeds.push_back(url->string_value());
			}
		}

		m_comment = torrent_file.dict_find_string_value("comment.utf-8");
		if (m_comment.empty()) m_comment = torrent_file.dict_find_string_value("comment");
	
		m_created_by = torrent_file.dict_find_string_value("created by.utf-8");
		if (m_created_by.empty()) m_created_by = torrent_file.dict_find_string_value("created by");

		lazy_entry const* info = torrent_file.dict_find_dict("info");
		if (info == 0)
		{
			error = "missing or invalid 'info' section in torrent file";
			return false;
		}
		return parse_info_section(*info, error);
	}

	boost::optional<pt::ptime>
	torrent_info::creation_date() const
	{
		if (m_creation_date != pt::ptime(gr::date(pt::not_a_date_time)))
		{
			return boost::optional<pt::ptime>(m_creation_date);
		}
		return boost::optional<pt::ptime>();
	}

	void torrent_info::add_tracker(std::string const& url, int tier)
	{
		announce_entry e(url);
		e.tier = tier;
		m_urls.push_back(e);

		using boost::bind;
		std::sort(m_urls.begin(), m_urls.end(), boost::bind<bool>(std::less<int>()
			, bind(&announce_entry::tier, _1), bind(&announce_entry::tier, _2)));
	}

// ------- start deprecation -------

	void torrent_info::print(std::ostream& os) const
	{
		os << "trackers:\n";
		for (std::vector<announce_entry>::const_iterator i = trackers().begin();
			i != trackers().end(); ++i)
		{
			os << i->tier << ": " << i->url << "\n";
		}
		if (!m_comment.empty())
			os << "comment: " << m_comment << "\n";
//		if (m_creation_date != pt::ptime(gr::date(pt::not_a_date_time)))
//			os << "creation date: " << to_simple_string(m_creation_date) << "\n";
		os << "private: " << (m_private?"yes":"no") << "\n";
		os << "number of pieces: " << num_pieces() << "\n";
		os << "piece length: " << piece_length() << "\n";
		os << "files:\n";
		for (file_iterator i = begin_files(); i != end_files(); ++i)
			os << "  " << std::setw(11) << i->size << "  " << i->path.string() << "\n";
	}

// ------- end deprecation -------

	int torrent_info::piece_size(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			int size = int(total_size()
				- (num_pieces() - 1) * piece_length());
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

	bool torrent_info::remap_files(std::vector<file_entry> const& map)
	{
		size_type offset = 0;
		m_remapped_files.resize(map.size());

		for (int i = 0; i < int(map.size()); ++i)
		{
			file_entry& fe = m_remapped_files[i];
			fe.path = map[i].path;
			fe.offset = offset;
			fe.size = map[i].size;
			fe.file_base = map[i].file_base;
			fe.orig_path.reset();
			offset += fe.size;
		}
		if (offset != total_size())
		{
			m_remapped_files.clear();
			return false;
		}

#ifndef NDEBUG
		std::vector<file_entry> map2(m_remapped_files);
		std::sort(map2.begin(), map2.end()
			, bind(&file_entry::file_base, _1) < bind(&file_entry::file_base, _2));
		std::stable_sort(map2.begin(), map2.end()
			, bind(&file_entry::path, _1) < bind(&file_entry::path, _2));
		fs::path last_path;
		size_type last_end = 0;
		for (std::vector<file_entry>::iterator i = map2.begin(), end(map2.end());
			i != end; ++i)
		{
			if (last_path == i->path)
			{
				assert(last_end <= i->file_base);
			}
			last_end = i->file_base + i->size;
			last_path = i->path;
		}
#endif

		return true;
	}

	std::vector<file_slice> torrent_info::map_block(int piece, size_type offset
		, int size_, bool storage) const
	{
		TORRENT_ASSERT(num_files() > 0);
		std::vector<file_slice> ret;

		size_type start = piece * (size_type)m_piece_length + offset;
		size_type size = size_;
		TORRENT_ASSERT(start + size <= m_total_size);

		// find the file iterator and file offset
		// TODO: make a vector that can map piece -> file index in O(1)
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		int counter = 0;
		for (file_iter = begin_files(storage);; ++counter, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != end_files(storage));
			if (file_offset < file_iter->size)
			{
				file_slice f;
				f.file_index = counter;
				f.offset = file_offset + file_iter->file_base;
				f.size = (std::min)(file_iter->size - file_offset, (size_type)size);
				size -= f.size;
				file_offset += f.size;
				ret.push_back(f);
			}
			
			TORRENT_ASSERT(size >= 0);
			if (size <= 0) break;

			file_offset -= file_iter->size;
		}
		return ret;
	}
	
	peer_request torrent_info::map_file(int file_index, size_type file_offset
		, int size, bool storage) const
	{
		TORRENT_ASSERT(file_index < num_files(storage));
		TORRENT_ASSERT(file_index >= 0);
		size_type offset = file_offset + file_at(file_index, storage).offset;

		peer_request ret;
		ret.piece = int(offset / piece_length());
		ret.start = int(offset - ret.piece * piece_length());
		ret.length = size;
		return ret;
	}

}

