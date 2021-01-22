/*

Copyright (c) 2003-2020, Arvid Norberg
Copyright (c) 2016-2017, Pavel Pimenov
Copyright (c) 2016-2019, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2016, 2019, Andrei Kurushin
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
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/string_util.hpp" // is_space, is_i2p_url
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/escape_string.hpp" // maybe_url_encode
#include "libtorrent/aux_/merkle.hpp" // for merkle_*
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/file_pointer.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/span.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <cinttypes>
#include <iterator>
#include <algorithm>
#include <set>
#include <ctime>
#include <array>

namespace libtorrent {

	TORRENT_EXPORT from_span_t from_span;

	constexpr torrent_info_flags_t torrent_info::multifile;
	constexpr torrent_info_flags_t torrent_info::private_torrent;
	constexpr torrent_info_flags_t torrent_info::i2p;
	constexpr torrent_info_flags_t torrent_info::ssl_torrent;
	constexpr torrent_info_flags_t torrent_info::v2_has_piece_hashes;

	namespace {

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

namespace aux {

	// fixes invalid UTF-8 sequences
	bool verify_encoding(std::string& target)
	{
		if (target.empty()) return true;

		std::string tmp_path;
		tmp_path.reserve(target.size()+5);
		bool valid_encoding = true;

		string_view ptr = target;
		while (!ptr.empty())
		{
			std::int32_t codepoint;
			int len;

			// decode a single utf-8 character
			std::tie(codepoint, len) = parse_utf8_codepoint(ptr);

			// this was the last character, and nothing was
			// written to the destination buffer (i.e. the source character was
			// truncated)
			if (codepoint == -1)
			{
				codepoint = '_';
				valid_encoding = false;
			}

			ptr = ptr.substr(std::min(std::size_t(len), ptr.size()));

			// encode codepoint into utf-8
			append_utf8_codepoint(tmp_path, codepoint);
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
				i = std::size_t(dot - seq_len);
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

	bool extract_single_file2(bdecode_node const& dict, file_storage& files
		, std::string const& path, string_view const name
		, std::ptrdiff_t const info_offset, char const* info_buffer
		, error_code& ec)
	{
		if (dict.type() != bdecode_node::dict_t) return false;

		file_flags_t file_flags = get_file_attributes(dict);

		if (file_flags & file_storage::flag_pad_file)
		{
			ec = errors::torrent_invalid_pad_file;
			return false;
		}

		// symlinks have an implied "size" of zero. i.e. they use up 0 bytes of
		// the torrent payload space
		std::int64_t const file_size = (file_flags & file_storage::flag_symlink)
			? 0 : dict.dict_find_int_value("length", -1);

		// if a file is too big, it will cause integer overflow in our
		// calculations of the size of the merkle tree (which is all 'int'
		// indices)
		if (file_size < 0
			|| (file_size / default_block_size) >= std::numeric_limits<int>::max() / 2
			|| file_size > file_storage::max_file_size)
		{
			ec = errors::torrent_invalid_length;
			return false;
		}

		std::time_t const mtime = std::time_t(dict.dict_find_int_value("mtime", 0));

		char const* pieces_root = nullptr;

		std::string symlink_path;
		if (file_flags & file_storage::flag_symlink)
		{
			if (bdecode_node const s_p = dict.dict_find_list("symlink path"))
			{
				auto const preallocate = static_cast<std::size_t>(path_length(s_p, ec));
				if (ec) return false;
				symlink_path.reserve(preallocate);
				for (int i = 0, end(s_p.list_size()); i < end; ++i)
				{
					auto pe = s_p.list_at(i).string_value();
					aux::sanitize_append_path_element(symlink_path, pe);
				}
			}
		}

		if (symlink_path.empty())
		{
			bdecode_node const root = dict.dict_find_string("pieces root");
			if (!root || root.type() != bdecode_node::string_t
				|| root.string_length() != sha256_hash::size())
			{
				ec = errors::torrent_missing_pieces_root;
				return false;
			}
			pieces_root = info_buffer + (root.string_offset() - info_offset);
			if (file_size > 0 && sha256_hash(pieces_root).is_all_zeros())
			{
				ec = errors::torrent_missing_pieces_root;
				return false;
			}
		}

		files.add_file_borrow(ec, name, path, file_size, file_flags, nullptr
			, mtime, symlink_path, pieces_root);
		return !ec;
	}

	// 'top_level' is extracting the file for a single-file torrent. The
	// distinction is that the filename is found in "name" rather than
	// "path"
	// root_dir is the name of the torrent, unless this is a single file
	// torrent, in which case it's empty.
	bool extract_single_file(bdecode_node const& dict, file_storage& files
		, std::string const& root_dir, std::ptrdiff_t const info_offset
		, char const* info_buffer, bool top_level, error_code& ec)
	{
		if (dict.type() != bdecode_node::dict_t) return false;

		file_flags_t file_flags = get_file_attributes(dict);

		// symlinks have an implied "size" of zero. i.e. they use up 0 bytes of
		// the torrent payload space
		std::int64_t const file_size = (file_flags & file_storage::flag_symlink)
			? 0 : dict.dict_find_int_value("length", -1);

		// if a file is too big, it will cause integer overflow in our
		// calculations of the size of the merkle tree (which is all 'int'
		// indices)
		if (file_size < 0
			|| (file_size / default_block_size) >= std::numeric_limits<int>::max() / 2
			|| file_size > file_storage::max_file_size)
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

			filename = { info_buffer + (p.string_offset() - info_offset)
				, static_cast<std::size_t>(p.string_length())};

			while (!filename.empty() && filename.front() == TORRENT_SEPARATOR)
				filename.remove_prefix(1);

			aux::sanitize_append_path_element(path, p.string_value());
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
						filename = {info_buffer + (e.string_offset() - info_offset)
							, static_cast<std::size_t>(e.string_length()) };
						while (!filename.empty() && filename.front() == TORRENT_SEPARATOR)
							filename.remove_prefix(1);
					}
					aux::sanitize_append_path_element(path, e.string_value());
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
				char cnt[20];
				std::snprintf(cnt, sizeof(cnt), "%" PRIu64, file_size);
				path = combine_path(".pad", cnt);
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
			filehash = info_buffer + (fh.string_offset() - info_offset);

		std::string symlink_path;
		if (file_flags & file_storage::flag_symlink)
		{
			if (bdecode_node const s_p = dict.dict_find_list("symlink path"))
			{
				auto const preallocate = static_cast<std::size_t>(path_length(s_p, ec));
				if (ec) return false;
				symlink_path.reserve(preallocate);
				for (int i = 0, end(s_p.list_size()); i < end; ++i)
				{
					auto pe = s_p.list_at(i).string_value();
					aux::sanitize_append_path_element(symlink_path, pe);
				}
			}
			else
			{
				// technically this is an invalid torrent. "symlink path" must exist
				file_flags &= ~file_storage::flag_symlink;
			}
			// symlink targets are validated later, as it may point to a file or
			// directory we haven't parsed yet
		}

		if (filename.size() > path.length()
			|| path.substr(path.size() - filename.size()) != filename)
		{
			// if the filename was sanitized and differ, clear it to just use path
			filename = {};
		}

		files.add_file_borrow(ec, filename, path, file_size, file_flags, filehash
			, mtime, symlink_path);
		return !ec;
	}

	bool extract_files2(bdecode_node const& tree, file_storage& target
		, std::string const& root_dir, ptrdiff_t const info_offset
		, char const* info_buffer
		, bool const has_files, int const depth, error_code& ec)
	{
		if (tree.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}

		// since we're parsing this recursively, we have to be careful not to blow
		// up the stack. 100 levels of sub directories should be enough. This
		// could be improved by an iterative parser, keeping the state on a more
		// compact side-stack
		if (depth > 100)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}

		for (int i = 0; i < tree.dict_size(); ++i)
		{
			auto e = tree.dict_at_node(i);
			if (e.second.type() != bdecode_node::dict_t || e.first.string_value().empty())
			{
				ec = errors::torrent_file_parse_failed;
				return false;
			}

			string_view filename = { info_buffer + (e.first.string_offset() - info_offset)
				, static_cast<size_t>(e.first.string_length()) };
			while (!filename.empty() && filename.front() == TORRENT_SEPARATOR)
				filename.remove_prefix(1);

			bool const leaf_node = e.second.dict_size() == 1 && e.second.dict_at(0).first.empty();
			bool const single_file = leaf_node && !has_files && tree.dict_size() == 1;

			std::string path = single_file ? std::string() : root_dir;
			aux::sanitize_append_path_element(path, filename);

			if (leaf_node)
			{
				if (filename.size() > path.length()
					|| path.substr(path.size() - filename.size()) != filename)
				{
					// if the filename was sanitized and differ, clear it to just use path
					filename = {};
				}

				if (!extract_single_file2(e.second.dict_at(0).second, target
					, path, filename, info_offset, info_buffer, ec))
				{
					return false;
				}
				continue;
			}

			if (!extract_files2(e.second, target, path, info_offset, info_buffer
				, true, depth + 1, ec))
			{
				return false;
			}
		}

		return true;
	}

	// root_dir is the name of the torrent, unless this is a single file
	// torrent, in which case it's empty.
	bool extract_files(bdecode_node const& list, file_storage& target
		, std::string const& root_dir, std::ptrdiff_t info_offset
		, char const* info_buffer, error_code& ec)
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
				, info_offset, info_buffer, false, ec))
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
#ifdef TORRENT_WINDOWS
		aux::file_pointer f(::_wfopen(convert_to_native_path_string(filename).c_str(), L"rb"));
#else
		aux::file_pointer f(std::fopen(filename.c_str(), "rb"));
#endif
		if (f.file() == nullptr)
		{
			ec.assign(errno, generic_category());
			return -1;
		}

		if (std::fseek(f.file(), 0, SEEK_END) < 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		std::int64_t const s = std::ftell(f.file());
		if (s < 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		if (s > max_buffer_size)
		{
			ec = errors::metadata_too_large;
			return -1;
		}
		if (std::fseek(f.file(), 0, SEEK_SET) < 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		v.resize(std::size_t(s));
		if (s == 0) return 0;
		std::size_t const read = std::fread(v.data(), 1, v.size(), f.file());
		if (read != std::size_t(s))
		{
			if (std::feof(f.file()))
			{
				v.resize(read);
				return 0;
			}
			ec.assign(errno, generic_category());
			return -1;
		}
		return 0;
	}

} // anonymous namespace

	web_seed_entry::web_seed_entry(std::string url_, type_t type_
		, std::string auth_
		, headers_t extra_headers_)
		: url(std::move(url_))
		, auth(std::move(auth_))
		, extra_headers(std::move(extra_headers_))
		, type(std::uint8_t(type_))
	{
	}

TORRENT_VERSION_NAMESPACE_3

	torrent_info::torrent_info(torrent_info const&) = default;
	torrent_info& torrent_info::operator=(torrent_info&&) = default;

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
		if (!parse_torrent_file(e, ec, load_torrent_limits{}.max_pieces))
			aux::throw_ex<system_error>(ec);
#else
		parse_torrent_file(e, ec, load_torrent_limits{}.max_pieces);
#endif
		INVARIANT_CHECK;
	}
#endif // TORRENT_ABI_VERSION

#ifndef BOOST_NO_EXCEPTIONS
	torrent_info::torrent_info(bdecode_node const& torrent_file)
	{
		error_code ec;
		if (!parse_torrent_file(torrent_file, ec, load_torrent_limits{}.max_pieces))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(span<char const> buffer, from_span_t)
	{
		error_code ec;
		bdecode_node e = bdecode(buffer, ec);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec, load_torrent_limits{}.max_pieces))
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

		if (!parse_torrent_file(e, ec, load_torrent_limits{}.max_pieces))
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
#endif

	file_storage const& torrent_info::orig_files() const
	{
		TORRENT_ASSERT(is_loaded());
		return m_orig_files ? *m_orig_files : m_files;
	}

	void torrent_info::rename_file(file_index_t index, std::string const& new_filename)
	{
		TORRENT_ASSERT(is_loaded());
		if (m_files.file_path(index) == new_filename) return;
		copy_on_write();
		m_files.rename_file(index, new_filename);
	}

	torrent_info::torrent_info(bdecode_node const& torrent_file
		, error_code& ec)
	{
		parse_torrent_file(torrent_file, ec, load_torrent_limits{}.max_pieces);
		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(span<char const> buffer
		, error_code& ec, from_span_t)
	{
		bdecode_node e = bdecode(buffer, ec);
		if (ec) return;
		parse_torrent_file(e, ec, load_torrent_limits{}.max_pieces);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, error_code& ec)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) return;

		bdecode_node e = bdecode(buf, ec);
		if (ec) return;
		parse_torrent_file(e, ec, load_torrent_limits{}.max_pieces);

		INVARIANT_CHECK;
	}

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(info_hash_t const& info_hash)
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

#if TORRENT_ABI_VERSION <= 2
	void torrent_info::swap(torrent_info& ti)
	{
		INVARIANT_CHECK;

		torrent_info tmp = std::move(ti);
		ti = std::move(*this);
		*this = std::move(tmp);
	}

	boost::shared_array<char> torrent_info::metadata() const
	{
		boost::shared_array<char> ret(new char[std::size_t(m_info_section_size)]);
		std::memcpy(ret.get(), m_info_section.get(), std::size_t(m_info_section_size));
		return ret;
	}
#endif

	string_view torrent_info::ssl_cert() const
	{
		if (!(m_flags & ssl_torrent)) return "";

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

#if TORRENT_ABI_VERSION < 3
	bool torrent_info::parse_info_section(bdecode_node const& info, error_code& ec)
	{
		return parse_info_section(info, ec, 0x200000);
	}
#endif

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
		m_info_hash.v1 = hasher(section).final();
		m_info_hash.v2 = hasher256(section).final();
		if (info.data_section().size() >= std::numeric_limits<int>::max())
		{
			ec = errors::metadata_too_large;
			return false;
		}

		if (section.empty() || section[0] != 'd' || section[section.size() - 1] != 'e')
		{
			ec = errors::invalid_bencoding;
			return false;
		}

		// copy the info section
		m_info_section_size = int(section.size());
		m_info_section.reset(new char[aux::numeric_cast<std::size_t>(m_info_section_size)]);
		std::memcpy(m_info_section.get(), section.data(), aux::numeric_cast<std::size_t>(m_info_section_size));

		// this is the offset from the start of the torrent file buffer to the
		// info-dictionary (within the torrent file).
		// we need this because we copy just the info dictionary buffer and pull
		// out parsed data (strings) from the bdecode_node and need to make them
		// point into our copy of the buffer.
		std::ptrdiff_t const info_offset = info.data_offset();

		// check for a version key
		int const version = int(info.dict_find_int_value("meta version", -1));
		if (version > 0)
		{
			char error_string[200];
			if (info.has_soft_error(error_string))
			{
				ec = errors::invalid_bencoding;
				return false;
			}

			if (version > 2)
			{
				ec = errors::torrent_unknown_version;
				return false;
			}
		}

		if (version < 2)
		{
			// this is a v1 torrent so the v2 info hash has no meaning
			// clear it just to make sure no one tries to use it
			m_info_hash.v2.clear();
		}

		// extract piece length
		std::int64_t const piece_length = info.dict_find_int_value("piece length", -1);
		// limit the piece length at INT_MAX / 2 to get a bit of headroom. We
		// commonly compute the number of blocks per pieces by adding
		// block_size - 1 before dividing by block_size. That would overflow with
		// a piece size of INT_MAX. This limit is still an unreasonably large
		// piece size anyway.
		if (piece_length <= 0 || piece_length > std::numeric_limits<int>::max() / 2)
		{
			ec = errors::torrent_missing_piece_length;
			return false;
		}

		// according to BEP 52: "It must be a power of two and at least 16KiB."
		if (version > 1 && (piece_length < default_block_size
			|| (piece_length & (piece_length - 1)) != 0))
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
		aux::sanitize_append_path_element(name, name_ent.string_value());
		if (name.empty())
		{
			if (m_info_hash.has_v1())
				name = aux::to_hex(m_info_hash.v1);
			else
				name = aux::to_hex(m_info_hash.v2);
		}

		// extract file list

		// save a copy so that we can extract both v1 and v2 files then compare the results
		file_storage v1_files;
		if (version >= 2)
			v1_files = files;

		bdecode_node const files_node = info.dict_find_list("files");

		bdecode_node file_tree_node = info.dict_find_dict("file tree");
		if (version >= 2 && file_tree_node)
		{
			if (!extract_files2(file_tree_node, files, name, info_offset
				, m_info_section.get(), bool(files_node), 0, ec))
			{
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}

			files.sanitize_symlinks();
			if (files.num_files() > 1)
				m_flags |= multifile;
			else
				m_flags &= ~multifile;
		}
		else if (version >= 2)
		{
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			ec = errors::torrent_missing_file_tree;
			return false;
		}
		else if (file_tree_node)
		{
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			ec = errors::torrent_missing_meta_version;
			return false;
		}

		if (!files_node)
		{
			// if this is a v2 torrent it is ok for the length key to be missing
			// that means it is a v2 only torrent
			if (version < 2 || info.dict_find("length"))
			{
				// if there's no list of files, there has to be a length
				// field.
				if (!extract_single_file(info, version == 2 ? v1_files : files, ""
					, info_offset, m_info_section.get(), true, ec))
				{
					// mark the torrent as invalid
					m_files.set_piece_length(0);
					return false;
				}

				m_flags &= ~multifile;
			}
			else
			{
				// this is a v2 only torrent so clear the v1 info hash to make sure no one uses it
				m_info_hash.v1.clear();
			}
		}
		else
		{
			if (!extract_files(files_node, version == 2 ? v1_files : files, name
				, info_offset, m_info_section.get(), ec))
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

		if (version >= 2
			&& v1_files.num_files() > 0
			&& !aux::files_equal(files, v1_files))
		{
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			ec = errors::torrent_inconsistent_files;
			return false;
		}

		// extract SHA-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		if (files.total_size() / files.piece_length() >=
			std::numeric_limits<int>::max())
		{
			ec = errors::too_many_pieces_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		files.set_num_pieces(int((files.total_size() + files.piece_length() - 1)
			/ files.piece_length()));

		// we expect the piece hashes to be < 2 GB in size
		if (files.num_pieces() >= std::numeric_limits<int>::max() / 20
			|| files.num_pieces() > max_pieces)
		{
			ec = errors::too_many_pieces_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		bdecode_node const pieces = info.dict_find_string("pieces");
		if (!pieces)
		{
			if (version < 2)
			{
				ec = errors::torrent_missing_pieces;
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}
		}
		else
		{
			if (pieces.string_length() != files.num_pieces() * 20)
			{
				ec = errors::torrent_invalid_hashes;
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return false;
			}

			std::ptrdiff_t const hash_offset = pieces.string_offset() - info_offset;
			TORRENT_ASSERT(hash_offset < std::numeric_limits<std::int32_t>::max());
			TORRENT_ASSERT(hash_offset >= 0);
			m_piece_hashes = static_cast<std::int32_t>(hash_offset);
			TORRENT_ASSERT(m_piece_hashes > 0);
			TORRENT_ASSERT(m_piece_hashes < m_info_section_size);
		}

		m_flags |= (info.dict_find_int_value("private", 0) != 0)
			? private_torrent : torrent_info_flags_t{};

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
				m_similar_torrents.push_back(static_cast<std::int32_t>(
					similar.list_at(i).string_offset() - info_offset));
			}
		}

		bdecode_node const collections = info.dict_find_list("collections");
		if (collections)
		{
			for (int i = 0; i < collections.list_size(); ++i)
			{
				bdecode_node const str = collections.list_at(i);

				if (str.type() != bdecode_node::string_t) continue;

				m_collections.emplace_back(std::int32_t(str.string_offset() - info_offset)
					, str.string_length());
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		if (info.dict_find_string("ssl-cert"))
			m_flags |= ssl_torrent;

		if (files.total_size() == 0)
		{
			ec = errors::torrent_invalid_length;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

		// now, commit the files structure we just parsed out
		// into the torrent_info object.
		m_files.swap(files);

		TORRENT_ASSERT(m_info_hash.has_v2() == m_files.v2());
		return true;
	}

	bool torrent_info::parse_piece_layers(bdecode_node const& e, error_code& ec)
	{
		std::map<sha256_hash, string_view> piece_layers;

		if (!e || e.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_missing_piece_layer;
			return false;
		}

		for (int i = 0; i < e.dict_size(); ++i)
		{
			auto const f = e.dict_at(i);
			if (f.first.size() != static_cast<std::size_t>(sha256_hash::size())
				|| f.second.type() != bdecode_node::string_t
				|| f.second.string_length() % sha256_hash::size() != 0)
				continue;

			piece_layers.emplace(sha256_hash(f.first), f.second.string_value());
		}

		m_piece_layers.resize(orig_files().num_files());

		for (file_index_t i : orig_files().file_range())
		{
			if (orig_files().file_size(i) <= orig_files().piece_length())
				continue;

			auto const piece_layer = piece_layers.find(orig_files().root(i));
			if (piece_layer == piece_layers.end())
			{
				ec = errors::torrent_missing_piece_layer;
				return false;
			}

			int const num_pieces = orig_files().file_num_pieces(i);

			if (ptrdiff_t(piece_layer->second.size()) != num_pieces * sha256_hash::size())
			{
				ec = errors::torrent_invalid_piece_layer;
				return false;
			}

			auto const hashes = piece_layer->second;
			if ((hashes.size() % sha256_hash::size()) != 0)
			{
				ec = errors::torrent_invalid_piece_layer;
				return false;
			}

			m_piece_layers[i].assign(hashes.begin(), hashes.end());
		}

		m_flags |= v2_has_piece_hashes;
		return true;
	}

	span<char const> torrent_info::piece_layer(file_index_t f) const
	{
		TORRENT_ASSERT_PRECOND(f >= file_index_t(0));
		if (f >= m_piece_layers.end_index()) return {};
		if (m_files.pad_file_at(f)) return {};

		if (m_files.file_size(f) <= piece_length())
		{
			auto const root_ptr = m_files.root_ptr(f);
			if (root_ptr == nullptr) return {};
			return {root_ptr, lt::sha256_hash::size()};
		}
		return m_piece_layers[f];
	}

	void torrent_info::free_piece_layers()
	{
		m_piece_layers.clear();
		m_piece_layers.shrink_to_fit();

		m_flags &= ~v2_has_piece_hashes;
	}

	void torrent_info::internal_set_creator(string_view const c)
	{ m_created_by = std::string(c); }

	void torrent_info::internal_set_creation_date(std::time_t const t)
	{ m_creation_date = t; }

	void torrent_info::internal_set_comment(string_view const s)
	{ m_comment = std::string(s); }

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

				m_info_hash = p.info_hashes;
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

		if (m_info_hash.has_v2() && !parse_piece_layers(torrent_file.dict_find_dict("piece layers"), ec))
		{
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return false;
		}

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
					ltrim(e.url);
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
			ltrim(e.url);
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
			m_web_seeds.push_back(std::move(ent));
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
				m_web_seeds.push_back(std::move(ent));
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
				std::string u = maybe_url_encode(url.string_value().to_string());
				if (!unique.insert(u).second) continue;
				m_web_seeds.emplace_back(std::move(u), web_seed_entry::http_seed);
			}
		}

		m_comment = torrent_file.dict_find_string_value("comment.utf-8").to_string();
		if (m_comment.empty()) m_comment = torrent_file.dict_find_string_value("comment").to_string();
		aux::verify_encoding(m_comment);

		m_created_by = torrent_file.dict_find_string_value("created by.utf-8").to_string();
		if (m_created_by.empty()) m_created_by = torrent_file.dict_find_string_value("created by").to_string();
		aux::verify_encoding(m_created_by);

		return true;
	}

	void torrent_info::add_tracker(std::string const& url, int const tier)
	{
		add_tracker(url, tier, announce_entry::source_client);
	}

	void torrent_info::add_tracker(std::string const& url, int const tier
		, announce_entry::tracker_source const source)
	{
		TORRENT_ASSERT_PRECOND(!url.empty());
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

	void torrent_info::clear_trackers()
	{
		m_urls.clear();
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
			ret.emplace_back(m_info_section.get() + st);

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
			ret.emplace_back(m_info_section.get() + c.first, aux::numeric_cast<std::size_t>(c.second));

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
			TORRENT_ASSERT(m_files.file_name(i).data() != nullptr);
			if (!m_files.owns_name(i))
			{
				// name needs to point into the allocated info section buffer
				TORRENT_ASSERT(m_files.file_name(i).data() >= m_info_section.get());
				TORRENT_ASSERT(m_files.file_name(i).data() < m_info_section.get() + m_info_section_size);
			}
			else
			{
				// name must be a null terminated string
				string_view const name = m_files.file_name(i);
				TORRENT_ASSERT(name.data()[name.size()] == '\0');
			}
		}

		TORRENT_ASSERT(m_piece_hashes <= m_info_section_size);
	}
#endif

	sha1_hash torrent_info::info_hash() const noexcept
	{
		return m_info_hash.get_best();
	}

	bool torrent_info::v1() const { return m_piece_hashes != 0; }
	bool torrent_info::v2() const { return !m_piece_layers.empty(); }

TORRENT_VERSION_NAMESPACE_3_END

}
