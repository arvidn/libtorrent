/*

Copyright (c) 2003-2014, Arvid Norberg
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

#if !defined TORRENT_NO_DEPRECATE && TORRENT_USE_IOSTREAM
#include <iostream>
#include <iomanip>
#endif

#include <iterator>
#include <algorithm>
#include "libtorrent/config.hpp"
#include "libtorrent/ConvertUTF.h"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/string_util.hpp" // is_space
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/escape_string.hpp" // maybe_url_encode
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/magnet_uri.hpp"

#ifndef TORRENT_NO_DEPRECATE
#include "libtorrent/lazy_entry.hpp"
#endif

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>
#include <boost/assert.hpp>
#include <boost/unordered_set.hpp>

#include <set>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
		if (target.empty()) return true;

		std::string tmp_path;
		tmp_path.reserve(target.size()+5);
		bool valid_encoding = true;

		UTF8 const* ptr = (UTF8 const*)&target[0];
		UTF8 const* end = (UTF8 const*)&target[0] + target.size();
		while (ptr < end)
		{
			UTF32 codepoint;
			UTF32* cp = &codepoint;
			
			// decode a single utf-8 character
			ConversionResult res = ConvertUTF8toUTF32(&ptr, end, &cp, cp + 1
				, lenientConversion);

			// this was the last character, and nothing was
			// written to the destination buffer (i.e. the source character was
			// truncated)
			if (res == sourceExhausted
				|| res == sourceIllegal)
			{
				if (cp == &codepoint)
				{
					if (res == sourceExhausted)
						ptr = end;
					else
						++ptr;

					codepoint = '_';
					valid_encoding = false;
				}
			}
			else if ((res != conversionOK && res != targetExhausted)
				|| codepoint == UNI_REPLACEMENT_CHAR)
			{
				// we expect the conversion to fail with targetExhausted, since we
				// only pass in a single destination character slot. The last
				// character will succeed though. Also, if the character was replaced,
				// use our own replacement symbol (underscore).
				codepoint = '_';
				valid_encoding = false;
			}

			// if fix paths is true, also replace characters that are invalid
			// in filenames
			if (fix_paths && codepoint < 0x7f && !valid_path_character(codepoint))
			{
				codepoint = '_';
				valid_encoding = false;
			}

			// encode codepoint into utf-8
			cp = &codepoint;
			UTF8 sequence[5];
			UTF8* start = sequence;
			res = ConvertUTF32toUTF8((const UTF32**)&cp, cp + 1, &start, start + 5, lenientConversion);
			TORRENT_ASSERT(res == conversionOK);

			for (int i = 0; i < start - sequence; ++i)
				tmp_path += (char)sequence[i];
		}

		// the encoding was not valid utf-8
		// save the original encoding and replace the
		// commonly used path with the correctly
		// encoded string
		if (!valid_encoding) target = tmp_path;
		return valid_encoding;
	}

	void sanitize_append_path_element(std::string& path, char const* element, int element_len)
	{
		if (element_len == 1 && element[0] == '.') return;

#ifdef TORRENT_WINDOWS
#define TORRENT_SEPARATOR "\\"
#else
#define TORRENT_SEPARATOR "/"
#endif
		path.reserve(path.size() + element_len + 2);
		int added_separator = 0;
		if (!path.empty())
		{
			path += TORRENT_SEPARATOR;
			added_separator = 1;
		}

		if (element_len == 0)
		{
			path += "_";
			return;
		}

#if !TORRENT_USE_UNC_PATHS && defined TORRENT_WINDOWS
		// if we're not using UNC paths on windows, there
		// are certain filenames we're not allowed to use
		static const char const* reserved_names[] =
		{
			"con", "prn", "aux", "clock$", "nul",
			"com0", "com1", "com2", "com3", "com4",
			"com5", "com6", "com7", "com8", "com9",
			"lpt0", "lpt1", "lpt2", "lpt3", "lpt4",
			"lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
		};
		int num_names = sizeof(reserved_names)/sizeof(reserved_names[0]);

		// this is not very efficient, but it only affects some specific
		// windows builds for now anyway (not even the default windows build)
		std::string pe(element, element_len);
		char const* file_end = strrchr(pe.c_str(), '.');
		std::string name;
		if (file_end) name.assign(pe.c_str(), file_end);
		else name = pe;
		std::transform(name.begin(), name.end(), name.begin(), &to_lower);
		char const* str = std::find(reserved_names, reserved_names + num_names, name);
		if (str != reserved + num_names)
		{
			pe = "_" + pe;
			element = pe.c_str();
			element_len = pe.size();
		}
#endif
		// this counts the number of unicode characters
		// we've added (which is different from the number
		// of bytes)
		int unicode_chars = 0;

		int added = 0;
		// the number of dots we've added
		char num_dots = 0;
		bool found_extension = false;
		for (int i = 0; i < element_len; ++i)
		{
			if (element[i] == '/'
				|| element[i] == '\\'
#ifdef TORRENT_WINDOWS
				|| element[i] == ':'
#endif
				)
				continue;

			if (element[i] == '.') ++num_dots;
		
			int last_len = 0;

			if ((element[i] & 0x80) == 0)
			{
				// 1 byte
				path += element[i];
				last_len = 1;
			}
			else if ((element[i] & 0xe0) == 0xc0)
			{
				// 2 bytes
				if (element_len - i < 2
					|| (element[i+1] & 0xc0) != 0x80)
				{
					path += '?';
					last_len = 1;
				}
				else
				{
					path += element[i];
					path += element[i+1];
					last_len = 2;
				}
				i += 1;
			}
			else if ((element[i] & 0xf0) == 0xe0)
			{
				// 3 bytes
				if (element_len - i < 3
					|| (element[i+1] & 0xc0) != 0x80
					|| (element[i+2] & 0xc0) != 0x80
					)
				{
					path += '?';
					last_len = 1;
				}
				else
				{
					path += element[i];
					path += element[i+1];
					path += element[i+2];
					last_len = 3;
				}
				i += 2;
			}
			else if ((element[i] & 0xf8) == 0xf0)
			{
				// 4 bytes
				if (element_len - i < 4
					|| (element[i+1] & 0xc0) != 0x80
					|| (element[i+2] & 0xc0) != 0x80
					|| (element[i+3] & 0xc0) != 0x80
					)
				{
					path += '?';
					last_len = 1;
				}
				else
				{
					path += element[i];
					path += element[i+1];
					path += element[i+2];
					path += element[i+3];
					last_len = 4;
				}
				i += 3;
			}

			added += last_len;
			++unicode_chars;

			// any given path element should not
			// be more than 255 characters
			// if we exceed 240, pick up any potential
			// file extension and add that too
#ifdef TORRENT_WINDOWS
			if (unicode_chars >= 240 && !found_extension)
#else
			if (added >= 240 && !found_extension)
#endif
			{
				int dot = -1;
				for (int j = element_len-1; j > (std::max)(element_len - 10, i); --j)
				{
					if (element[j] != '.') continue;
					dot = j;
					break;
				}
				// there is no extension
				if (dot == -1) break;
				found_extension = true;
				i = dot - 1;
			}
		}

		if (added == num_dots && added <= 2)
		{
			// revert everything
			path.erase(path.end()-added-added_separator, path.end());
			return;
		}

		if (added == 0 && added_separator)
		{
			// remove the separator added at the beginning
			path.erase(path.end()-1);
			return;
		}

		// remove trailing spaces and dots. These aren't allowed in filenames on windows
		for (int i = path.size() - 1; i >= 0; --i)
		{
			if (path[i] != ' ' && path[i] != '.') break;
			path.resize(i);
		}

		if (path.empty()) path = "_";
	}

	// 'top_level' is extracting the file for a single-file torrent. The
	// distinction is that the filename is found in "name" rather than
	// "path"
	// root_dir is the name of the torrent, unless this is a single file
	// torrent, in which case it's empty.
	bool extract_single_file(bdecode_node const& dict, file_storage& files
		, std::string const& root_dir, ptrdiff_t info_ptr_diff, bool top_level
		, error_code& ec)
	{
		if (dict.type() != bdecode_node::dict_t) return false;
		boost::int64_t file_size = dict.dict_find_int_value("length", -1);
		if (file_size < 0)
		{
			ec = errors::torrent_invalid_length;
			return false;
		}

		boost::int64_t mtime = dict.dict_find_int_value("mtime", 0);

		std::string path = root_dir;
		std::string path_element;
		char const* filename = NULL;
		int filename_len = 0;

		if (top_level)
		{
			// prefer the name.utf-8 because if it exists, it is more likely to be
			// correctly encoded
			bdecode_node p = dict.dict_find_string("name.utf-8");
			if (!p) p = dict.dict_find_string("name");
			if (!p || p.string_length() == 0)
			{
				ec = errors::torrent_missing_name;
				return false;
			}

			filename = p.string_ptr() + info_ptr_diff;
			filename_len = p.string_length();
			sanitize_append_path_element(path, p.string_ptr(), p.string_length());

//			if (path.empty()) path = to_hex(files.info_hash().to_string());
		}
		else
		{
			bdecode_node p = dict.dict_find_list("path.utf-8");
			if (!p) p = dict.dict_find_list("path");
			if (!p || p.list_size() == 0)
			{
				ec = errors::torrent_missing_name;
				return false;
			}

			int preallocate = path.size();
			for (int i = 0, end(p.list_size()); i < end; ++i)
			{
				bdecode_node e = p.list_at(i);
				if (e.type() != bdecode_node::string_t)
				{
					ec = errors::torrent_missing_name;
					return false;
				}
				preallocate += e.string_length() + 1;
			}
			path.reserve(preallocate);

			for (int i = 0, end(p.list_size()); i < end; ++i)
			{
				bdecode_node e = p.list_at(i);
				if (i == end - 1)
				{
					filename = e.string_ptr() + info_ptr_diff;
					filename_len = e.string_length();
				}
				sanitize_append_path_element(path, e.string_ptr(), e.string_length());
			}
		}

		// bitcomet pad file
		boost::uint32_t file_flags = 0;
		if (path.find("_____padding_file_") != std::string::npos)
			file_flags = file_storage::flag_pad_file;

		bdecode_node attr = dict.dict_find_string("attr");
		if (attr)
		{
			for (int i = 0; i < attr.string_length(); ++i)	
			{
				switch (attr.string_ptr()[i])
				{
					case 'l': file_flags |= file_storage::flag_symlink; file_size = 0; break;
					case 'x': file_flags |= file_storage::flag_executable; break;
					case 'h': file_flags |= file_storage::flag_hidden; break;
					case 'p': file_flags |= file_storage::flag_pad_file; break;
				}
			}
		}

		bdecode_node fh = dict.dict_find_string("sha1");
		char const* filehash = NULL;
		if (fh && fh.string_length() == 20)
			filehash = fh.string_ptr() + info_ptr_diff;

		std::string symlink_path;
		bdecode_node s_p = dict.dict_find("symlink path");
		if (s_p && s_p.type() == bdecode_node::list_t
			&& (file_flags & file_storage::flag_symlink))
		{
			for (int i = 0, end(s_p.list_size()); i < end; ++i)
			{
				std::string path_element = s_p.list_at(i).string_value();
				symlink_path = combine_path(symlink_path, path_element);
			}
		}
		else
		{
			file_flags &= ~file_storage::flag_symlink;
		}

		if (filename_len > path.length()
			|| path.compare(path.size() - filename_len, filename_len, filename
				, filename_len) != 0)
		{
			// if the filename was sanitized and differ, clear it to just use path
			filename = NULL;
			filename_len = 0;
		}

		files.add_file_borrow(filename, filename_len, path, file_size, file_flags, filehash
			, mtime, symlink_path);
		return true;
	}

#if TORRENT_HAS_BOOST_UNORDERED
	struct string_hash_no_case
	{
		size_t operator()(std::string const& s) const
		{
			char const* s1 = s.c_str();
			size_t ret = 5381;
			int c;

			while ((c = *s1++))
				ret = (ret * 33) ^ to_lower(c);

			return ret;
		}
	};

	struct string_eq_no_case
	{
		bool operator()(std::string const& lhs, std::string const& rhs) const
		{
			char c1, c2;
			char const* s1 = lhs.c_str();
			char const* s2 = rhs.c_str();
	
			while (*s1 != 0 && *s2 != 0)
			{
				c1 = to_lower(*s1);
				c2 = to_lower(*s2);
				if (c1 != c2) return false;
				++s1;
				++s2;
			}
			return *s1 == *s2;
		}
	};

#else
	struct string_less_no_case
	{
		bool operator()(std::string const& lhs, std::string const& rhs) const
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
#endif

	// root_dir is the name of the torrent, unless this is a single file
	// torrent, in which case it's empty.
	bool extract_files(bdecode_node const& list, file_storage& target
		, std::string const& root_dir, ptrdiff_t info_ptr_diff, error_code& ec)
	{
		if (list.type() != bdecode_node::list_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}
		target.reserve(list.list_size());

		for (int i = 0, end(list.list_size()); i < end; ++i)
		{
			if (!extract_single_file(list.list_at(i), target, root_dir
				, info_ptr_diff, false, ec))
				return false;
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

	int load_file(std::string const& filename, std::vector<char>& v, error_code& ec, int limit = 8000000)
	{
		ec.clear();
		file f;
		if (!f.open(filename, file::read_only, ec)) return -1;
		boost::int64_t s = f.get_size(ec);
		if (ec) return -1;
		if (s > limit)
		{
			ec = errors::metadata_too_large;
			return -2;
		}
		v.resize((unsigned int)s);
		if (s == 0) return 0;
		file::iovec_t b = {&v[0], size_t(s) };
		boost::int64_t read = f.readv(0, &b, 1, ec);
		if (read != s) return -3;
		if (ec) return -3;
		return 0;
	}

	announce_entry::announce_entry(std::string const& u)
		: url(u)
		, next_announce(min_time())
		, min_announce(min_time())
		, scrape_incomplete(-1)
		, scrape_complete(-1)
		, scrape_downloaded(-1)
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
		, scrape_incomplete(-1)
		, scrape_complete(-1)
		, scrape_downloaded(-1)
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
	{ return total_seconds(next_announce - aux::time_now()); }

	int announce_entry::min_announce_in() const
	{ return total_seconds(min_announce - aux::time_now()); }

	void announce_entry::reset()
	{
		start_sent = false;
		next_announce = min_time();
		min_announce = min_time();
	}

	void announce_entry::failed(aux::session_settings const& sett, int retry_interval)
	{
		++fails;
		// the exponential back-off ends up being:
		// 7, 15, 27, 45, 95, 127, 165, ... seconds
		// with the default tracker_backoff of 250
		int delay = (std::min)(tracker_retry_delay_min + int(fails) * int(fails)
			* tracker_retry_delay_min * sett.get_int(settings_pack::tracker_backoff) / 100
			, int(tracker_retry_delay_max));
		delay = (std::max)(delay, retry_interval);
		next_announce = aux::time_now() + seconds(delay);
		updating = false;
	}

	bool announce_entry::can_announce(time_point now, bool is_seed) const
	{
		// if we're a seed and we haven't sent a completed
		// event, we need to let this announce through
		bool need_send_complete = is_seed && !complete_sent;

		return now > next_announce
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
		: url(url_)
		, auth(auth_)
		, extra_headers(extra_headers_)
		, type(type_)
	{
	}

	torrent_info::torrent_info(torrent_info const& t)
		: m_files(t.m_files)
		, m_orig_files(t.m_orig_files)
		, m_urls(t.m_urls)
		, m_web_seeds(t.m_web_seeds)
		, m_nodes(t.m_nodes)
		, m_merkle_tree(t.m_merkle_tree)
		, m_piece_hashes(t.m_piece_hashes)
		, m_comment(t.m_comment)
		, m_created_by(t.m_created_by)
		, m_creation_date(t.m_creation_date)
		, m_info_hash(t.m_info_hash)
		, m_merkle_first_leaf(t.m_merkle_first_leaf)
		, m_info_section_size(t.m_info_section_size)
		, m_multifile(t.m_multifile)
		, m_private(t.m_private)
		, m_i2p(t.m_i2p)
	{
#if TORRENT_USE_INVARIANT_CHECKS
		t.check_invariant();
#endif
		if (m_info_section_size == 0) return;
		TORRENT_ASSERT(m_piece_hashes);

		error_code ec;
		m_info_section.reset(new char[m_info_section_size]);
		memcpy(m_info_section.get(), t.m_info_section.get(), m_info_section_size);

		ptrdiff_t offset = m_info_section.get() - t.m_info_section.get();

		m_files.apply_pointer_offset(offset);
		if (m_orig_files)
			const_cast<file_storage&>(*m_orig_files).apply_pointer_offset(offset);

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		for (int i = 0; i < m_collections.size(); ++i)
			m_collections[i].first += offset;

		for (int i = 0; i < m_similar_torrents.size(); ++i)
			m_similar_torrents[i] += offset;
#endif

		if (m_info_dict)
		{
			// make this decoded object point to our copy of the info section
			// buffer
			m_info_dict.switch_underlying_buffer(m_info_section.get());
		}

		m_piece_hashes += offset;
		TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
		TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
	}

	void torrent_info::resolve_duplicate_filenames()
	{
		INVARIANT_CHECK;

		boost::unordered_set<boost::uint32_t> files;

		std::string empty_str;

		// insert all directories first, to make sure no files
		// are allowed to collied with them
		m_files.all_path_hashes(files);
		for (int i = 0; i < m_files.num_files(); ++i)
		{
			// as long as this file already exists
			// increase the counter
			boost::uint32_t h = m_files.file_path_hash(i, empty_str);
			if (!files.insert(h).second)
			{
				// This filename appears to already exist!
				// If this happens, just start over and do it the slow way,
				// comparing full file names and come up with new names
				resolve_duplicate_filenames_slow();
				return;
			}
		}
	}

	void torrent_info::resolve_duplicate_filenames_slow()
	{
		INVARIANT_CHECK;
		int cnt = 0;

#if TORRENT_HAS_BOOST_UNORDERED
		boost::unordered_set<std::string, string_hash_no_case, string_eq_no_case> files;
#else
		std::set<std::string, string_less_no_case> files;
#endif

		std::vector<std::string> const& paths = m_files.paths();
		files.reserve(paths.size() + m_files.num_files());

		// insert all directories first, to make sure no files
		// are allowed to collied with them
		for (std::vector<std::string>::const_iterator i = paths.begin()
			, end(paths.end()); i != end; ++i)
		{
			std::string p = combine_path(m_files.name(), *i);
			files.insert(p);
			while (has_parent_path(p))
			{
				p = parent_path(p);
				// we don't want trailing slashes here
				TORRENT_ASSERT(p.back() == *TORRENT_SEPARATOR);
				p.pop_back();
				files.insert(p);
			}
		}

		for (int i = 0; i < m_files.num_files(); ++i)
		{
			// as long as this file already exists
			// increase the counter
			std::string filename = m_files.file_path(i);
			if (!files.insert(filename).second)
			{
				std::string base = remove_extension(filename);
				std::string ext = extension(filename);
				do
				{
					++cnt;
					char new_ext[50];
					snprintf(new_ext, sizeof(new_ext), ".%d%s", cnt, ext.c_str());
					filename = base + new_ext;
				}
				while (!files.insert(filename).second);

				copy_on_write();
				m_files.rename_file(i, filename);
			}
			cnt = 0;
		}
	}

	void torrent_info::remap_files(file_storage const& f)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_loaded());
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
	torrent_info::torrent_info(lazy_entry const& torrent_file, error_code& ec
		, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::pair<char const*, int> buf = torrent_file.data_section();
		bdecode_node e;
		if (bdecode(buf.first, buf.first + buf.second, e, ec) != 0)
			return;
		parse_torrent_file(e, ec, 0);
	}

	torrent_info::torrent_info(lazy_entry const& torrent_file, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::pair<char const*, int> buf = torrent_file.data_section();
		bdecode_node e;
		error_code ec;
		if (bdecode(buf.first, buf.first + buf.second, e, ec) != 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw invalid_torrent_file(ec);
#endif
			return;
		}
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, ec, 0))
			throw invalid_torrent_file(ec);
#else
		parse_torrent_file(e, ec, 0);
#endif
	}

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, torrent_file);

		bdecode_node e;
		error_code ec;
		if (tmp.size() == 0 || bdecode(&tmp[0], &tmp[0] + tmp.size(), e, ec) != 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw invalid_torrent_file(ec);
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
	torrent_info::torrent_info(bdecode_node const& torrent_file, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
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
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		error_code ec;
		bdecode_node e;
		if (bdecode(buffer, buffer + size, e, ec) != 0)
			throw invalid_torrent_file(ec);

		if (!parse_torrent_file(e, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> buf;
		error_code ec;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) throw invalid_torrent_file(ec);

		bdecode_node e;
		if (buf.size() == 0 || bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			throw invalid_torrent_file(ec);

		if (!parse_torrent_file(e, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE
	torrent_info::torrent_info(std::wstring const& filename, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
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

		bdecode_node e;
		if (buf.size() == 0 || bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			throw invalid_torrent_file(ec);

		if (!parse_torrent_file(e, ec, flags))
			throw invalid_torrent_file(ec);

		INVARIANT_CHECK;
	}

	void torrent_info::rename_file(int index, std::wstring const& new_filename)
	{
		TORRENT_ASSERT(is_loaded());
		copy_on_write();
		m_files.rename_file_deprecated(index, new_filename);
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING
#endif

	torrent_info::torrent_info(bdecode_node const& torrent_file, error_code& ec, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		parse_torrent_file(torrent_file, ec, flags);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(char const* buffer, int size, error_code& ec, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		bdecode_node e;
		if (bdecode(buffer, buffer + size, e, ec) != 0)
			return;
		parse_torrent_file(e, ec, flags);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, error_code& ec, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) return;

		bdecode_node e;
		if (buf.size() == 0 || bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			return;
		parse_torrent_file(e, ec, flags);

		INVARIANT_CHECK;
	}

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE
	torrent_info::torrent_info(std::wstring const& filename, error_code& ec, int flags)
		: m_piece_hashes(0)
		, m_creation_date(0)
		, m_merkle_first_leaf(0)
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

		bdecode_node e;
		if (buf.size() == 0 || bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
			return;
		parse_torrent_file(e, ec, flags);

		INVARIANT_CHECK;
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash, int flags)
		: m_piece_hashes(0)
		, m_creation_date(time(0))
		, m_info_hash(info_hash)
		, m_merkle_first_leaf(0)
		, m_info_section_size(0)
		, m_multifile(false)
		, m_private(false)
		, m_i2p(false)
	{}

	torrent_info::~torrent_info()
	{}

	void torrent_info::load(char const* buffer, int size, error_code& ec)
	{
		bdecode_node e;
		if (bdecode(buffer, buffer + size, e, ec) != 0)
			return;

		if (!parse_torrent_file(e, ec, 0))
			return;
	}

	void torrent_info::unload()
	{
		TORRENT_ASSERT(m_info_section.unique());

		m_info_section.reset();
		m_info_section_size = 0;

		// if we have orig_files, we have to keep
		// m_files around, since it means we have
		// remapped files, and we won't be able to
		// restore that from just reloading the
		// torrent file
		if (m_orig_files) m_orig_files.reset();
		else m_files.unload();

		m_piece_hashes = 0;
		std::vector<web_seed_entry>().swap(m_web_seeds);

		TORRENT_ASSERT(!is_loaded());
	}

	void torrent_info::copy_on_write()
	{
		TORRENT_ASSERT(is_loaded());
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

	std::string torrent_info::ssl_cert() const
	{
		// this is parsed lazily
		if (!m_info_dict)
		{
			error_code ec;
			bdecode(m_info_section.get(), m_info_section.get()
				+ m_info_section_size, m_info_dict, ec);
			if (ec) return "";
		}
		if (m_info_dict.type() != bdecode_node::dict_t) return "";
		return m_info_dict.dict_find_string_value("ssl-cert");
	}

	bool torrent_info::parse_info_section(bdecode_node const& info
		, error_code& ec, int flags)
	{
		if (info.type() != bdecode_node::dict_t)
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
		file_storage files;
		files.set_piece_length(piece_length);

		// extract file name (or the directory name if it's a multifile libtorrent)
		bdecode_node name_ent = info.dict_find_string("name.utf-8");
		if (!name_ent) name_ent = info.dict_find_string("name");
		if (!name_ent)
		{
			ec = errors::torrent_missing_name;
			return false;
		}

		std::string name;
		sanitize_append_path_element(name, name_ent.string_ptr()
			, name_ent.string_length());
		if (name.empty()) name = to_hex(m_info_hash.to_string());

		// extract file list
		bdecode_node i = info.dict_find_list("files");
		if (!i)
		{
			// if there's no list of files, there has to be a length
			// field.
			if (!extract_single_file(info, files, "", info_ptr_diff, true, ec))
				return false;

			m_multifile = false;
		}
		else
		{
			if (!extract_files(i, files, name, info_ptr_diff, ec))
				return false;
			m_multifile = true;
		}
		TORRENT_ASSERT(!files.name().empty());

		// extract sha-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		files.set_num_pieces(int((files.total_size() + files.piece_length() - 1)
			/ files.piece_length()));

		bdecode_node pieces = info.dict_find_string("pieces");
		bdecode_node root_hash = info.dict_find_string("root hash");
		if (!pieces && !root_hash)
		{
			ec = errors::torrent_missing_pieces;
			return false;
		}
		
		if (pieces)
		{
			if (pieces.string_length() != files.num_pieces() * 20)
			{
				ec = errors::torrent_invalid_hashes;
				return false;
			}

			m_piece_hashes = pieces.string_ptr() + info_ptr_diff;
			TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
			TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
		}
		else
		{
			TORRENT_ASSERT(root_hash);
			if (root_hash.string_length() != 20)
			{
				ec = errors::torrent_invalid_hashes;
				return false;
			}
			int num_leafs = merkle_num_leafs(files.num_pieces());
			int num_nodes = merkle_num_nodes(num_leafs);
			m_merkle_first_leaf = num_nodes - num_leafs;
			m_merkle_tree.resize(num_nodes);
			std::memset(&m_merkle_tree[0], 0, num_nodes * 20);
			m_merkle_tree[0].assign(root_hash.string_ptr());
		}

		m_private = info.dict_find_int_value("private", 0);

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node similar = info.dict_find_list("similar");
		if (similar)
		{
			for (int i = 0; i < similar.list_size(); ++i)
			{
				if (similar.list_at(i).type() != bdecode_node::string_t)
					continue;

				if (similar.list_at(i).string_length() != 20)
					continue;

				m_similar_torrents.push_back(similar.list_at(i).string_ptr()
					+ info_ptr_diff);
			}
		}

		bdecode_node collections = info.dict_find_list("collections");
		if (collections)
		{
			for (int i = 0; i < collections.list_size(); ++i)
			{
				bdecode_node str = collections.list_at(i);

				if (str.type() != bdecode_node::string_t) continue;

				m_collections.push_back(std::make_pair(str.string_ptr()
					+ info_ptr_diff, str.string_length()));
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		// now, commit the files structure we just parsed out
		// into the torrent_info object.
		// if we already have an m_files that's populated, it
		// indicates that we unloaded this torrent_info ones
		// and we had modifications to the files, so we unloaded
		// the orig_files. In that case, the orig files is what
		// needs to be restored
		if (m_files.is_loaded()) {
			m_orig_files.reset(new file_storage);
			const_cast<file_storage&>(*m_orig_files).swap(files);
		}
		else
		{
			m_files.swap(files);
		}
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

	bool torrent_info::parse_torrent_file(bdecode_node const& torrent_file
		, error_code& ec, int flags)
	{
		if (torrent_file.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_is_no_dict;
			return false;
		}

		bdecode_node info = torrent_file.dict_find_dict("info");
		if (info == 0)
		{
			bdecode_node link = torrent_file.dict_find_string("magnet-uri");
			if (link)
			{
				std::string uri = link.string_value();

				add_torrent_params p;
				parse_magnet_uri(uri, p, ec);
				if (ec) return false;

				m_info_hash = p.info_hash;
				for (std::vector<std::string>::iterator i = p.trackers.begin()
					, end(p.trackers.end()); i != end; ++i)
					m_urls.push_back(*i);

				return true;
			}

			ec = errors::torrent_missing_info;
			return false;
		}
		if (!parse_info_section(info, ec, flags)) return false;
		resolve_duplicate_filenames();

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node similar = torrent_file.dict_find_list("similar");
		if (similar)
		{
			for (int i = 0; i < similar.list_size(); ++i)
			{
				if (similar.list_at(i).type() != bdecode_node::string_t)
					continue;

				if (similar.list_at(i).string_length() != 20)
					continue;

				m_owned_similar_torrents.push_back(
					sha1_hash(similar.list_at(i).string_ptr()));
			}
		}

		bdecode_node collections = torrent_file.dict_find_list("collections");
		if (collections)
		{
			for (int i = 0; i < collections.list_size(); ++i)
			{
				bdecode_node str = collections.list_at(i);

				if (str.type() != bdecode_node::string_t) continue;

				m_owned_collections.push_back(std::string(str.string_ptr()
					, str.string_length()));
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		// extract the url of the tracker
		bdecode_node i = torrent_file.dict_find_list("announce-list");
		if (i)
		{
			m_urls.reserve(i.list_size());
			for (int j = 0, end(i.list_size()); j < end; ++j)
			{
				bdecode_node tier = i.list_at(j);
				if (tier.type() != bdecode_node::list_t) continue;
				for (int k = 0, end(tier.list_size()); k < end; ++k)
				{
					announce_entry e(tier.list_string_value_at(k));
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

		bdecode_node nodes = torrent_file.dict_find_list("nodes");
		if (nodes)
		{
			for (int i = 0, end(nodes.list_size()); i < end; ++i)
			{
				bdecode_node n = nodes.list_at(i);
				if (n.type() != bdecode_node::list_t
					|| n.list_size() < 2
					|| n.list_at(0).type() != bdecode_node::string_t
					|| n.list_at(1).type() != bdecode_node::int_t)
					continue;
				m_nodes.push_back(std::make_pair(
					n.list_at(0).string_value()
					, int(n.list_at(1).int_value())));
			}
		}

		// extract creation date
		boost::int64_t cd = torrent_file.dict_find_int_value("creation date", -1);
		if (cd >= 0)
		{
			m_creation_date = long(cd);
		}

		// if there are any url-seeds, extract them
		bdecode_node url_seeds = torrent_file.dict_find("url-list");
		if (url_seeds && url_seeds.type() == bdecode_node::string_t
			&& url_seeds.string_length() > 0)
		{
			web_seed_entry ent(maybe_url_encode(url_seeds.string_value())
				, web_seed_entry::url_seed);
			if (m_multifile && ent.url[ent.url.size()-1] != '/') ent.url += '/';
			m_web_seeds.push_back(ent);
		}
		else if (url_seeds && url_seeds.type() == bdecode_node::list_t)
		{
			// only add a URL once
			std::set<std::string> unique;
			for (int i = 0, end(url_seeds.list_size()); i < end; ++i)
			{
				bdecode_node url = url_seeds.list_at(i);
				if (url.type() != bdecode_node::string_t) continue;
				if (url.string_length() == 0) continue;
				web_seed_entry ent(maybe_url_encode(url.string_value())
					, web_seed_entry::url_seed);
				if (m_multifile && ent.url[ent.url.size()-1] != '/') ent.url += '/';
				if (unique.count(ent.url)) continue;
				unique.insert(ent.url);
				m_web_seeds.push_back(ent);
			}
		}

		// if there are any http-seeds, extract them
		bdecode_node http_seeds = torrent_file.dict_find("httpseeds");
		if (http_seeds && http_seeds.type() == bdecode_node::string_t
			&& http_seeds.string_length() > 0)
		{
			m_web_seeds.push_back(web_seed_entry(maybe_url_encode(http_seeds.string_value())
				, web_seed_entry::http_seed));
		}
		else if (http_seeds && http_seeds.type() == bdecode_node::list_t)
		{
			// only add a URL once
			std::set<std::string> unique;
			for (int i = 0, end(http_seeds.list_size()); i < end; ++i)
			{
				bdecode_node url = http_seeds.list_at(i);
				if (url.type() != bdecode_node::string_t || url.string_length() == 0) continue;
				std::string u = maybe_url_encode(url.string_value());
				if (unique.count(u)) continue;
				unique.insert(u);
				m_web_seeds.push_back(web_seed_entry(u, web_seed_entry::http_seed));
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

	bool torrent_info::parse_info_section(lazy_entry const& le, error_code& ec
		, int flags)
	{
		if (le.type() == lazy_entry::none_t) return false;
		std::pair<char const*, int> buf = le.data_section();
		bdecode_node e;
		if (bdecode(buf.first, buf.first + buf.second, e, ec) != 0)
			return false;

		return parse_info_section(e, ec, flags);
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

	std::vector<sha1_hash> torrent_info::similar_torrents() const
	{
		std::vector<sha1_hash> ret;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		ret.reserve(m_similar_torrents.size() + m_owned_similar_torrents.size());

		for (int i = 0; i < m_similar_torrents.size(); ++i)
			ret.push_back(sha1_hash(m_similar_torrents[i]));

		for (int i = 0; i < m_owned_similar_torrents.size(); ++i)
			ret.push_back(m_owned_similar_torrents[i]);
#endif

		return ret;
	}

	std::vector<std::string> torrent_info::collections() const
	{
		std::vector<std::string> ret;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		ret.reserve(m_collections.size() + m_owned_collections.size());

		for (int i = 0; i < m_collections.size(); ++i)
			ret.push_back(std::string(m_collections[i].first, m_collections[i].second));

		for (int i = 0; i < m_owned_collections.size(); ++i)
			ret.push_back(m_owned_collections[i]);
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		return ret;
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
		for (int i = 0; i < m_files.num_files(); ++i)
			os << "  " << std::setw(11) << m_files.file_size(i)
				<< "  " << m_files.file_path(i) << "\n";
	}

// ------- end deprecation -------
#endif

#if TORRENT_USE_INVARIANT_CHECKS
	void torrent_info::check_invariant() const
	{
		for (int i = 0; i < m_files.num_files(); ++i)
		{
			TORRENT_ASSERT(m_files.file_name_ptr(i) != 0);
			if (m_files.file_name_len(i) != -1)
			{
				// name needs to point into the allocated info section buffer
				TORRENT_ASSERT(m_files.file_name_ptr(i) >= m_info_section.get());
				TORRENT_ASSERT(m_files.file_name_ptr(i) < m_info_section.get() + m_info_section_size);
			}
			else
			{
				// name must be a valid string
				TORRENT_ASSERT(strlen(m_files.file_name_ptr(i)) < 2048);
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

