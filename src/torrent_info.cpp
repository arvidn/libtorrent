/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/ConvertUTF.h"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/string_util.hpp" // is_space, is_i2p_url
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/escape_string.hpp" // maybe_url_encode
#include "libtorrent/aux_/merkle.hpp" // for merkle_*
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/numeric_cast.hpp"

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/lazy_entry.hpp"
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <iterator>
#include <algorithm>
#include <set>
#include <ctime>
#include <array>

namespace libtorrent {

	TORRENT_EXPORT from_span_t from_span;

	namespace {

	// this is an arbitrary limit to avoid malicious torrents causing
	// unreasaonably large allocations for the merkle hash tree
	// the size of the tree would be max_pieces * sizeof(int) * 2
	// which is about 8 MB with this limit
	// TODO: remove this limit and the overloads that imply it, in favour of
	// using load_torrent_limits
	constexpr int default_piece_limit = 0x100000;

	bool valid_path_character(std::int32_t const c)
	{
#ifdef TORRENT_WINDOWS
		static const char invalid_chars[] = "?<>\"|\b*:";
#else
		static const char invalid_chars[] = "";
#endif
		if (c < 32) return false;
		if (c > 127) return true;
		return std::strchr(invalid_chars, static_cast<char>(c)) == nullptr;
	}

	bool filter_path_character(std::int32_t const c)
	{
		// these unicode characters change the writing direction of the
		// string and can be used for attacks:
		// https://security.stackexchange.com/questions/158802/how-can-this-executable-have-an-avi-extension
		static const std::array<std::int32_t, 7> bad_cp = {{0x202a, 0x202b, 0x202c, 0x202d, 0x202e, 0x200e, 0x200f}};
		if (std::find(bad_cp.begin(), bad_cp.end(), c) != bad_cp.end()) return true;

		static const char invalid_chars[] = "/\\";
		if (c > 127) return false;
		return std::strchr(invalid_chars, static_cast<char>(c)) != nullptr;
	}

	} // anonymous namespace

	// fixes invalid UTF-8 sequences
	bool verify_encoding(std::string& target)
	{
		if (target.empty()) return true;

		std::string tmp_path;
		tmp_path.reserve(target.size()+5);
		bool valid_encoding = true;

		UTF8 const* ptr = reinterpret_cast<UTF8 const*>(&target[0]);
		UTF8 const* end = ptr + target.size();
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

			// encode codepoint into utf-8
			cp = &codepoint;
			UTF8 sequence[5];
			UTF8* start = sequence;
			res = ConvertUTF32toUTF8(const_cast<const UTF32**>(&cp), cp + 1, &start, start + 5, lenientConversion);
			TORRENT_UNUSED(res);
			TORRENT_ASSERT(res == conversionOK);

			for (int i = 0; i < std::min(5, int(start - sequence)); ++i)
				tmp_path += char(sequence[i]);
		}

		// the encoding was not valid utf-8
		// save the original encoding and replace the
		// commonly used path with the correctly
		// encoded string
		if (!valid_encoding) target = tmp_path;
		return valid_encoding;
	}

	void sanitize_append_path_element(std::string& path, string_view element)
	{
		if (element.size() == 1 && element[0] == '.') return;

#ifdef TORRENT_WINDOWS
#define TORRENT_SEPARATOR '\\'
#else
#define TORRENT_SEPARATOR '/'
#endif
		path.reserve(path.size() + element.size() + 2);
		int added_separator = 0;
		if (!path.empty())
		{
			path += TORRENT_SEPARATOR;
			added_separator = 1;
		}

		if (element.empty())
		{
			path += "_";
			return;
		}

#if !TORRENT_USE_UNC_PATHS && defined TORRENT_WINDOWS
#pragma message ("building for windows without UNC paths is deprecated")

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
		std::string pe = element.to_string();
		char const* file_end = strrchr(pe.c_str(), '.');
		std::string name = file_end
			? std::string(pe.data(), file_end)
			: pe;
		std::transform(name.begin(), name.end(), name.begin(), &to_lower);
		char const** str = std::find(reserved_names, reserved_names + num_names, name);
		if (str != reserved_names + num_names)
		{
			pe = "_" + pe;
			element = string_view();
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

		int seq_len = 0;
		for (std::size_t i = 0; i < element.size(); i += std::size_t(seq_len))
		{
			std::int32_t code_point;
			std::tie(code_point, seq_len) = parse_utf8_codepoint(element.substr(i));

			if (code_point >= 0 && filter_path_character(code_point))
			{
				continue;
			}

			if (code_point < 0 || !valid_path_character(code_point))
			{
				// invalid utf8 sequence, replace with "_"
				path += '_';
				++added;
				++unicode_chars;
				continue;
			}

			TORRENT_ASSERT(isLegalUTF8(reinterpret_cast<UTF8 const*>(element.data() + i), seq_len));

			// validation passed, add it to the output string
			for (std::size_t k = i; k < i + std::size_t(seq_len); ++k)
			{
				TORRENT_ASSERT(element[k] != 0);
				path.push_back(element[k]);
			}

			if (code_point == '.') ++num_dots;

			added += seq_len;
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
				for (int j = int(element.size()) - 1;
					j > std::max(int(element.size()) - 10, int(i)); --j)
				{
					if (element[aux::numeric_cast<std::size_t>(j)] != '.') continue;
					dot = j;
					break;
				}
				// there is no extension
				if (dot == -1) break;
				found_extension = true;
				TORRENT_ASSERT(dot > 0);
				i = std::size_t(dot - 1);
			}
		}

		if (added == num_dots && added <= 2)
		{
			// revert everything
			path.erase(path.end() - added - added_separator, path.end());
			return;
		}

#ifdef TORRENT_WINDOWS
		// remove trailing spaces and dots. These aren't allowed in filenames on windows
		for (int i = int(path.size()) - 1; i >= 0; --i)
		{
			if (path[i] != ' ' && path[i] != '.') break;
			path.resize(i);
			--added;
			TORRENT_ASSERT(added >= 0);
		}

		if (added == 0 && added_separator)
		{
			// remove the separator added at the beginning
			path.erase(path.end() - 1);
			return;
		}
#endif

		if (path.empty()) path = "_";
	}

