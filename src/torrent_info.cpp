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

#include <boost/bind.hpp>
#include <boost/assert.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/escape_string.hpp" // is_space
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/session_settings.hpp"

#if TORRENT_USE_I2P
#include "libtorrent/parse_url.hpp"
#endif

namespace libtorrent
{
	
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
	TORRENT_EXTRA_EXPORT bool verify_encoding(std::string& target, bool fix_paths = false)
	{
		std::string tmp_path;
		bool valid_encoding = true;
		for (std::string::iterator i = target.begin()
			, end(target.end()); i != end; ++i)
		{
			// valid ascii-character
			if ((*i & 0x80) == 0)
			{
				// replace invalid characters with '_'
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
				tmp_path += "_";
				valid_encoding = false;
				break;
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
				tmp_path += "_";
				valid_encoding = false;
				break;
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
				tmp_path += "_";
				valid_encoding = false;
				break;
			}

			// valid 4-byte utf-8 character
			if ((i[0] & 0xf8) == 0xf0
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

			tmp_path += "_";
			valid_encoding = false;
		}
		// the encoding was not valid utf-8
		// save the original encoding and replace the
		// commonly used path with the correctly
		// encoded string
		if (!valid_encoding) target = tmp_path;
		return valid_encoding;
	}

	// TODO: 1 we might save constructing a std::string if this would take a char const* instead
	bool valid_path_element(std::string const& element)
	{
		if (element.empty()
			|| element == "." || element == ".."
			|| element[0] == '/' || element[0] == '\\'
			|| element[element.size()-1] == ':')
			return false;
		return true;
	}

	void trim_path_element(std::string& element)
	{
		const int max_path_len = TORRENT_MAX_PATH;

		// on windows, the max path is expressed in
		// unicode characters, not bytes
#if defined TORRENT_WINDOWS
		std::wstring path_element;
		utf8_wchar(element, path_element);
		if (path_element.size() <= max_path_len) return;

		// truncate filenames that are too long. But keep extensions!
		std::wstring ext;
		wchar_t const* ext1 = wcsrchr(path_element.c_str(), '.');
		if (ext1 != NULL) ext = ext1;

		if (ext.size() > 15)
		{
			path_element.resize(max_path_len);
		}
		else
		{
			path_element.resize(max_path_len - ext.size());
			path_element += ext;
		}
		// remove trailing spaces and dots. These aren't allowed in filenames on windows
		for (int i = path_element.size() - 1; i >= 0; --i)
		{
			if (path_element[i] != L' ' && path_element[i] != L'.') break;
			path_element[i] = L'_';
		}
		wchar_utf8(path_element, element);
#else
		std::string& path_element = element;
		if (int(path_element.size()) <= max_path_len) return;

		// truncate filenames that are too long. But keep extensions!
		std::string ext = extension(path_element);
		if (ext.size() > 15)
		{
			path_element.resize(max_path_len);
		}
		else
		{
			path_element.resize(max_path_len - ext.size());
			path_element += ext;
		}
#endif
	}

	TORRENT_EXTRA_EXPORT std::string sanitize_path(std::string const& p)
	{
		std::string new_path;
		std::string split = split_path(p);
		for (char const* e = split.c_str(); e != 0; e = next_path_element(e))
		{
			std::string pe = e;
#if !TORRENT_USE_UNC_PATHS && defined TORRENT_WINDOWS
			// if we're not using UNC paths on windows, there
			// are certain filenames we're not allowed to use
			const static char const* reserved_names[] =
			{
				"con", "prn", "aux", "clock$", "nul",
				"com0", "com1", "com2", "com3", "com4",
				"com5", "com6", "com7", "com8", "com9",
				"lpt0", "lpt1", "lpt2", "lpt3", "lpt4",
				"lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
			};
			int num_names = sizeof(reserved_names)/sizeof(reserved_names[0]);

			char const* file_end = strrchr(pe.c_str(), '.');
			std::string name;
			if (file_end) name.assign(pe.c_str(), file_end);
			else name = pe;
			std::transform(name.begin(), name.end(), name.begin(), &to_lower);
			char const** str = std::find(reserved_names, reserved_names + num_names, name);
			if (str != reserved_names + num_names)
			{
				pe += "_";
			}
#endif
			if (!valid_path_element(pe)) continue;
			trim_path_element(pe);
			new_path = combine_path(new_path, pe);
		}
		return new_path;
	}

	bool extract_single_file(lazy_entry const& dict, file_entry& target
		, std::string const& root_dir, lazy_entry const** filehash
		, lazy_entry const** filename, time_t* mtime)
	{
		if (dict.type() != lazy_entry::dict_t) return false;
		lazy_entry const* length = dict.dict_find("length");
		if (length == 0 || length->type() != lazy_entry::int_t)
			return false;
		target.size = length->int_value();
		if (target.size < 0)
			return false;

		size_type ts = dict.dict_find_int_value("mtime", -1);
		if (ts > 0) *mtime = std::time_t(ts);

		// prefer the name.utf-8
		// because if it exists, it is more
		// likely to be correctly encoded

		lazy_entry const* p = dict.dict_find("path.utf-8");
		if (p == 0 || p->type() != lazy_entry::list_t)
			p = dict.dict_find("path");
		if (p == 0 || p->type() != lazy_entry::list_t)
			return false;

		std::string path = root_dir;
		for (int i = 0, end(p->list_size()); i < end; ++i)
		{
			if (p->list_at(i)->type() != lazy_entry::string_t)
				return false;
			std::string path_element = p->list_at(i)->string_value();
			if (!valid_path_element(path_element)) continue;
			if (i == end - 1) *filename = p->list_at(i);
			trim_path_element(path_element);
			path = combine_path(path, path_element);
		}
		path = sanitize_path(path);
		verify_encoding(path, true);

		// bitcomet pad file
		if (path.find("_____padding_file_") != std::string::npos)
			target.pad_file = true;

		target.path = path;

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

		lazy_entry const* fh = dict.dict_find_string("sha1");
		if (fh && fh->string_length() == 20 && filehash)
			*filehash = fh;

		lazy_entry const* s_p = dict.dict_find("symlink path");
		if (s_p != 0 && s_p->type() == lazy_entry::list_t && target.symlink_attribute)
		{
			for (int i = 0, end(s_p->list_size()); i < end; ++i)
			{
				std::string path_element = s_p->list_at(i)->string_value();
				trim_path_element(path_element);
				target.symlink_path = combine_path(target.symlink_path, path_element);
			}
		}

		return true;
	}

	struct string_less_no_case
	{
		bool operator()(std::string const& lhs, std::string const& rhs)
		{
			char c1, c2;
			char const* s1 = lhs.c_str();
			char const* s2 = rhs.c_str();
	
			while (*s1 != 0 || *s2 != 0)
			{
				c1 = to_lower(*s1);
				c2 = to_lower(*s2);
				if (c1 < c2) return true;
				if (c1 > c2) return false;
				++s1;
				++s2;
			}
			return false;
		}
	};

	bool extract_files(lazy_entry const& list, file_storage& target
		, std::string const& root_dir, ptrdiff_t info_ptr_diff)
	{
		if (list.type() != lazy_entry::list_t) return false;
		target.reserve(list.list_size());

		// TODO: 1 this logic should be a separate step
		// done once the torrent is loaded, and the original
		// filenames should be preserved!
		std::set<std::string, string_less_no_case> files;

		for (int i = 0, end(list.list_size()); i < end; ++i)
		{
			lazy_entry const* file_hash = 0;
			time_t mtime = 0;
			file_entry e;
			lazy_entry const* fee = 0;
			if (!extract_single_file(*list.list_at(i), e, root_dir
				, &file_hash, &fee, &mtime))
				return false;

			// as long as this file already exists
			// increase the counter
			int cnt = 0;
			while (!files.insert(e.path).second)
			{
				++cnt;
				char suffix[50];
				snprintf(suffix, sizeof(suffix), "%d%s", cnt, extension(e.path).c_str());
				replace_extension(e.path, suffix);
			}
			target.add_file(e, file_hash ? file_hash->string_ptr() + info_ptr_diff : 0);

			// This is a memory optimization! Instead of having
			// each entry keep a string for its filename, make it
			// simply point into the info-section buffer
			internal_file_entry const& fe = *target.rbegin();
			// TODO: once the filename renaming is removed from here
			// this check can be removed as well
			if (fee && fe.filename() == fee->string_value())
			{
				// this string pointer does not necessarily point into
				// the m_info_section buffer.
				char const* str_ptr = fee->string_ptr() + info_ptr_diff;
				const_cast<internal_file_entry&>(fe).set_name(str_ptr, fee->string_length());
			}
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
		TORRENT_ASSERT(pieces > 0);
		// round up to nearest 2 exponent
		int ret = 1;
		while (pieces > ret) ret <<= 1;
		return ret;
	}

	int load_file(std::string const& filename, std::vector<char>& v, error_code& ec, int limit)
	{
		ec.clear();
		file f;
		if (!f.open(filename, file::read_only, ec)) return -1;
		size_type s = f.get_size(ec);
		if (ec) return -1;
		if (s > limit)
		{
			ec = error_code(errors::metadata_too_large, get_libtorrent_category());
			return -2;
		}
		v.resize(s);
		if (s == 0) return 0;
		file::iovec_t b = {&v[0], s};
		size_type read = f.readv(0, &b, 1, ec);
		if (read != s) return -3;
		if (ec) return -3;
		return 0;
	}

	announce_entry::announce_entry(std::string const& u)
		: url(u)
		, next_announce(min_time())
		, min_announce(min_time())
		, tier(0)
		, fail_limit(0)
		, fails(0)
		, updating(false)
		, source(0)
		, verified(false)
		, start_sent(false)
		, complete_sent(false)
		, send_stats(true)
	{}

	announce_entry::announce_entry()
		: next_announce(min_time())
		, min_announce(min_time())
		, tier(0)
		, fail_limit(0)
		, fails(0)
		, updating(false)
		, source(0)
		, verified(false)
		, start_sent(false)
		, complete_sent(false)
		, send_stats(true)
	{}

	announce_entry::~announce_entry() {}

	int announce_entry::next_announce_in() const
	{ return total_seconds(next_announce - time_now()); }

	int announce_entry::min_announce_in() const
	{ return total_seconds(min_announce - time_now()); }

	void announce_entry::failed(session_settings const& sett, int retry_interval)
	{
		++fails;
		// the exponential back-off ends up being:
		// 7, 15, 27, 45, 95, 127, 165, ... seconds
		// with the default tracker_backoff of 250
		int delay = (std::min)(tracker_retry_delay_min + int(fails) * int(fails)
			* tracker_retry_delay_min * sett.tracker_backoff / 100
			, int(tracker_retry_delay_max));
		delay = (std::max)(delay, retry_interval);
		next_announce = time_now() + seconds(delay);
		updating = false;
	}

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

	void announce_entry::trim()
	{
		while (!url.empty() && is_space(url[0]))
			url.erase(url.begin());
	}

	web_seed_entry::web_seed_entry(std::string const& url_, type_t type_
		, std::string const& auth_
		, headers_t const& extra_headers_)
		: url(url_), type(type_)
		, auth(auth_), extra_headers(extra_headers_)
		, retry(time_now()), resolving(false), removed(false)
		, peer_info(0, true, 0)
	{
		peer_info.web_seed = true;
	}

	torrent_info::torrent_info(torrent_info const& t, int flags)
		: m_merkle_first_leaf(t.m_merkle_first_leaf)
		, m_files(t.m_files)
		, m_orig_files(t.m_orig_files)
		, m_urls(t.m_urls)
		, m_web_seeds(t.m_web_seeds)
		, m_nodes(t.m_nodes)
		, m_merkle_tree(t.m_merkle_tree)
		, m_piece_hashes(t.m_piece_hashes)
		, m_comment(t.m_comment)
		, m_created_by(t.m_created_by)
#ifdef TORRENT_USE_OPENSSL
		, m_ssl_root_cert(t.m_ssl_root_cert)
#endif
		, m_creation_date(t.m_creation_date)
		, m_info_hash(t.m_info_hash)
		, m_info_section_size(t.m_info_section_size)
		, m_multifile(t.m_multifile)
		, m_private(t.m_private)
		, m_i2p(t.m_i2p)
	{
#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		t.check_invariant();
#endif
		if (m_info_section_size > 0)
		{
			error_code ec;
			m_info_section.reset(new char[m_info_section_size]);
			memcpy(m_info_section.get(), t.m_info_section.get(), m_info_section_size);
			int ret = lazy_bdecode(m_info_section.get(), m_info_section.get()
				+ m_info_section_size, m_info_dict, ec);
			TORRENT_ASSERT(ret == 0);

			ptrdiff_t offset = m_info_section.get() - t.m_info_section.get();

			m_piece_hashes += offset;
			TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
			TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
		}
		INVARIANT_CHECK;
	}

	void torrent_info::remap_files(file_storage const& f)
	{
		INVARIANT_CHECK;

		// the new specified file storage must have the exact
		// same size as the current file storage
		TORRENT_ASSERT(m_files.total_size() == f.total_size());

		if (m_files.total_size() != f.total_size()) return;
		copy_on_write();
		m_files = f;
		m_files.set_num_pieces(m_orig_files->num_pieces());
		m_files.set_piece_length(m_orig_files->piece_length());
	}

#ifndef TORRENT_NO_DEPRECATE
	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, torrent_file);

		lazy_entry e;
		error_code ec;
		if (tmp.size() == 0 || lazy_bdecode(&tmp[0], &tmp[0] + tmp.size(), e, ec) != 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw invalid_torrent_file(errors::invalid_bencoding);
#endif
			return;
		}
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, ec, 0))
			throw invalid_torrent_file(ec);
