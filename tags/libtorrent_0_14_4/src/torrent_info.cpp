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

#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/file.hpp"

namespace gr = boost::gregorian;

using namespace libtorrent;

namespace libtorrent
{
	
	namespace fs = boost::filesystem;

	void convert_to_utf8(std::string& str, unsigned char chr)
	{
		str += 0xc0 | ((chr & 0xff) >> 6);
		str += 0x80 | (chr & 0x3f);
	}

	bool verify_encoding(std::string& target)
	{
		std::string tmp_path;
		bool valid_encoding = true;
		for (std::string::iterator i = target.begin()
			, end(target.end()); i != end; ++i)
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
		if (!valid_encoding) target = tmp_path;
		return valid_encoding;
	}

	void verify_encoding(file_entry& target)
	{
		std::string p = target.path.string();
		if (!verify_encoding(p)) target.path = p;
	}

	bool valid_path_element(std::string const& element)
	{
		if (element.empty()
			|| element == "." || element == ".."
			|| element[0] == '/' || element[0] == '\\'
			|| element[element.size()-1] == ':')
			return false;
		return true;
	}

	fs::path sanitize_path(fs::path const& p)
	{
		fs::path new_path;
		for (fs::path::const_iterator i = p.begin(); i != p.end(); ++i)
		{
			if (!valid_path_element(*i)) continue;
			new_path /= *i;
		}
		TORRENT_ASSERT(!new_path.is_complete());
		return new_path;
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
			target.path /= path_element;
		}
		target.path = sanitize_path(target.path);
		verify_encoding(target);
		TORRENT_ASSERT(!target.path.is_complete());

		if (target.path.is_complete())
			return false;
		return true;
	}

	bool extract_files(lazy_entry const& list, file_storage& target
		, std::string const& root_dir)
	{
		if (list.type() != lazy_entry::list_t) return false;
		for (int i = 0, end(list.list_size()); i < end; ++i)
		{
			file_entry e;
			if (!extract_single_file(*list.list_at(i), e, root_dir))
				return false;
			target.add_file(e);
		}
		return true;
	}

	int load_file(fs::path const& filename, std::vector<char>& v)
	{
		file f;
		error_code ec;
		if (!f.open(filename, file::in, ec)) return -1;
		f.seek(0, file::end, ec);
		if (ec) return -1;
		size_type s = f.tell(ec);
		if (ec) return -1;
		if (s > 5000000) return -2;
		v.resize(s);
		if (s == 0) return 0;
		f.seek(0, file::begin, ec);
		if (ec) return -1;
		size_type read = f.read(&v[0], s, ec);
		if (read != s) return -3;
		if (ec) return -3;
		return 0;
	}

#ifndef TORRENT_NO_DEPRECATE
	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
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
		parse_torrent_file(e, error);
#endif
	}
#endif

	torrent_info::torrent_info(lazy_entry const& torrent_file)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
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
		parse_torrent_file(torrent_file, error);
#endif
	}

	torrent_info::torrent_info(char const* buffer, int size)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
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
		parse_torrent_file(e, error);
#endif
	}

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash)
		: m_info_hash(info_hash)
		, m_creation_date(pt::second_clock::universal_time())
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{}

	torrent_info::torrent_info(fs::path const& filename)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf);
		if (ret < 0) return;

		if (buf.empty())
#ifndef BOOST_NO_EXCEPTIONS
			throw invalid_torrent_file();
#else
			return;
#endif

		lazy_entry e;
		lazy_bdecode(&buf[0], &buf[0] + buf.size(), e);
		std::string error;
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, error))
			throw invalid_torrent_file();
#else
		parse_torrent_file(e, error);
#endif
	}

	torrent_info::~torrent_info()
	{}

	void torrent_info::copy_on_write()
	{
		if (m_orig_files) return;
		m_orig_files.reset(new file_storage(m_files));
	}

	void torrent_info::swap(torrent_info& ti)
	{
		using std::swap;
		m_urls.swap(ti.m_urls);
		m_url_seeds.swap(ti.m_url_seeds);
		m_files.swap(ti.m_files);
		m_orig_files.swap(ti.m_orig_files);
		m_nodes.swap(ti.m_nodes);
		swap(m_info_hash, ti.m_info_hash);
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
		std::memcpy(m_info_section.get(), section.first, m_info_section_size);
		TORRENT_ASSERT(section.first[0] == 'd');
		TORRENT_ASSERT(section.first[m_info_section_size-1] == 'e');

		// extract piece length
		int piece_length = info.dict_find_int_value("piece length", -1);
		if (piece_length <= 0)
		{
			error = "invalid or missing 'piece length' entry in torrent file";
			return false;
		}
		m_files.set_piece_length(piece_length);

		// extract file name (or the directory name if it's a multifile libtorrent)
		std::string name = info.dict_find_string_value("name.utf-8");
		if (name.empty()) name = info.dict_find_string_value("name");
		if (name.empty())
		{
			error = "missing name in torrent file";
			return false;
		}

		name = sanitize_path(name).string();
		if (!valid_path_element(name))
		{
			error = "invalid 'name' of torrent (possible exploit attempt)";
			return false;
		}

		// correct utf-8 encoding errors
		verify_encoding(name);
	
		// extract file list
		lazy_entry const* i = info.dict_find_list("files");
		if (i == 0)
		{
			// if there's no list of files, there has to be a length
			// field.
			file_entry e;
			e.path = name;
			e.offset = 0;
			e.size = info.dict_find_int_value("length", -1);
			if (e.size < 0)
			{
				error = "invalid length of torrent";
				return false;
			}
			m_files.add_file(e);
			m_multifile = false;
		}
		else
		{
			if (!extract_files(*i, m_files, name))
			{
				error = "failed to parse files from torrent file";
				return false;
			}
			m_multifile = true;
		}
		m_files.set_name(name);

		// extract sha-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		m_files.set_num_pieces(int((m_files.total_size() + m_files.piece_length() - 1)
			/ m_files.piece_length()));

		lazy_entry const* pieces = info.dict_find("pieces");
		if (pieces == 0 || pieces->type() != lazy_entry::string_t)
		{
			error = "invalid or missing 'pieces' entry in torrent file";
			return false;
		}
		
		if (pieces->string_length() != m_files.num_pieces() * 20)
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

			if (!m_urls.empty())
			{
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
		verify_encoding(m_comment);
	
		m_created_by = torrent_file.dict_find_string_value("created by.utf-8");
		if (m_created_by.empty()) m_created_by = torrent_file.dict_find_string_value("created by");
		verify_encoding(m_created_by);

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

#ifndef TORRENT_NO_DEPRECATE
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
		for (file_storage::iterator i = m_files.begin(); i != m_files.end(); ++i)
			os << "  " << std::setw(11) << i->size << "  " << i->path.string() << "\n";
	}

// ------- end deprecation -------
#endif

}

