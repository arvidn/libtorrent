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

#if !defined TORRENT_NO_DEPRECATE && TORRENT_USE_IOSTREAM
#include <iostream>
#include <iomanip>
#endif

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

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/utf8.hpp"

namespace gr = boost::gregorian;

namespace libtorrent
{
	
	namespace fs = boost::filesystem;

	void convert_to_utf8(std::string& str, unsigned char chr)
	{
		str += 0xc0 | ((chr & 0xff) >> 6);
		str += 0x80 | (chr & 0x3f);
	}

	bool valid_path_character(char c)
	{
#ifdef TORRENT_WINDOWS
		static const char invalid_chars[] = "?<>\"|\b*:";
#else
		static const char invalid_chars[] = "";
#endif
		if (c >= 0 && c < 32) return false;
		return std::strchr(invalid_chars, c) == 0;
	}

	// fixes invalid UTF-8 sequences and
	// replaces characters that are invalid
	// in paths
	TORRENT_EXPORT bool verify_encoding(std::string& target, bool fix_paths = false)
	{
		std::string tmp_path;
		bool valid_encoding = true;
		for (std::string::iterator i = target.begin()
			, end(target.end()); i != end; ++i)
		{
			// valid ascii-character
			if ((*i & 0x80) == 0)
			{
				// replace invalid characters with '.'
				if (!fix_paths || valid_path_character(*i))
				{
					tmp_path += *i;
				}
				else
				{
					tmp_path += '_';
					valid_encoding = false;
				}
				continue;
			}
			
			if (end - i < 2)
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

			if (end - i < 3)
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

			if (end - i < 4)
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
		if (!verify_encoding(p, true)) target.path = p;
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

	void trim_path_element(std::string& path_element)
	{
#ifdef FILENAME_MAX
		const int max_path_len = FILENAME_MAX;
#else
		// on windows, NAME_MAX refers to Unicode characters
		// on linux it refers to bytes (utf-8 encoded)
		// TODO: Make this count Unicode characters instead of bytes on windows
		const int max_path_len = NAME_MAX;
#endif
		if (path_element.size() > max_path_len)
		{
			// truncate filenames that are too long. But keep extensions!
			std::string ext = fs::extension(path_element);
			if (ext.size() > 15)
			{
				path_element.resize(max_path_len);
			}
			else
			{
				path_element.resize(max_path_len - ext.size());
				path_element += ext;
			}
		}
	}

	TORRENT_EXPORT fs::path sanitize_path(fs::path const& p)
	{
		fs::path new_path;
		for (fs::path::const_iterator i = p.begin(); i != p.end(); ++i)
		{
			if (!valid_path_element(*i)) continue;
			std::string pe = *i;
			trim_path_element(pe);
			new_path /= pe;
		}
		TORRENT_ASSERT(!new_path.is_complete());
		return new_path;
	}

	bool extract_single_file(lazy_entry const& dict, file_entry& target
		, std::string const& root_dir)
	{
		if (dict.type() != lazy_entry::dict_t) return false;
		lazy_entry const* length = dict.dict_find("length");
		if (length == 0 || length->type() != lazy_entry::int_t)
			return false;
		target.size = length->int_value();
		target.path = root_dir;
		target.file_base = 0;

		size_type ts = dict.dict_find_int_value("mtime", -1);
		if (ts >= 0) target.mtime = std::time_t(ts);

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
			trim_path_element(path_element);
			target.path /= path_element;
		}
		target.path = sanitize_path(target.path);
		verify_encoding(target);
		TORRENT_ASSERT(!target.path.is_complete());

		// bitcomet pad file
		if (target.path.string().find("_____padding_file_") != std::string::npos)
			target.pad_file = true;

		lazy_entry const* attr = dict.dict_find_string("attr");
		if (attr)
		{
			for (int i = 0; i < attr->string_length(); ++i)	
			{
				switch (attr->string_ptr()[i])
				{
					case 'l': target.symlink_attribute = true; target.size = 0; break;
					case 'x': target.executable_attribute = true; break;
					case 'h': target.hidden_attribute = true; break;
					case 'p': target.pad_file = true; break;
				}
			}
		}

		lazy_entry const* s_p = dict.dict_find("symlink path");
		if (s_p != 0 && s_p->type() == lazy_entry::list_t)
		{
			for (int i = 0, end(s_p->list_size()); i < end; ++i)
			{
				std::string path_element = s_p->list_at(i)->string_value();
				trim_path_element(path_element);
				target.symlink_path /= path_element;
			}
		}

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
#if BOOST_VERSON > 103600
			int cnt = 0;
			for (file_storage::iterator k = target.begin()
				, end(target.end()); k != end; ++k)
			{
				if (string_equal_no_case(e.path.string().c_str()
					, k->path.string().c_str())) ++cnt;
			}
			if (cnt)
			{
				char suffix[15];
				snprintf(suffix, sizeof(suffix), ".%d", cnt);
				e.path.replace_extension(suffix + e.path.extension());
				// TODO: we should really make sure that this new name
				// doesn't already exist as well, otherwise we might
				// just create another collision
			}
#endif
			target.add_file(e);
		}
		return true;
	}