namespace {

	file_flags_t get_file_attributes(bdecode_node const& dict)
	{
		file_flags_t file_flags = {};
		bdecode_node const attr = dict.dict_find_string("attr");
		if (attr)
		{
			for (char const c : attr.string_value())
			{
				switch (c)
				{
					case 'l': file_flags |= file_storage::flag_symlink; break;
					case 'x': file_flags |= file_storage::flag_executable; break;
					case 'h': file_flags |= file_storage::flag_hidden; break;
					case 'p': file_flags |= file_storage::flag_pad_file; break;
				}
			}
		}
		return file_flags;
	}

	// iterates an array of strings and returns the sum of the lengths of all
	// strings + one additional character per entry (to account for the presumed
	// forward- or backslash to separate directory entries)
	int path_length(bdecode_node const& p, error_code& ec)
	{
		int ret = 0;
		int const len = p.list_size();
		for (int i = 0; i < len; ++i)
		{
			bdecode_node const e = p.list_at(i);
			if (e.type() != bdecode_node::string_t)
			{
				ec = errors::torrent_invalid_name;
				return -1;
			}
			ret += e.string_length();
		}
		return ret + len;
	}

	// 'top_level' is extracting the file for a single-file torrent. The
	// distinction is that the filename is found in "name" rather than
	// "path"
	// root_dir is the name of the torrent, unless this is a single file
	// torrent, in which case it's empty.
	bool extract_single_file(bdecode_node const& dict, file_storage& files
		, std::string const& root_dir, std::ptrdiff_t const info_ptr_diff, bool top_level
		, int& pad_file_cnt, error_code& ec)
	{
		if (dict.type() != bdecode_node::dict_t) return false;

		file_flags_t file_flags = get_file_attributes(dict);

		// symlinks have an implied "size" of zero. i.e. they use up 0 bytes of
		// the torrent payload space
		std::int64_t const file_size = (file_flags & file_storage::flag_symlink)
			? 0
			: dict.dict_find_int_value("length", -1);
		if (file_size < 0 )
		{
			ec = errors::torrent_invalid_length;
			return false;
		}

		std::time_t const mtime = std::time_t(dict.dict_find_int_value("mtime", 0));

		std::string path = root_dir;
		string_view filename;

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

			filename = { p.string_ptr() + info_ptr_diff
				, static_cast<std::size_t>(p.string_length())};

			while (!filename.empty() && filename.front() == TORRENT_SEPARATOR)
				filename.remove_prefix(1);

			sanitize_append_path_element(path, p.string_value());
			if (path.empty())
			{
				ec = errors::torrent_missing_name;
				return false;
			}
		}
		else
		{
			bdecode_node p = dict.dict_find_list("path.utf-8");
			if (!p) p = dict.dict_find_list("path");

			if (p && p.list_size() > 0)
			{
				std::size_t const preallocate = path.size() + std::size_t(path_length(p, ec));
				std::size_t const orig_path_len = path.size();
				if (ec) return false;
				path.reserve(preallocate);

				for (int i = 0, end(p.list_size()); i < end; ++i)
				{
					bdecode_node const e = p.list_at(i);
					if (i == end - 1)
					{
						filename = {e.string_ptr() + info_ptr_diff
							, static_cast<std::size_t>(e.string_length()) };
						while (!filename.empty() && filename.front() == TORRENT_SEPARATOR)
							filename.remove_prefix(1);
					}
					sanitize_append_path_element(path, e.string_value());
				}

				// if all path elements were sanitized away, we need to use another
				// name instead
				if (path.size() == orig_path_len)
				{
					path += TORRENT_SEPARATOR;
					path += "_";
				}
			}
			else if (file_flags & file_storage::flag_pad_file)
			{
				// pad files don't need a path element, we'll just store them
				// under the .pad directory
				char cnt[11];
				std::snprintf(cnt, sizeof(cnt), "%d", pad_file_cnt);
				path = combine_path(".pad", cnt);
				++pad_file_cnt;
			}
			else
			{
				ec = errors::torrent_missing_name;
				return false;
			}
		}

		// bitcomet pad file
		if (path.find("_____padding_file_") != std::string::npos)
			file_flags |= file_storage::flag_pad_file;

		bdecode_node const fh = dict.dict_find_string("sha1");
		char const* filehash = nullptr;
		if (fh && fh.string_length() == 20)
			filehash = fh.string_ptr() + info_ptr_diff;