#else
		parse_torrent_file(e, ec, 0);
#endif
		INVARIANT_CHECK;
	}
#endif

#ifndef BOOST_NO_EXCEPTIONS
	torrent_info::torrent_info(lazy_entry const& torrent_file, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		error_code ec;
		if (!parse_torrent_file(torrent_file, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(char const* buffer, int size, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		error_code ec;
		lazy_entry e;
		if (lazy_bdecode(buffer, buffer + size, e, ec) != 0)
			throw invalid_torrent_file(ec);

		if (!parse_torrent_file(e, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> buf;
		error_code ec;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) throw invalid_torrent_file(ec);

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			throw invalid_torrent_file(ec);

		if (!parse_torrent_file(e, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}

#if TORRENT_USE_WSTRING
	torrent_info::torrent_info(std::wstring const& filename, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> buf;
		std::string utf8;
		wchar_utf8(filename, utf8);
		error_code ec;
		int ret = load_file(utf8, buf, ec);
		if (ret < 0) throw invalid_torrent_file(ec);

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			throw invalid_torrent_file(ec);

		if (!parse_torrent_file(e, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}
#endif
#endif

	torrent_info::torrent_info(lazy_entry const& torrent_file, error_code& ec, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		parse_torrent_file(torrent_file, ec, flags);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(char const* buffer, int size, error_code& ec, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		lazy_entry e;
		if (lazy_bdecode(buffer, buffer + size, e, ec) != 0)
			return;
		parse_torrent_file(e, ec, flags);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, error_code& ec, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) return;

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			return;
		parse_torrent_file(e, ec, flags);

		INVARIANT_CHECK;
	}

#if TORRENT_USE_WSTRING
	torrent_info::torrent_info(std::wstring const& filename, error_code& ec, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> buf;
		std::string utf8;
		wchar_utf8(filename, utf8);
		int ret = load_file(utf8, buf, ec);
		if (ret < 0) return;

		lazy_entry e;
		if (buf.size() == 0 || lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			return;
		parse_torrent_file(e, ec, flags);

		INVARIANT_CHECK;
	}
#endif

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash, int flags)
		: m_merkle_first_leaf(0)
		, m_piece_hashes(0)
		, m_creation_date(time(0))
		, m_info_hash(info_hash)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{}

	torrent_info::~torrent_info()
	{}

	void torrent_info::copy_on_write()
	{
		INVARIANT_CHECK;

		if (m_orig_files) return;
		m_orig_files.reset(new file_storage(m_files));
	}

#define SWAP(a, b) \
	  	tmp = a; \
		a = b; \
		b = tmp;

	void torrent_info::swap(torrent_info& ti)
	{
		INVARIANT_CHECK;

		using std::swap;
		m_urls.swap(ti.m_urls);
		m_web_seeds.swap(ti.m_web_seeds);
		m_files.swap(ti.m_files);
		m_orig_files.swap(ti.m_orig_files);
		m_nodes.swap(ti.m_nodes);
		swap(m_info_hash, ti.m_info_hash);
		swap(m_creation_date, ti.m_creation_date);
		m_comment.swap(ti.m_comment);
		m_created_by.swap(ti.m_created_by);
#ifdef TORRENT_USE_OPENSSL
		m_ssl_root_cert.swap(ti.m_ssl_root_cert);
#endif
		boost::uint32_t tmp;
		SWAP(m_multifile, ti.m_multifile);
		SWAP(m_private, ti.m_private);
		SWAP(m_i2p, ti.m_i2p);
		swap(m_info_section, ti.m_info_section);
		SWAP(m_info_section_size, ti.m_info_section_size);
		swap(m_piece_hashes, ti.m_piece_hashes);
		m_info_dict.swap(ti.m_info_dict);
		swap(m_merkle_tree, ti.m_merkle_tree);
		SWAP(m_merkle_first_leaf, ti.m_merkle_first_leaf);
	}

#undef SWAP

	bool torrent_info::parse_info_section(lazy_entry const& info, error_code& ec, int flags)
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

		// when translating a pointer that points into the 'info' tree's
		// backing buffer, into a pointer to our copy of the info section,
		// this is the pointer offset to use.
		ptrdiff_t info_ptr_diff = m_info_section.get() - section.first;

		// extract piece length
		int piece_length = info.dict_find_int_value("piece length", -1);
		if (piece_length <= 0)
		{
			ec = errors::torrent_missing_piece_length;
			return false;
		}
		m_files.set_piece_length(piece_length);

		// extract file name (or the directory name if it's a multifile libtorrent)
		lazy_entry const* name_ent = info.dict_find_string("name.utf-8");
		if (name_ent == 0) name_ent = info.dict_find_string("name");
		if (name_ent == 0)
		{
			ec = errors::torrent_missing_name;
			return false;
		}

		std::string name = name_ent->string_value();
		if (name.empty()) name = to_hex(m_info_hash.to_string());
		name = sanitize_path(name);
	
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
			if (e.size < 0)
			{
				ec = errors::torrent_file_parse_failed;
				return false;
			}
			e.mtime = info.dict_find_int_value("mtime", 0);
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
					e.symlink_path = combine_path(e.symlink_path, path_element);
				}
			}
			lazy_entry const* fh = info.dict_find_string("sha1");
			if (fh && fh->string_length() != 20) fh = 0;

			// bitcomet pad file
			if (e.path.find("_____padding_file_") != std::string::npos)
				e.pad_file = true;
			if (e.size < 0)
			{
				ec = errors::torrent_invalid_length;
				return false;
			}
			m_files.add_file(e, fh ? fh->string_ptr() + info_ptr_diff : 0);
			m_multifile = false;
		}
		else
		{
			if (!extract_files(*i, m_files, name, info_ptr_diff))
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

			m_piece_hashes = pieces->string_ptr() + info_ptr_diff;
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

#ifdef TORRENT_USE_OPENSSL
		m_ssl_root_cert = info.dict_find_string_value("ssl-cert");
#endif

		return true;
	}

	bool torrent_info::add_merkle_nodes(std::map<int, sha1_hash> const& subtree
		, int piece)
	{
		INVARIANT_CHECK;

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
		INVARIANT_CHECK;

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

#if TORRENT_USE_I2P
	bool is_i2p_url(std::string const& url)
	{
		using boost::tuples::ignore;
		std::string hostname;
		error_code ec;
		boost::tie(ignore, ignore, hostname, ignore, ignore)
			= parse_url_components(url, ec);
		char const* top_domain = strrchr(hostname.c_str(), '.');
		return top_domain && strcmp(top_domain, ".i2p") == 0;
	}
#endif

	bool torrent_info::parse_torrent_file(lazy_entry const& torrent_file, error_code& ec, int flags)
	{
		if (torrent_file.type() != lazy_entry::dict_t)
		{
			ec = errors::torrent_is_no_dict;
			return false;
		}

		lazy_entry const* info = torrent_file.dict_find_dict("info");
		if (info == 0)
		{
			ec = errors::torrent_missing_info;
			return false;
		}
		if (!parse_info_section(*info, ec, flags)) return false;

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
#if TORRENT_USE_I2P
					if (is_i2p_url(e.url)) m_i2p = true;
#endif
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
#if TORRENT_USE_I2P
			if (is_i2p_url(e.url)) m_i2p = true;
#endif
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
			m_creation_date = long(cd);
		}

		// if there are any url-seeds, extract them
		lazy_entry const* url_seeds = torrent_file.dict_find("url-list");
		if (url_seeds && url_seeds->type() == lazy_entry::string_t && url_seeds->string_length() > 0)
		{
			web_seed_entry ent(maybe_url_encode(url_seeds->string_value())
				, web_seed_entry::url_seed);
			if (m_multifile && ent.url[ent.url.size()-1] != '/') ent.url += '/';
			m_web_seeds.push_back(ent);
		}
		else if (url_seeds && url_seeds->type() == lazy_entry::list_t)
		{
			for (int i = 0, end(url_seeds->list_size()); i < end; ++i)
			{
				lazy_entry const* url = url_seeds->list_at(i);
				if (url->type() != lazy_entry::string_t) continue;
				if (url->string_length() == 0) continue;
				web_seed_entry ent(maybe_url_encode(url->string_value())
					, web_seed_entry::url_seed);
				if (m_multifile && ent.url[ent.url.size()-1] != '/') ent.url += '/';
				m_web_seeds.push_back(ent);
			}
		}

		// if there are any http-seeds, extract them
		lazy_entry const* http_seeds = torrent_file.dict_find("httpseeds");
		if (http_seeds && http_seeds->type() == lazy_entry::string_t && http_seeds->string_length() > 0)
		{
			m_web_seeds.push_back(web_seed_entry(maybe_url_encode(http_seeds->string_value())
				, web_seed_entry::http_seed));
		}
		else if (http_seeds && http_seeds->type() == lazy_entry::list_t)
		{
			for (int i = 0, end(http_seeds->list_size()); i < end; ++i)
			{
				lazy_entry const* url = http_seeds->list_at(i);
				if (url->type() != lazy_entry::string_t || url->string_length() == 0) continue;
				m_web_seeds.push_back(web_seed_entry(maybe_url_encode(url->string_value())
					, web_seed_entry::http_seed));
			}
		}

		m_comment = torrent_file.dict_find_string_value("comment.utf-8");
		if (m_comment.empty()) m_comment = torrent_file.dict_find_string_value("comment");
		verify_encoding(m_comment);
	
		m_created_by = torrent_file.dict_find_string_value("created by.utf-8");
		if (m_created_by.empty()) m_created_by = torrent_file.dict_find_string_value("created by");
		verify_encoding(m_created_by);

		return true;
	}

	boost::optional<time_t>
	torrent_info::creation_date() const
	{
		if (m_creation_date != 0)
		{
			return boost::optional<time_t>(m_creation_date);
		}
		return boost::optional<time_t>();
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

#ifndef TORRENT_NO_DEPRECATE
	namespace
	{
		struct filter_web_seed_type
		{
			filter_web_seed_type(web_seed_entry::type_t t_) : t(t_) {}
			void operator() (web_seed_entry const& w)
			{ if (w.type == t) urls.push_back(w.url); }
			std::vector<std::string> urls;
			web_seed_entry::type_t t;
		};
	}

	std::vector<std::string> torrent_info::url_seeds() const
	{
		return std::for_each(m_web_seeds.begin(), m_web_seeds.end()
			, filter_web_seed_type(web_seed_entry::url_seed)).urls;
	}

	std::vector<std::string> torrent_info::http_seeds() const
	{
		return std::for_each(m_web_seeds.begin(), m_web_seeds.end()
			, filter_web_seed_type(web_seed_entry::http_seed)).urls;
	}

#endif // TORRENT_NO_DEPRECATE

	void torrent_info::add_url_seed(std::string const& url
			, std::string const& ext_auth
			, web_seed_entry::headers_t const& ext_headers)
	{
		m_web_seeds.push_back(web_seed_entry(url, web_seed_entry::url_seed
			, ext_auth, ext_headers));
	}

	void torrent_info::add_http_seed(std::string const& url
			, std::string const& auth
			, web_seed_entry::headers_t const& extra_headers)
	{
		m_web_seeds.push_back(web_seed_entry(url, web_seed_entry::http_seed
			, auth, extra_headers));
	}


#if !defined TORRENT_NO_DEPRECATE && TORRENT_USE_IOSTREAM
// ------- start deprecation -------

	void torrent_info::print(std::ostream& os) const
	{
		INVARIANT_CHECK;

		os << "trackers:\n";
		for (std::vector<announce_entry>::const_iterator i = trackers().begin();
			i != trackers().end(); ++i)
		{
			os << i->tier << ": " << i->url << "\n";
		}
		if (!m_comment.empty())
			os << "comment: " << m_comment << "\n";
		os << "private: " << (m_private?"yes":"no") << "\n";
		os << "number of pieces: " << num_pieces() << "\n";
		os << "piece length: " << piece_length() << "\n";
		os << "files:\n";
		for (file_storage::iterator i = m_files.begin(); i != m_files.end(); ++i)
			os << "  " << std::setw(11) << i->size << "  " << m_files.file_path(*i) << "\n";
	}

// ------- end deprecation -------
#endif

#ifdef TORRENT_DEBUG
	void torrent_info::check_invariant() const
	{
		for (file_storage::iterator i = m_files.begin()
			, end(m_files.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->name != 0);
			if (i->name_len > 0)
			{
				// name needs to point into the allocated info section buffer
				TORRENT_ASSERT(i->name >= m_info_section.get());
				TORRENT_ASSERT(i->name < m_info_section.get() + m_info_section_size);
			}
			else
			{
				// name must be a valid string
				TORRENT_ASSERT(strlen(i->name) < 2048);
			}
		}

		if (m_piece_hashes != 0)
		{
			TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
			TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
		}
	}
#endif

}