	int merkle_get_parent(int tree_node)
	{
		// node 0 doesn't have a parent
		TORRENT_ASSERT(tree_node > 0);
		return (tree_node - 1) / 2;
	}

	int merkle_get_sibling(int tree_node)
	{
		// node 0 doesn't have a sibling
		TORRENT_ASSERT(tree_node > 0);
		// even numbers have their sibling to the left
		// odd numbers have their sibling to the right
		return tree_node + (tree_node&1?1:-1);
	}

	int merkle_num_nodes(int leafs)
	{
		TORRENT_ASSERT(leafs > 0);
		return (leafs << 1) - 1;
	}

	int merkle_num_leafs(int pieces)
	{
		// round up to nearest 2 exponent
		int i;
		for (i = 0; pieces > 0; pieces >>= 1, ++i);
		return 1 << i;
	}

	int load_file(fs::path const& filename, std::vector<char>& v)
	{
		file f;
		error_code ec;
		if (!f.open(filename, file::read_only, ec)) return -1;
		size_type s = f.get_size(ec);
		if (ec) return -1;
		if (s > 5000000) return -2;
		v.resize(s);
		if (s == 0) return 0;
		file::iovec_t b = {&v[0], s};
		size_type read = f.readv(0, &b, 1, ec);
		if (read != s) return -3;
		if (ec) return -3;
		return 0;
	}

	int announce_entry::next_announce_in() const
	{ return total_seconds(time_now() - next_announce); }

	int announce_entry::min_announce_in() const
	{ return total_seconds(time_now() - min_announce); }

	bool announce_entry::can_announce(ptime now, bool is_seed) const
	{
		// if we're a seed and we haven't sent a completed
		// event, we need to let this announce through
		bool need_send_complete = is_seed && !complete_sent;

		return now >= next_announce
			&& (now >= min_announce || need_send_complete)
			&& (fails < fail_limit || fail_limit == 0)
			&& !updating;
	}

	torrent_info::torrent_info(torrent_info const& t)
		: m_files(t.m_files)
		, m_orig_files(t.m_orig_files)
		, m_urls(t.m_urls)
		, m_url_seeds(t.m_url_seeds)
		, m_http_seeds(t.m_http_seeds)
		, m_nodes(t.m_nodes)
		, m_info_hash(t.m_info_hash)
		, m_creation_date(t.m_creation_date)
		, m_comment(t.m_comment)
		, m_created_by(t.m_created_by)
		, m_multifile(t.m_multifile)
		, m_private(t.m_private)
		, m_info_section_size(t.m_info_section_size)
		, m_piece_hashes(t.m_piece_hashes)
		, m_merkle_tree(t.m_merkle_tree)
		, m_merkle_first_leaf(t.m_merkle_first_leaf)
	{
		if (m_info_section_size > 0)
		{
			m_info_section.reset(new char[m_info_section_size]);
			memcpy(m_info_section.get(), t.m_info_section.get(), m_info_section_size);
			int ret = lazy_bdecode(m_info_section.get(), m_info_section.get()
				+ m_info_section_size, m_info_dict);

			lazy_entry const* pieces = m_info_dict.dict_find_string("pieces");
			if (pieces && pieces->string_length() == m_files.num_pieces() * 20)
			{
				m_piece_hashes = m_info_section.get() + (pieces->string_ptr() - m_info_section.get());
				TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
				TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
			}
		}
	}