		std::string symlink_path;
		if (file_flags & file_storage::flag_symlink)
		{
			if (bdecode_node const s_p = dict.dict_find_list("symlink path"))
			{
				std::size_t const preallocate = std::size_t(path_length(s_p, ec));
				if (ec) return false;
				symlink_path.reserve(preallocate);
				for (int i = 0, end(s_p.list_size()); i < end; ++i)
				{
					auto pe = s_p.list_at(i).string_value();
					sanitize_append_path_element(symlink_path, pe);
				}
			}
			// symlink targets are validated later, as it may point to a file or
			// directory we haven't parsed yet
		}
		else
		{
			// technically this is an invalid torrent. "symlink path" must exist
			file_flags &= ~file_storage::flag_symlink;
		}

		if (filename.size() > path.length()
			|| path.substr(path.size() - filename.size()) != filename)
		{
			// if the filename was sanitized and differ, clear it to just use path
			filename = {};
		}

		files.add_file_borrow(filename, path, file_size, file_flags, filehash
			, mtime, symlink_path);
		return true;
	}

	// root_dir is the name of the torrent, unless this is a single file
	// torrent, in which case it's empty.
	bool extract_files(bdecode_node const& list, file_storage& target
		, std::string const& root_dir, std::ptrdiff_t info_ptr_diff, error_code& ec)
	{
		if (list.type() != bdecode_node::list_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}
		target.reserve(list.list_size());

		// this is the counter used to name pad files
		int pad_file_cnt = 0;
		for (int i = 0, end(list.list_size()); i < end; ++i)
		{
			if (!extract_single_file(list.list_at(i), target, root_dir
				, info_ptr_diff, false, pad_file_cnt, ec))
				return false;
		}
		// this rewrites invalid symlinks to point to themselves
		target.sanitize_symlinks();
		return true;
	}

	int load_file(std::string const& filename, std::vector<char>& v
		, error_code& ec, int const max_buffer_size = 80000000)
	{
		ec.clear();
		file f;
		if (!f.open(filename, open_mode::read_only, ec)) return -1;
		std::int64_t const s = f.get_size(ec);
		if (ec) return -1;
		if (s > max_buffer_size)
		{
			ec = errors::metadata_too_large;
			return -1;
		}
		v.resize(std::size_t(s));
		if (s == 0) return 0;
		std::int64_t const read = f.readv(0, {v}, ec);
		if (read != s) return -3;
		if (ec) return -3;
		return 0;
	}

} // anonymous namespace

	web_seed_entry::web_seed_entry(std::string const& url_, type_t type_
		, std::string const& auth_
		, headers_t const& extra_headers_)
		: url(url_)
		, auth(auth_)
		, extra_headers(extra_headers_)
		, type(std::uint8_t(type_))
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
		, m_info_section_size(t.m_info_section_size)
		, m_merkle_first_leaf(t.m_merkle_first_leaf)
		, m_flags(t.m_flags)
	{
#if TORRENT_USE_INVARIANT_CHECKS
		t.check_invariant();
#endif
		if (m_info_section_size == 0) return;
		TORRENT_ASSERT(m_piece_hashes);

		m_info_section.reset(new char[aux::numeric_cast<std::size_t>(m_info_section_size)]);
		std::memcpy(m_info_section.get(), t.m_info_section.get(), aux::numeric_cast<std::size_t>(m_info_section_size));

		std::ptrdiff_t const offset = m_info_section.get() - t.m_info_section.get();

		m_files.apply_pointer_offset(offset);
		if (m_orig_files)
			const_cast<file_storage&>(*m_orig_files).apply_pointer_offset(offset);

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		for (auto& c : m_collections)
			c.first += offset;

		for (auto& st : m_similar_torrents)
			st += offset;
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

		std::unordered_set<std::uint32_t> files;

		std::string const empty_str;

		// insert all directories first, to make sure no files
		// are allowed to collied with them
		m_files.all_path_hashes(files);
		for (auto const i : m_files.file_range())
		{
			// as long as this file already exists
			// increase the counter
			std::uint32_t const h = m_files.file_path_hash(i, empty_str);
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

namespace {

	template <class CRC>
	void process_string_lowercase(CRC& crc, string_view str)
	{
		for (char const c : str)
			crc.process_byte(to_lower(c) & 0xff);
	}

	struct name_entry
	{
		file_index_t idx;
		int length;
	};
}

	void torrent_info::resolve_duplicate_filenames_slow()
	{
		INVARIANT_CHECK;

		// maps filename hash to file index
		// or, if the file_index is negative, maps into the paths vector
		std::unordered_multimap<std::uint32_t, name_entry> files;

		std::vector<std::string> const& paths = m_files.paths();
		files.reserve(paths.size() + aux::numeric_cast<std::size_t>(m_files.num_files()));

		// insert all directories first, to make sure no files
		// are allowed to collied with them
		{
			boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
			if (!m_files.name().empty())
			{
				process_string_lowercase(crc, m_files.name());
			}
			file_index_t path_index{-1};
			for (auto const& path : paths)
			{
				auto local_crc = crc;
				if (!path.empty()) local_crc.process_byte(TORRENT_SEPARATOR);
				int count = 0;
				for (char const c : path)
				{
					if (c == TORRENT_SEPARATOR)
						files.insert({local_crc.checksum(), {path_index, count}});
					local_crc.process_byte(to_lower(c) & 0xff);
					++count;
				}
				files.insert({local_crc.checksum(), {path_index, int(path.size())}});
				--path_index;
			}
		}

		// keep track of the total number of name collisions. If there are too
		// many, it's probably a malicious torrent and we should just fail
		int num_collisions = 0;
		for (auto const i : m_files.file_range())
		{
			// as long as this file already exists
			// increase the counter
			std::uint32_t const hash = m_files.file_path_hash(i, "");
			auto range = files.equal_range(hash);
			auto const match = std::find_if(range.first, range.second, [&](std::pair<std::uint32_t, name_entry> const& o)
			{
				std::string const other_name = o.second.idx < file_index_t{}
					? combine_path(m_files.name(), paths[std::size_t(-static_cast<int>(o.second.idx)-1)].substr(0, std::size_t(o.second.length)))
					: m_files.file_path(o.second.idx);
				return string_equal_no_case(other_name, m_files.file_path(i));
			});

			if (match == range.second)
			{
				files.insert({hash, {i, 0}});
				continue;
			}

			// pad files are allowed to collide with each-other, as long as they have
			// the same size.
			file_index_t const other_idx = match->second.idx;
			if (other_idx >= file_index_t{}
				&& (m_files.file_flags(i) & file_storage::flag_pad_file)
				&& (m_files.file_flags(other_idx) & file_storage::flag_pad_file)
				&& m_files.file_size(i) == m_files.file_size(other_idx))
				continue;

			std::string filename = m_files.file_path(i);
			std::string base = remove_extension(filename);
			std::string ext = extension(filename);
			int cnt = 0;
			for (;;)
			{
				++cnt;
				char new_ext[50];
				std::snprintf(new_ext, sizeof(new_ext), ".%d%s", cnt, ext.c_str());
				filename = base + new_ext;

				boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
				process_string_lowercase(crc, filename);
				std::uint32_t const new_hash = crc.checksum();
				if (files.find(new_hash) == files.end())
				{
					files.insert({new_hash, {i, 0}});
					break;
				}
				++num_collisions;
				if (num_collisions > 100)
				{
				// TODO: this should be considered a failure, and the .torrent file
				// rejected
				}
			}

			copy_on_write();
			m_files.rename_file(i, filename);
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

#if TORRENT_ABI_VERSION == 1
	torrent_info::torrent_info(lazy_entry const& torrent_file, error_code& ec)
	{
		std::pair<char const*, int> buf = torrent_file.data_section();
		bdecode_node e;
		if (bdecode(buf.first, buf.first + buf.second, e, ec) != 0)
			return;
		parse_torrent_file(e, ec);
	}

	torrent_info::torrent_info(lazy_entry const& torrent_file)
	{
		std::pair<char const*, int> buf = torrent_file.data_section();
		bdecode_node e;
		error_code ec;
		if (bdecode(buf.first, buf.first + buf.second, e, ec) != 0)
		{
			aux::throw_ex<system_error>(ec);
		}
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, ec))
			aux::throw_ex<system_error>(ec);
#else
		parse_torrent_file(e, ec);
#endif
	}

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
	{
		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char>> out(tmp);
		bencode(out, torrent_file);

		bdecode_node e;
		error_code ec;
		if (tmp.empty() || bdecode(&tmp[0], &tmp[0] + tmp.size(), e, ec) != 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			aux::throw_ex<system_error>(ec);
#else
			return;
#endif
		}
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, ec))
			aux::throw_ex<system_error>(ec);
#else
		parse_torrent_file(e, ec);
#endif
		INVARIANT_CHECK;
	}