	void torrent_info::remap_files(file_storage const& f)
	{
		// the new specified file storage must have the exact
		// same size as the current file storage
		TORRENT_ASSERT(m_files.total_size() == f.total_size());

		if (m_files.total_size() != f.total_size()) return;
		copy_on_write();
		m_files = f;
	}

#ifndef TORRENT_NO_DEPRECATE
	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
		, m_merkle_first_leaf(0)
	{
		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, torrent_file);

		lazy_entry e;
		if (tmp.size() == 0 || lazy_bdecode(&tmp[0], &tmp[0] + tmp.size(), e) != 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw invalid_torrent_file(errors::invalid_bencoding);
#endif
			return;
		}
		error_code ec;
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, ec))
			throw invalid_torrent_file(ec);
#else
		parse_torrent_file(e, ec);
#endif
	}
#endif

#ifndef BOOST_NO_EXCEPTIONS
	torrent_info::torrent_info(lazy_entry const& torrent_file)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
		, m_merkle_first_leaf(0)
	{
		error_code ec;
		if (!parse_torrent_file(torrent_file, ec))
			throw invalid_torrent_file(ec);
	}

	torrent_info::torrent_info(char const* buffer, int size)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
		, m_merkle_first_leaf(0)
	{
		error_code ec;
		lazy_entry e;
		if (lazy_bdecode(buffer, buffer + size, e) != 0)
			throw invalid_torrent_file(errors::invalid_bencoding);

		if (!parse_torrent_file(e, ec))
			throw invalid_torrent_file(ec);
	}

	torrent_info::torrent_info(fs::path const& filename)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf);
		if (ret < 0) return;

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e) != 0)
			throw invalid_torrent_file(errors::invalid_bencoding);
		error_code ec;
		if (!parse_torrent_file(e, ec))
			throw invalid_torrent_file(ec);
	}

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
	torrent_info::torrent_info(fs::wpath const& filename)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
		, m_merkle_first_leaf(0)
	{
		std::vector<char> buf;
		std::string utf8;
		wchar_utf8(filename.string(), utf8);
		int ret = load_file(utf8, buf);
		if (ret < 0) return;

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e) != 0)
			throw invalid_torrent_file(errors::invalid_bencoding);

		error_code ec;
		if (!parse_torrent_file(e, ec))
			throw invalid_torrent_file(ec);
	}
#endif
#endif

	torrent_info::torrent_info(lazy_entry const& torrent_file, error_code& ec)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		parse_torrent_file(torrent_file, ec);
	}

	torrent_info::torrent_info(char const* buffer, int size, error_code& ec)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
		, m_merkle_first_leaf(0)
	{
		lazy_entry e;
		if (lazy_bdecode(buffer, buffer + size, e) != 0)
		{
			ec = errors::invalid_bencoding;
			return;
		}
		parse_torrent_file(e, ec);
	}

	torrent_info::torrent_info(fs::path const& filename, error_code& ec)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf);
		if (ret < 0) return;

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e) != 0)
		{
			ec = errors::invalid_bencoding;
			return;
		}
		parse_torrent_file(e, ec);
	}

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
	torrent_info::torrent_info(fs::wpath const& filename, error_code& ec)
		: m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_info_section_size(0)
		, m_piece_hashes(0)
	{
		std::vector<char> buf;
		std::string utf8;
		wchar_utf8(filename.string(), utf8);
		int ret = load_file(utf8, buf);
		if (ret < 0) return;

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e) != 0)
		{
			ec = errors::invalid_bencoding;
			return;
		}
		parse_torrent_file(e, ec);
	}