#endif // TORRENT_ABI_VERSION

#ifndef BOOST_NO_EXCEPTIONS
	torrent_info::torrent_info(bdecode_node const& torrent_file)
	{
		error_code ec;
		if (!parse_torrent_file(torrent_file, ec))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(span<char const> buffer, from_span_t)
	{
		error_code ec;
		bdecode_node e = bdecode(buffer, ec);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename)
	{
		std::vector<char> buf;
		error_code ec;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) aux::throw_ex<system_error>(ec);

		bdecode_node e = bdecode(buf, ec);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(bdecode_node const& torrent_file
		, load_torrent_limits const& cfg)
	{
		error_code ec;
		if (!parse_torrent_file(torrent_file, ec, cfg.max_pieces))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(span<char const> buffer
		, load_torrent_limits const& cfg, from_span_t)
	{
		error_code ec;
		bdecode_node e = bdecode(buffer, ec, nullptr
			, cfg.max_decode_depth, cfg.max_decode_tokens);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec, cfg.max_pieces))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename
		, load_torrent_limits const& cfg)
	{
		std::vector<char> buf;
		error_code ec;
		int ret = load_file(filename, buf, ec, cfg.max_buffer_size);
		if (ret < 0) aux::throw_ex<system_error>(ec);

		bdecode_node e = bdecode(buf, ec, nullptr, cfg.max_decode_depth
			, cfg.max_decode_tokens);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec, cfg.max_pieces))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

#if TORRENT_ABI_VERSION == 1
	torrent_info::torrent_info(std::wstring const& filename)
	{
		std::vector<char> buf;
		error_code ec;
		int ret = load_file(wchar_utf8(filename), buf, ec);
		if (ret < 0) aux::throw_ex<system_error>(ec);

		bdecode_node e = bdecode(buf, ec);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	void torrent_info::rename_file(file_index_t index, std::wstring const& new_filename)
	{
		TORRENT_ASSERT(is_loaded());
		copy_on_write();
		m_files.rename_file_deprecated(index, new_filename);
	}
#endif // TORRENT_ABI_VERSION
#endif

	torrent_info::torrent_info(bdecode_node const& torrent_file
		, error_code& ec)
	{
		parse_torrent_file(torrent_file, ec);
		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(span<char const> buffer
		, error_code& ec, from_span_t)
	{
		bdecode_node e = bdecode(buffer, ec);
		if (ec) return;
		parse_torrent_file(e, ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, error_code& ec)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) return;

		bdecode_node e = bdecode(buf, ec);
		if (ec) return;
		parse_torrent_file(e, ec);

		INVARIANT_CHECK;
	}

#if TORRENT_ABI_VERSION == 1
	torrent_info::torrent_info(std::wstring const& filename
		, error_code& ec)
	{
		std::vector<char> buf;
		int ret = load_file(wchar_utf8(filename), buf, ec);
		if (ret < 0) return;

		bdecode_node e = bdecode(buf, ec);
		if (ec) return;
		parse_torrent_file(e, ec);

		INVARIANT_CHECK;
	}
#endif // TORRENT_ABI_VERSION

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash)
		: m_info_hash(info_hash)
	{}

	torrent_info::~torrent_info() = default;

	sha1_hash torrent_info::hash_for_piece(piece_index_t const index) const
	{ return sha1_hash(hash_for_piece_ptr(index)); }

	void torrent_info::copy_on_write()
	{
		TORRENT_ASSERT(is_loaded());
		INVARIANT_CHECK;

		if (m_orig_files) return;
		m_orig_files.reset(new file_storage(m_files));
	}

	void torrent_info::swap(torrent_info& ti)
	{
		INVARIANT_CHECK;

		using std::swap;
		m_urls.swap(ti.m_urls);
		m_web_seeds.swap(ti.m_web_seeds);
		m_files.swap(ti.m_files);
		m_orig_files.swap(ti.m_orig_files);
		m_nodes.swap(ti.m_nodes);
		m_similar_torrents.swap(ti.m_similar_torrents);
		m_owned_similar_torrents.swap(ti.m_owned_similar_torrents);
		m_collections.swap(ti.m_collections);
		m_owned_collections.swap(ti.m_owned_collections);
		swap(m_info_hash, ti.m_info_hash);
		swap(m_creation_date, ti.m_creation_date);
		m_comment.swap(ti.m_comment);
		m_created_by.swap(ti.m_created_by);
		swap(m_info_section, ti.m_info_section);
		swap(m_piece_hashes, ti.m_piece_hashes);
		m_info_dict.swap(ti.m_info_dict);
		swap(m_merkle_tree, ti.m_merkle_tree);
		swap(m_info_section_size, ti.m_info_section_size);
		swap(m_merkle_first_leaf, ti.m_merkle_first_leaf);
		swap(m_flags, ti.m_flags);
	}

	string_view torrent_info::ssl_cert() const
	{
		if ((m_flags & ssl_torrent) == 0) return "";

		// this is parsed lazily
		if (!m_info_dict)
		{
			error_code ec;
			bdecode(m_info_section.get(), m_info_section.get()
				+ m_info_section_size, m_info_dict, ec);
			TORRENT_ASSERT(!ec);
			if (ec) return "";
		}
		TORRENT_ASSERT(m_info_dict.type() == bdecode_node::dict_t);
		if (m_info_dict.type() != bdecode_node::dict_t) return "";
		return m_info_dict.dict_find_string_value("ssl-cert");
	}

	bool torrent_info::parse_info_section(bdecode_node const& e, error_code& ec)
	{
		return parse_info_section(e, ec, default_piece_limit);
	}

	bool torrent_info::parse_info_section(bdecode_node const& info
		, error_code& ec, int const max_pieces)
	{
		if (info.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_info_no_dict;
			return false;
		}

		// hash the info-field to calculate info-hash
		auto section = info.data_section();
		m_info_hash = hasher(section).final();
		if (info.data_section().size() >= std::numeric_limits<int>::max())
		{
			ec = errors::metadata_too_large;
			return false;
		}

		// copy the info section
		m_info_section_size = int(section.size());
		m_info_section.reset(new char[aux::numeric_cast<std::size_t>(m_info_section_size)]);
		std::memcpy(m_info_section.get(), section.data(), aux::numeric_cast<std::size_t>(m_info_section_size));
		TORRENT_ASSERT(section[0] == 'd');
		TORRENT_ASSERT(section[m_info_section_size - 1] == 'e');

		// when translating a pointer that points into the 'info' tree's
		// backing buffer, into a pointer to our copy of the info section,
		// this is the pointer offset to use.
		std::ptrdiff_t const info_ptr_diff = m_info_section.get() - section.data();

		// extract piece length
		std::int64_t piece_length = info.dict_find_int_value("piece length", -1);
		if (piece_length <= 0 || piece_length > std::numeric_limits<int>::max())
		{
			ec = errors::torrent_missing_piece_length;
			return false;
		}
		file_storage files;
		files.set_piece_length(static_cast<int>(piece_length));

		// extract file name (or the directory name if it's a multi file libtorrent)
		bdecode_node name_ent = info.dict_find_string("name.utf-8");
		if (!name_ent) name_ent = info.dict_find_string("name");
		if (!name_ent)
		{
			ec = errors::torrent_missing_name;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		std::string name;
		sanitize_append_path_element(name, name_ent.string_value());
		if (name.empty()) name = aux::to_hex(m_info_hash);

		// extract file list
		bdecode_node const files_node = info.dict_find_list("files");
		if (!files_node)
		{
			// if there's no list of files, there has to be a length
			// field.
			// this is the counter used to name pad files
			int pad_file_cnt = 0;
			if (!extract_single_file(info, files, "", info_ptr_diff, true, pad_file_cnt, ec))
			{
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}

			files.sanitize_symlinks();
			m_flags &= ~multifile;
		}
		else
		{
			if (!extract_files(files_node, files, name, info_ptr_diff, ec))
			{
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}
			m_flags |= multifile;
		}
		if (files.num_files() == 0)
		{
			ec = errors::no_files_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}
		if (files.name().empty())
		{
			ec = errors::torrent_missing_name;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		// extract SHA-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		if (files.total_size() >=
			static_cast<std::int64_t>(std::numeric_limits<int>::max()
			- files.piece_length()) * files.piece_length())
		{
			ec = errors::too_many_pieces_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		files.set_num_pieces(int((files.total_size() + files.piece_length() - 1)
			/ files.piece_length()));

		bdecode_node const pieces = info.dict_find_string("pieces");
		bdecode_node const root_hash = info.dict_find_string("root hash");
		if (!pieces && !root_hash)
		{
			ec = errors::torrent_missing_pieces;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		// we expect the piece hashes to be < 2 GB in size
		if (files.num_pieces() >= std::numeric_limits<int>::max() / 20
			|| files.num_pieces() > max_pieces)
		{
			ec = errors::too_many_pieces_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		if (pieces)
		{
			if (pieces.string_length() != files.num_pieces() * 20)
			{
				ec = errors::torrent_invalid_hashes;
				// mark the torrent as invalid
				m_files.set_piece_length(0);
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
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}
			if (files.num_pieces() <= 0)
			{
				ec = errors::no_files_in_torrent;
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}
			int const num_leafs = merkle_num_leafs(files.num_pieces());
			int const num_nodes = merkle_num_nodes(num_leafs);
			m_merkle_first_leaf = num_nodes - num_leafs;
			m_merkle_tree.resize(num_nodes);
			m_merkle_tree[0].assign(root_hash.string_ptr());
		}

		m_flags |= (info.dict_find_int_value("private", 0) != 0)
			? private_torrent : 0;

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node const similar = info.dict_find_list("similar");
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

		bdecode_node const collections = info.dict_find_list("collections");
		if (collections)
		{
			for (int i = 0; i < collections.list_size(); ++i)
			{
				bdecode_node const str = collections.list_at(i);

				if (str.type() != bdecode_node::string_t) continue;

				m_collections.emplace_back(str.string_ptr()
					+ info_ptr_diff, str.string_length());
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		if (info.dict_find_string("ssl-cert"))
			m_flags |= ssl_torrent;

		// now, commit the files structure we just parsed out
		// into the torrent_info object.
		m_files.swap(files);
		return true;
	}

	bdecode_node torrent_info::info(char const* key) const
	{
		if (m_info_dict.type() == bdecode_node::none_t)
		{
			error_code ec;
			bdecode(m_info_section.get(), m_info_section.get()
				+ m_info_section_size, m_info_dict, ec);
			if (ec) return bdecode_node();
		}
		return m_info_dict.dict_find(key);
	}


	bool torrent_info::add_merkle_nodes(std::map<int, sha1_hash> const& subtree
		, piece_index_t const piece)
	{
		INVARIANT_CHECK;

		int n = m_merkle_first_leaf + static_cast<int>(piece);
		auto const it = subtree.find(n);
		if (it == subtree.end()) return false;
		sha1_hash h = it->second;

		// if the verification passes, these are the
		// nodes to add to our tree
		std::map<int, sha1_hash> to_add;

		while (n > 0)
		{
			int const sibling = merkle_get_sibling(n);
			int const parent = merkle_get_parent(n);
			auto const sibling_hash = subtree.find(sibling);
			if (sibling_hash == subtree.end())
				return false;
			to_add[n] = h;
			to_add[sibling] = sibling_hash->second;
			hasher hs;
			if (sibling < n)
			{
				hs.update(sibling_hash->second);
				hs.update(h);
			}
			else
			{
				hs.update(h);
				hs.update(sibling_hash->second);
			}
			h = hs.final();
			n = parent;
		}
		if (h != m_merkle_tree[0]) return false;

		// the nodes and piece hash matched the root-hash
		// insert them into our tree

		for (auto const& i : to_add)
		{
			m_merkle_tree[i.first] = i.second;
		}
		return true;
	}

	void torrent_info::internal_set_creator(string_view const c)
	{ m_created_by = std::string(c); }

	void torrent_info::internal_set_creation_date(std::time_t const t)
	{ m_creation_date = t; }

	void torrent_info::internal_set_comment(string_view const s)
	{ m_comment = std::string(s); }

	// builds a list of nodes that are required to verify
	// the given piece
	std::map<int, sha1_hash>
	torrent_info::build_merkle_list(piece_index_t const piece) const
	{
		INVARIANT_CHECK;

		std::map<int, sha1_hash> ret;
		int n = m_merkle_first_leaf + static_cast<int>(piece);
		ret[n] = m_merkle_tree[n];
		ret[0] = m_merkle_tree[0];
		while (n > 0)
		{
			int sibling = merkle_get_sibling(n);
			int parent = merkle_get_parent(n);
			ret[sibling] = m_merkle_tree[sibling];
			// we cannot build the tree path if one
			// of the nodes in the tree is missing
			TORRENT_ASSERT(!m_merkle_tree[sibling].is_all_zeros());
			n = parent;
		}
		return ret;
	}

	bool torrent_info::parse_torrent_file(bdecode_node const& torrent_file
		, error_code& ec)
	{
		return parse_torrent_file(torrent_file, ec, default_piece_limit);
	}

	bool torrent_info::parse_torrent_file(bdecode_node const& torrent_file
		, error_code& ec, int const piece_limit)
	{
		if (torrent_file.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_is_no_dict;
			return false;
		}

		bdecode_node const info = torrent_file.dict_find_dict("info");
		if (!info)
		{
			bdecode_node const uri = torrent_file.dict_find_string("magnet-uri");
			if (uri)
			{
				auto const p = parse_magnet_uri(uri.string_value(), ec);
				if (ec) return false;

				m_info_hash = p.info_hash;
				m_urls.reserve(m_urls.size() + p.trackers.size());
				for (auto const& url : p.trackers)
					m_urls.emplace_back(url);

				return true;
			}

			ec = errors::torrent_missing_info;
			return false;
		}
		if (!parse_info_section(info, ec, piece_limit)) return false;
		resolve_duplicate_filenames();

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node const similar = torrent_file.dict_find_list("similar");
		if (similar)
		{
			for (int i = 0; i < similar.list_size(); ++i)
			{
				if (similar.list_at(i).type() != bdecode_node::string_t)
					continue;

				if (similar.list_at(i).string_length() != 20)
					continue;

				m_owned_similar_torrents.emplace_back(
					similar.list_at(i).string_ptr());
			}
		}

		bdecode_node const collections = torrent_file.dict_find_list("collections");
		if (collections)
		{
			for (int i = 0; i < collections.list_size(); ++i)
			{
				bdecode_node const str = collections.list_at(i);

				if (str.type() != bdecode_node::string_t) continue;

				m_owned_collections.emplace_back(str.string_ptr()
					, aux::numeric_cast<std::size_t>(str.string_length()));
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		// extract the url of the tracker
		bdecode_node const announce_node = torrent_file.dict_find_list("announce-list");
		if (announce_node)
		{
			m_urls.reserve(announce_node.list_size());
			for (int j = 0, end(announce_node.list_size()); j < end; ++j)
			{
				bdecode_node const tier = announce_node.list_at(j);
				if (tier.type() != bdecode_node::list_t) continue;
				for (int k = 0, end2(tier.list_size()); k < end2; ++k)
				{
					announce_entry e(tier.list_string_value_at(k).to_string());
					e.trim();
					if (e.url.empty()) continue;
					e.tier = std::uint8_t(j);
					e.fail_limit = 0;
					e.source = announce_entry::source_torrent;
#if TORRENT_USE_I2P
					if (is_i2p_url(e.url)) m_flags |= i2p;
#endif
					m_urls.push_back(e);
				}
			}

			if (!m_urls.empty())
			{
				// shuffle each tier
				aux::random_shuffle(m_urls);
				std::stable_sort(m_urls.begin(), m_urls.end()
					, [](announce_entry const& lhs, announce_entry const& rhs)
					{ return lhs.tier < rhs.tier; });
			}
		}

		if (m_urls.empty())
		{
			announce_entry e(torrent_file.dict_find_string_value("announce"));
			e.fail_limit = 0;
			e.source = announce_entry::source_torrent;
			e.trim();
#if TORRENT_USE_I2P
			if (is_i2p_url(e.url)) m_flags |= i2p;
#endif
			if (!e.url.empty()) m_urls.push_back(e);
		}

		bdecode_node const nodes = torrent_file.dict_find_list("nodes");
		if (nodes)
		{
			for (int i = 0, end(nodes.list_size()); i < end; ++i)
			{
				bdecode_node const n = nodes.list_at(i);
				if (n.type() != bdecode_node::list_t
					|| n.list_size() < 2
					|| n.list_at(0).type() != bdecode_node::string_t
					|| n.list_at(1).type() != bdecode_node::int_t)
					continue;
				m_nodes.emplace_back(
					n.list_at(0).string_value().to_string()
					, int(n.list_at(1).int_value()));
			}
		}

		// extract creation date
		std::int64_t const cd = torrent_file.dict_find_int_value("creation date", -1);
		if (cd >= 0)
		{
			m_creation_date = std::time_t(cd);
		}

		// if there are any url-seeds, extract them
		bdecode_node const url_seeds = torrent_file.dict_find("url-list");
		if (url_seeds && url_seeds.type() == bdecode_node::string_t
			&& url_seeds.string_length() > 0)
		{
			web_seed_entry ent(maybe_url_encode(url_seeds.string_value().to_string())
				, web_seed_entry::url_seed);
			if ((m_flags & multifile) && num_files() > 1)
				ensure_trailing_slash(ent.url);
			m_web_seeds.push_back(ent);
		}
		else if (url_seeds && url_seeds.type() == bdecode_node::list_t)
		{
			// only add a URL once
			std::set<std::string> unique;
			for (int i = 0, end(url_seeds.list_size()); i < end; ++i)
			{
				bdecode_node const url = url_seeds.list_at(i);
				if (url.type() != bdecode_node::string_t) continue;
				if (url.string_length() == 0) continue;
				web_seed_entry ent(maybe_url_encode(url.string_value().to_string())
					, web_seed_entry::url_seed);
				if ((m_flags & multifile) && num_files() > 1)
					ensure_trailing_slash(ent.url);
				if (!unique.insert(ent.url).second) continue;
				m_web_seeds.push_back(ent);
			}
		}

		// if there are any http-seeds, extract them
		bdecode_node const http_seeds = torrent_file.dict_find("httpseeds");
		if (http_seeds && http_seeds.type() == bdecode_node::string_t
			&& http_seeds.string_length() > 0)
		{
			m_web_seeds.emplace_back(maybe_url_encode(http_seeds.string_value().to_string())
				, web_seed_entry::http_seed);
		}
		else if (http_seeds && http_seeds.type() == bdecode_node::list_t)
		{
			// only add a URL once
			std::set<std::string> unique;
			for (int i = 0, end(http_seeds.list_size()); i < end; ++i)
			{
				bdecode_node const url = http_seeds.list_at(i);
				if (url.type() != bdecode_node::string_t || url.string_length() == 0) continue;
				std::string const u = maybe_url_encode(url.string_value().to_string());
				if (!unique.insert(u).second) continue;
				m_web_seeds.emplace_back(u, web_seed_entry::http_seed);
			}
		}

		m_comment = torrent_file.dict_find_string_value("comment.utf-8").to_string();
		if (m_comment.empty()) m_comment = torrent_file.dict_find_string_value("comment").to_string();
		verify_encoding(m_comment);

		m_created_by = torrent_file.dict_find_string_value("created by.utf-8").to_string();
		if (m_created_by.empty()) m_created_by = torrent_file.dict_find_string_value("created by").to_string();
		verify_encoding(m_created_by);

		return true;
	}

	void torrent_info::add_tracker(std::string const& url, int const tier)
	{
		add_tracker(url, tier, announce_entry::source_client);
	}

	void torrent_info::add_tracker(std::string const& url, int const tier
		, announce_entry::tracker_source const source)
	{
		auto const i = std::find_if(m_urls.begin(), m_urls.end()
			, [&url](announce_entry const& ae) { return ae.url == url; });
		if (i != m_urls.end()) return;

		announce_entry e(url);
		e.tier = std::uint8_t(tier);
		e.source = source;
		m_urls.push_back(e);

		std::sort(m_urls.begin(), m_urls.end()
			, [] (announce_entry const& lhs, announce_entry const& rhs)
			{ return lhs.tier < rhs.tier; });
	}

#if TORRENT_ABI_VERSION == 1
namespace {

		struct filter_web_seed_type
		{
			explicit filter_web_seed_type(web_seed_entry::type_t t_) : t(t_) {}
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

	bool torrent_info::parse_info_section(lazy_entry const& le, error_code& ec)
	{
		if (le.type() == lazy_entry::none_t) return false;
		std::pair<char const*, int> buf = le.data_section();
		bdecode_node e;
		if (bdecode(buf.first, buf.first + buf.second, e, ec) != 0)
			return false;

		return parse_info_section(e, ec);
	}

#endif // TORRENT_ABI_VERSION

	void torrent_info::add_url_seed(std::string const& url
		, std::string const& ext_auth
		, web_seed_entry::headers_t const& ext_headers)
	{
		m_web_seeds.emplace_back(url, web_seed_entry::url_seed
			, ext_auth, ext_headers);
	}

	void torrent_info::add_http_seed(std::string const& url
		, std::string const& auth
		, web_seed_entry::headers_t const& extra_headers)
	{
		m_web_seeds.emplace_back(url, web_seed_entry::http_seed
			, auth, extra_headers);
	}

	void torrent_info::set_web_seeds(std::vector<web_seed_entry> seeds)
	{
		m_web_seeds = std::move(seeds);
	}

	std::vector<sha1_hash> torrent_info::similar_torrents() const
	{
		std::vector<sha1_hash> ret;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		ret.reserve(m_similar_torrents.size() + m_owned_similar_torrents.size());

		for (auto const& st : m_similar_torrents)
			ret.emplace_back(st);

		for (auto const& st : m_owned_similar_torrents)
			ret.push_back(st);
#endif

		return ret;
	}

	std::vector<std::string> torrent_info::collections() const
	{
		std::vector<std::string> ret;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		ret.reserve(m_collections.size() + m_owned_collections.size());

		for (auto const& c : m_collections)
			ret.emplace_back(c.first, aux::numeric_cast<std::size_t>(c.second));

		for (auto const& c : m_owned_collections)
			ret.push_back(c);
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		return ret;
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void torrent_info::check_invariant() const
	{
		for (auto const i : m_files.file_range())
		{
			TORRENT_ASSERT(m_files.file_name_ptr(i) != nullptr);
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

		if (m_piece_hashes != nullptr)
		{
			TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
			TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
		}
	}
#endif

}