#endif

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
		m_info_dict.swap(ti.m_info_dict);
		swap(m_merkle_tree, ti.m_merkle_tree);
		swap(m_merkle_first_leaf, ti.m_merkle_first_leaf);
	}

	bool torrent_info::parse_info_section(lazy_entry const& info, error_code& ec)
	{
		if (info.type() != lazy_entry::dict_t)
		{
			ec = errors::torrent_info_no_dict;
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
			ec = errors::torrent_missing_piece_length;
			return false;
		}
		m_files.set_piece_length(piece_length);

		// extract file name (or the directory name if it's a multifile libtorrent)
		std::string name = info.dict_find_string_value("name.utf-8");
		if (name.empty()) name = info.dict_find_string_value("name");
		if (name.empty())
		{
			ec = errors::torrent_missing_name;
			return false;
		}

		name = sanitize_path(name).string();
	
		if (!valid_path_element(name))
		{
			ec = errors::torrent_invalid_name;
			return false;
		}

		// correct utf-8 encoding errors
		verify_encoding(name, true);
	
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
			size_type ts = info.dict_find_int_value("mtime", -1);
			if (ts >= 0)
				e.mtime = std::time_t(ts);
			lazy_entry const* attr = info.dict_find_string("attr");
			if (attr)
			{
				for (int i = 0; i < attr->string_length(); ++i)	
				{
					switch (attr->string_ptr()[i])
					{
						case 'l': e.symlink_attribute = true; e.size = 0; break;
						case 'x': e.executable_attribute = true; break;
						case 'h': e.hidden_attribute = true; break;
						case 'p': e.pad_file = true; break;
					}
				}
			}

			lazy_entry const* s_p = info.dict_find("symlink path");
			if (s_p != 0 && s_p->type() == lazy_entry::list_t)
			{
				for (int i = 0, end(s_p->list_size()); i < end; ++i)
				{
					std::string path_element = s_p->list_at(i)->string_value();
					trim_path_element(path_element);
					e.symlink_path /= path_element;
				}
			}
			// bitcomet pad file
			if (e.path.string().find("_____padding_file_") != std::string::npos)
				e.pad_file = true;
			if (e.size < 0)
			{
				ec = errors::torrent_invalid_length;
				return false;
			}
			m_files.add_file(e);
			m_multifile = false;
		}
		else
		{
			if (!extract_files(*i, m_files, name))
			{
				ec = errors::torrent_file_parse_failed;
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
		lazy_entry const* root_hash = info.dict_find("root hash");
		if ((pieces == 0 || pieces->type() != lazy_entry::string_t)
			&& (root_hash == 0 || root_hash->type() != lazy_entry::string_t))
		{
			ec = errors::torrent_missing_pieces;
			return false;
		}
		
		if (pieces)
		{
			if (pieces->string_length() != m_files.num_pieces() * 20)
			{
				ec = errors::torrent_invalid_hashes;
				return false;
			}

			m_piece_hashes = m_info_section.get() + (pieces->string_ptr() - section.first);
			TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
			TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
		}
		else
		{
			TORRENT_ASSERT(root_hash);
			if (root_hash->string_length() != 20)
			{
				ec = errors::torrent_invalid_hashes;
				return false;
			}
			int num_leafs = merkle_num_leafs(m_files.num_pieces());
			int num_nodes = merkle_num_nodes(num_leafs);
			m_merkle_first_leaf = num_nodes - num_leafs;
			m_merkle_tree.resize(num_nodes);
			std::memset(&m_merkle_tree[0], 0, num_nodes * 20);
			m_merkle_tree[0].assign(root_hash->string_ptr());
		}

		m_private = info.dict_find_int_value("private", 0);
		return true;
	}

	bool torrent_info::add_merkle_nodes(std::map<int, sha1_hash> const& subtree
		, int piece)
	{
		int n = m_merkle_first_leaf + piece;
		typedef std::map<int, sha1_hash>::const_iterator iter;
		iter i = subtree.find(n);
		if (i == subtree.end()) return false;
		sha1_hash h = i->second;

		// if the verification passes, these are the
		// nodes to add to our tree
		std::map<int, sha1_hash> to_add;

		while (n > 0)
		{
			int sibling = merkle_get_sibling(n);
			int parent = merkle_get_parent(n);
			iter sibling_hash = subtree.find(sibling);
			if (sibling_hash == subtree.end())
				return false;
			to_add[n] = h;
			to_add[sibling] = sibling_hash->second;
			hasher hs;
			if (sibling < n)
			{
				hs.update((char const*)&sibling_hash->second[0], 20);
				hs.update((char const*)&h[0], 20);
			}
			else
			{
				hs.update((char const*)&h[0], 20);
				hs.update((char const*)&sibling_hash->second[0], 20);
			}
			h = hs.final();
			n = parent;
		}
		if (h != m_merkle_tree[0]) return false;

		// the nodes and piece hash matched the root-hash
		// insert them into our tree

		for (std::map<int, sha1_hash>::iterator i = to_add.begin()
			, end(to_add.end()); i != end; ++i)
		{
			m_merkle_tree[i->first] = i->second;
		}
		return true;
	}

	// builds a list of nodes that are required to verify
	// the given piece
	std::map<int, sha1_hash> torrent_info::build_merkle_list(int piece) const
	{
		std::map<int, sha1_hash> ret;
		int n = m_merkle_first_leaf + piece;
		ret[n] = m_merkle_tree[n];
		ret[0] = m_merkle_tree[0];
		while (n > 0)
		{
			int sibling = merkle_get_sibling(n);
			int parent = merkle_get_parent(n);
			ret[sibling] = m_merkle_tree[sibling];
			// we cannot build the tree path if one
			// of the nodes in the tree is missing
			TORRENT_ASSERT(m_merkle_tree[sibling] != sha1_hash(0));
			n = parent;
		}
		return ret;
	}

	bool torrent_info::parse_torrent_file(lazy_entry const& torrent_file, error_code& ec)
	{
		if (torrent_file.type() != lazy_entry::dict_t)
		{
			ec = errors::torrent_is_no_dict;
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
					e.trim();
					if (e.url.empty()) continue;
					e.tier = j;
					e.fail_limit = 0;
					e.source = announce_entry::source_torrent;
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
			e.fail_limit = 0;
			e.source = announce_entry::source_torrent;
			e.trim();
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
			m_url_seeds.push_back(maybe_url_encode(url_seeds->string_value()));
		}
		else if (url_seeds && url_seeds->type() == lazy_entry::list_t)
		{
			for (int i = 0, end(url_seeds->list_size()); i < end; ++i)
			{
				lazy_entry const* url = url_seeds->list_at(i);
				if (url->type() != lazy_entry::string_t) continue;
				m_url_seeds.push_back(maybe_url_encode(url->string_value()));
			}
		}

		// if there are any http-seeds, extract them
		lazy_entry const* http_seeds = torrent_file.dict_find("httpseeds");
		if (http_seeds && http_seeds->type() == lazy_entry::string_t)
		{
			m_http_seeds.push_back(maybe_url_encode(http_seeds->string_value()));
		}
		else if (http_seeds && http_seeds->type() == lazy_entry::list_t)
		{
			for (int i = 0, end(http_seeds->list_size()); i < end; ++i)
			{
				lazy_entry const* url = http_seeds->list_at(i);
				if (url->type() != lazy_entry::string_t) continue;
				m_http_seeds.push_back(maybe_url_encode(url->string_value()));
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
			ec = errors::torrent_missing_info;
			return false;
		}
		return parse_info_section(*info, ec);
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
		e.source = announce_entry::source_client;
		m_urls.push_back(e);

		std::sort(m_urls.begin(), m_urls.end(), boost::bind(&announce_entry::tier, _1)
			< boost::bind(&announce_entry::tier, _2));
	}

#if !defined TORRENT_NO_DEPRECATE && TORRENT_USE_IOSTREAM
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

