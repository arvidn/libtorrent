/*

Copyright (c) 2008-2016, Arvid Norberg
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

#include "libtorrent/create_torrent.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_to_wstring
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/aux_/merkle.hpp" // for merkle_*()
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/file.hpp" // for combine_path etc.

#include <sys/types.h>
#include <sys/stat.h>

#include <functional>
#include <memory>

using namespace std::placeholders;

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {
	namespace
	{
		inline bool default_pred(std::string const&) { return true; }

		inline bool ignore_subdir(std::string const& leaf)
		{ return leaf == ".." || leaf == "."; }

		std::uint32_t get_file_attributes(std::string const& p)
		{
#ifdef TORRENT_WINDOWS
			WIN32_FILE_ATTRIBUTE_DATA attr;
#if TORRENT_USE_WSTRING
			std::wstring path = convert_to_wstring(p);
			GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr);
#else
			std::string path = convert_to_native(p);
			GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attr);
#endif // TORRENT_USE_WSTRING
			if (attr.dwFileAttributes == INVALID_FILE_ATTRIBUTES) return 0;
			if (attr.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) return file_storage::attribute_hidden;
			return 0;
#else
			struct stat s;
			if (::lstat(convert_to_native(p).c_str(), &s) < 0) return 0;
			std::uint32_t file_attr = 0;
			if (s.st_mode & S_IXUSR)
				file_attr += file_storage::attribute_executable;
			if (S_ISLNK(s.st_mode))
				file_attr += file_storage::attribute_symlink;
			return file_attr;
#endif
		}

#ifndef TORRENT_WINDOWS
		std::string get_symlink_path_impl(char const* path)
		{
			constexpr int MAX_SYMLINK_PATH = 200;

			char buf[MAX_SYMLINK_PATH];
			std::string f = convert_to_native(path);
			int char_read = int(readlink(f.c_str(), buf, MAX_SYMLINK_PATH));
			if (char_read < 0) return "";
			if (char_read < MAX_SYMLINK_PATH) buf[char_read] = 0;
			else buf[0] = 0;
			return convert_from_native(buf);
		}
#endif

		std::string get_symlink_path(std::string const& p)
		{
#if defined TORRENT_WINDOWS
			TORRENT_UNUSED(p);
			return "";
#else
			return get_symlink_path_impl(p.c_str());
#endif
		}

		void add_files_impl(file_storage& fs, std::string const& p
			, std::string const& l, std::function<bool(std::string)> pred
			, std::uint32_t const flags)
		{
			std::string f = combine_path(p, l);
			if (!pred(f)) return;
			error_code ec;
			file_status s;
			stat_file(f, &s, ec, (flags & create_torrent::symlinks) ? dont_follow_links : 0);
			if (ec) return;

			// recurse into directories
			bool recurse = (s.mode & file_status::directory) != 0;

			// if the file is not a link or we're following links, and it's a directory
			// only then should we recurse
#ifndef TORRENT_WINDOWS
			if ((s.mode & file_status::link) && (flags & create_torrent::symlinks))
				recurse = false;
#endif

			if (recurse)
			{
				for (directory i(f, ec); !i.done(); i.next(ec))
				{
					std::string leaf = i.file();
					if (ignore_subdir(leaf)) continue;
					add_files_impl(fs, p, combine_path(l, leaf), pred, flags);
				}
			}
			else
			{
				// #error use the fields from s
				std::uint32_t file_flags = get_file_attributes(f);

				// mask all bits to check if the file is a symlink
				if ((file_flags & file_storage::attribute_symlink)
					&& (flags & create_torrent::symlinks))
				{
					std::string sym_path = get_symlink_path(f);
					fs.add_file(l, 0, file_flags, std::time_t(s.mtime), sym_path);
				}
				else
				{
					fs.add_file(l, s.file_size, file_flags, std::time_t(s.mtime));
				}
			}
		}

		struct hash_state
		{
			create_torrent& ct;
			storage_holder storage;
			disk_io_thread& iothread;
			piece_index_t piece_counter;
			piece_index_t completed_piece;
			std::function<void(piece_index_t)> const& f;
			error_code& ec;
		};

		void on_hash(piece_index_t const piece, sha1_hash const& piece_hash
			, storage_error const& error, hash_state* st)
		{
			if (error)
			{
				// on error
				st->ec = error.ec;
				st->iothread.abort(true);
				return;
			}
			st->ct.set_hash(piece, sha1_hash(piece_hash));
			st->f(st->completed_piece);
			++st->completed_piece;
			if (st->piece_counter < st->ct.files().end_piece())
			{
				st->iothread.async_hash(st->storage, st->piece_counter
					, disk_interface::sequential_access
					, std::bind(&on_hash, _1, _2, _3, st), nullptr);
				++st->piece_counter;
			}
			else
			{
				st->iothread.abort(true);
			}
			st->iothread.submit_jobs();
		}

	} // anonymous namespace

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE

	void add_files(file_storage& fs, std::wstring const& wfile
		, std::function<bool(std::string)> p, std::uint32_t flags)
	{
		std::string utf8 = wchar_utf8(wfile);
		add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), p, flags);
	}

	void add_files(file_storage& fs
		, std::wstring const& wfile, std::uint32_t flags)
	{
		std::string utf8 = wchar_utf8(wfile);
		add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), default_pred, flags);
	}

	void set_piece_hashes(create_torrent& t, std::wstring const& p
		, std::function<void(int)> f, error_code& ec)
	{
		std::string utf8 = wchar_utf8(p);
		set_piece_hashes(t, utf8, f, ec);
	}

	void set_piece_hashes_deprecated(create_torrent& t, std::wstring const& p
		, std::function<void(int)> f, error_code& ec)
	{
		std::string utf8 = wchar_utf8(p);
		set_piece_hashes(t, utf8, f, ec);
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

	void add_files(file_storage& fs, std::string const& file
		, std::function<bool(std::string)> p, std::uint32_t flags)
	{
		add_files_impl(fs, parent_path(complete(file)), filename(file), p, flags);
	}

	void add_files(file_storage& fs, std::string const& file, std::uint32_t flags)
	{
		add_files_impl(fs, parent_path(complete(file)), filename(file)
			, default_pred, flags);
	}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, std::function<void(piece_index_t)> const& f, error_code& ec)
	{
		// optimized path
		io_service ios;

#if TORRENT_USE_UNC_PATHS
		std::string path = canonicalize_path(p);
#else
		std::string const& path = p;
#endif

		if (t.files().num_files() == 0)
		{
			ec = errors::no_files_in_torrent;
			return;
		}

		if (t.files().total_size() == 0)
		{
			ec = errors::torrent_invalid_length;
			return;
		}

		counters cnt;
		disk_io_thread disk_thread(ios, cnt);

		storage_params params;
		params.files = &t.files();
		params.mapped_files = nullptr;
		params.path = path;
		params.pool = &disk_thread.files();
		params.mode = storage_mode_sparse;

		std::unique_ptr<storage_interface> stor(default_storage_constructor(params));
		stor->set_files(&t.files());
		storage_holder storage = disk_thread.new_torrent(std::move(stor));

		settings_pack sett;
		sett.set_int(settings_pack::cache_size, 0);
		sett.set_int(settings_pack::aio_threads, 1);

		disk_thread.set_settings(&sett);

		int const piece_read_ahead = std::max(1, 15 * 1024 * 1024 / t.piece_length());

		hash_state st = { t, std::move(storage), disk_thread, piece_index_t(0), piece_index_t(0), f, ec };
		for (piece_index_t i(0); i < piece_index_t(piece_read_ahead); ++i)
		{
			disk_thread.async_hash(st.storage, i, disk_interface::sequential_access
				, std::bind(&on_hash, _1, _2, _3, &st), nullptr);
			++st.piece_counter;
			if (st.piece_counter >= t.files().end_piece()) break;
		}
		disk_thread.submit_jobs();
		ios.run(ec);
	}

	create_torrent::~create_torrent() = default;

	create_torrent::create_torrent(file_storage& fs, int piece_size
		, int pad_file_limit, int flags, int alignment)
		: m_files(fs)
		, m_creation_date(time(nullptr))
		, m_multifile(fs.num_files() > 1)
		, m_private(false)
		, m_merkle_torrent((flags & merkle) != 0)
		, m_include_mtime((flags & modification_time) != 0)
		, m_include_symlinks((flags & symlinks) != 0)
	{
		// return instead of crash in release mode
		if (fs.num_files() == 0 || fs.total_size() == 0) return;

		if (!m_multifile && has_parent_path(m_files.file_path(file_index_t(0))))
			m_multifile = true;

		// a piece_size of 0 means automatic
		if (piece_size == 0 && !m_merkle_torrent)
		{
			const int target_size = 40 * 1024;
			piece_size = int(fs.total_size() / (target_size / 20));

			int i = 16*1024;
			for (; i < 2*1024*1024; i *= 2)
			{
				if (piece_size > i) continue;
				break;
			}
			piece_size = i;
		}
		else if (piece_size == 0 && m_merkle_torrent)
		{
			piece_size = 64*1024;
		}

		// to support mutable torrents, alignment always has to be the piece size,
		// because piece hashes are compared to determine whether files are
		// identical
		if (flags & mutable_torrent_support)
			alignment = piece_size;

		// make sure the size is an even power of 2
#ifndef NDEBUG
		for (int i = 0; i < 32; ++i)
		{
			if (piece_size & (1 << i))
			{
				TORRENT_ASSERT((piece_size & ~(1 << i)) == 0);
				break;
			}
		}
#endif
		m_files.set_piece_length(piece_size);
		if (flags & (optimize_alignment | mutable_torrent_support))
			m_files.optimize(pad_file_limit, alignment, (flags & mutable_torrent_support) != 0);

		m_files.set_num_pieces(static_cast<int>(
			(m_files.total_size() + m_files.piece_length() - 1) / m_files.piece_length()));
		m_piece_hash.resize(m_files.num_pieces());
	}

	create_torrent::create_torrent(torrent_info const& ti)
		: m_files(const_cast<file_storage&>(ti.files()))
		, m_creation_date(time(0))
		, m_multifile(ti.num_files() > 1)
		, m_private(ti.priv())
		, m_merkle_torrent(ti.is_merkle_torrent())
		, m_include_mtime(false)
		, m_include_symlinks(false)
	{
		TORRENT_ASSERT(ti.is_valid());
		TORRENT_ASSERT(ti.num_pieces() > 0);
		TORRENT_ASSERT(ti.num_files() > 0);
		TORRENT_ASSERT(ti.total_size() > 0);

		if (!ti.is_valid()) return;
		if (ti.creation_date() > 0) m_creation_date = ti.creation_date();

		if (!ti.creator().empty()) set_creator(ti.creator().c_str());
		if (!ti.comment().empty()) set_comment(ti.comment().c_str());

		for (auto const& n : ti.nodes())
			add_node(n);

		for (auto const& t : ti.trackers())
			add_tracker(t.url, t.tier);

		for (auto const& s : ti.web_seeds())
		{
			if (s.type == web_seed_entry::url_seed)
				add_url_seed(s.url);
			else if (s.type == web_seed_entry::http_seed)
				add_http_seed(s.url);
		}

		m_piece_hash.resize(m_files.num_pieces());
		for (piece_index_t i(0); i != m_files.end_piece(); ++i)
			set_hash(i, ti.hash_for_piece(i));

		boost::shared_array<char> const info = ti.metadata();
		int const size = ti.metadata_size();
		m_info_dict.preformatted().assign(&info[0], &info[0] + size);
		m_info_hash = ti.info_hash();
	}

	entry create_torrent::generate() const
	{
		entry dict;

		if (m_files.num_files() == 0 || m_files.total_size() == 0)
			return dict;

		TORRENT_ASSERT(m_files.piece_length() > 0);

		if (!m_urls.empty()) dict["announce"] = m_urls.front().first;

		if (!m_nodes.empty())
		{
			entry& nodes = dict["nodes"];
			entry::list_type& nodes_list = nodes.list();
			for (auto const& n : m_nodes)
			{
				entry::list_type node;
				node.push_back(entry(n.first));
				node.push_back(entry(n.second));
				nodes_list.push_back(entry(node));
			}
		}

		if (m_urls.size() > 1)
		{
			entry trackers(entry::list_t);
			entry tier(entry::list_t);
			int current_tier = m_urls.front().second;
			for (auto const& url : m_urls)
			{
				if (url.second != current_tier)
				{
					current_tier = url.second;
					trackers.list().push_back(tier);
					tier.list().clear();
				}
				tier.list().push_back(entry(url.first));
			}
			trackers.list().push_back(tier);
			dict["announce-list"] = trackers;
		}

		if (!m_comment.empty())
			dict["comment"] = m_comment;

		dict["creation date"] = m_creation_date;

		if (!m_created_by.empty())
			dict["created by"] = m_created_by;

		if (!m_url_seeds.empty())
		{
			if (m_url_seeds.size() == 1)
			{
				dict["url-list"] = m_url_seeds.front();
			}
			else
			{
				entry& list = dict["url-list"];
				for (auto const& url : m_url_seeds)
				{
					list.list().push_back(entry(url));
				}
			}
		}

		if (!m_http_seeds.empty())
		{
			if (m_http_seeds.size() == 1)
			{
				dict["httpseeds"] = m_http_seeds.front();
			}
			else
			{
				entry& list = dict["httpseeds"];
				for (auto const& url : m_http_seeds)
				{
					list.list().push_back(entry(url));
				}
			}
		}

		entry& info = dict["info"];
		if (m_info_dict.type() == entry::dictionary_t
			|| m_info_dict.type() == entry::preformatted_t)
		{
			info = m_info_dict;
			return dict;
		}

		if (!m_collections.empty())
		{
			entry& list = info["collections"];
			for (auto const& c : m_collections)
			{
				list.list().push_back(entry(c));
			}
		}

		if (!m_similar.empty())
		{
			entry& list = info["similar"];
			for (auto const& ih : m_similar)
			{
				list.list().push_back(entry(ih.to_string()));
			}
		}

		info["name"] = m_files.name();

		if (!m_root_cert.empty())
			info["ssl-cert"] = m_root_cert;

		if (m_private) info["private"] = 1;

		if (!m_multifile)
		{
			file_index_t const first(0);
			if (m_include_mtime) info["mtime"] = m_files.mtime(first);
			info["length"] = m_files.file_size(first);
			std::uint32_t const flags = m_files.file_flags(first);
			if (flags & (file_storage::flag_pad_file
				| file_storage::flag_hidden
				| file_storage::flag_executable
				| file_storage::flag_symlink))
			{
				std::string& attr = info["attr"].string();
				if (flags & file_storage::flag_pad_file) attr += 'p';
				if (flags & file_storage::flag_hidden) attr += 'h';
				if (flags & file_storage::flag_executable) attr += 'x';
				if (m_include_symlinks && (flags & file_storage::flag_symlink)) attr += 'l';
			}
			if (m_include_symlinks
				&& (flags & file_storage::flag_symlink))
			{
				entry& sympath_e = info["symlink path"];

				std::string split = split_path(m_files.symlink(first));
				for (char const* e = split.c_str(); e != nullptr; e = next_path_element(e))
					sympath_e.list().push_back(e);
			}
			if (!m_filehashes.empty())
			{
				info["sha1"] = m_filehashes[first].to_string();
			}
		}
		else
		{
			if (!info.find_key("files"))
			{
				entry& files = info["files"];

				for (file_index_t i(0); i != m_files.end_file(); ++i)
				{
					files.list().push_back(entry());
					entry& file_e = files.list().back();
					if (m_include_mtime && m_files.mtime(i)) file_e["mtime"] = m_files.mtime(i);
					file_e["length"] = m_files.file_size(i);
					entry& path_e = file_e["path"];

					TORRENT_ASSERT(has_parent_path(m_files.file_path(i)));

					{
						std::string split = split_path(m_files.file_path(i));
						TORRENT_ASSERT(split.c_str() == m_files.name());

						for (char const* e = next_path_element(split.c_str());
							e != nullptr; e = next_path_element(e))
							path_e.list().push_back(entry(e));
					}

					std::uint32_t const flags = m_files.file_flags(i);
					if (flags != 0)
					{
						std::string& attr = file_e["attr"].string();
						if (flags & file_storage::flag_pad_file) attr += 'p';
						if (flags & file_storage::flag_hidden) attr += 'h';
						if (flags & file_storage::flag_executable) attr += 'x';
						if (m_include_symlinks && (flags & file_storage::flag_symlink)) attr += 'l';
					}

					if (m_include_symlinks
						&& (flags & file_storage::flag_symlink))
					{
						entry& sympath_e = file_e["symlink path"];

						std::string split = split_path(m_files.symlink(i));
						for (char const* e = split.c_str(); e != nullptr; e = next_path_element(e))
							sympath_e.list().push_back(entry(e));
					}
					if (!m_filehashes.empty() && m_filehashes[i] != sha1_hash())
					{
						file_e["sha1"] = m_filehashes[i].to_string();
					}
				}
			}
		}

		info["piece length"] = m_files.piece_length();
		if (m_merkle_torrent)
		{
			int const num_leafs = merkle_num_leafs(m_files.num_pieces());
			int const num_nodes = merkle_num_nodes(num_leafs);
			int const first_leaf = num_nodes - num_leafs;
			m_merkle_tree.resize(num_nodes);
			int const num_pieces = int(m_piece_hash.size());
			for (int i = 0; i < num_pieces; ++i)
				m_merkle_tree[first_leaf + i] = m_piece_hash[piece_index_t(i)];
			for (int i = num_pieces; i < num_leafs; ++i)
				m_merkle_tree[first_leaf + i].clear();

			// now that we have initialized all leaves, build
			// each level bottom-up
			int level_start = first_leaf;
			int level_size = num_leafs;
			while (level_start > 0)
			{
				int parent = merkle_get_parent(level_start);
				for (int i = level_start; i < level_start + level_size; i += 2, ++parent)
				{
					hasher h;
					h.update(m_merkle_tree[i]);
					h.update(m_merkle_tree[i + 1]);
					m_merkle_tree[parent] = h.final();
				}
				level_start = merkle_get_parent(level_start);
				level_size /= 2;
			}
			TORRENT_ASSERT(level_size == 1);
			info["root hash"] = m_merkle_tree[0];
		}
		else
		{
			std::string& p = info["pieces"].string();

			for (sha1_hash const& h : m_piece_hash)
				p.append(h.data(), h.size());
		}

		std::vector<char> buf;
		bencode(std::back_inserter(buf), info);
		m_info_hash = hasher(buf).final();

		return dict;
	}

	void create_torrent::add_tracker(string_view url, int const tier)
	{
		using announce_entry = std::pair<std::string, int>;
		auto i = std::find_if(m_urls.begin(), m_urls.end()
			, [&url](announce_entry const& ae) { return ae.first == url; });
		if (i != m_urls.end()) return;
		m_urls.emplace_back(url.to_string(), tier);

		std::sort(m_urls.begin(), m_urls.end()
			, [](announce_entry const& lhs, announce_entry const& rhs)
			{ return lhs.second < rhs.second; });
	}

	void create_torrent::set_root_cert(string_view cert)
	{
		m_root_cert.assign(cert.data(), cert.size());
	}

	void create_torrent::add_similar_torrent(sha1_hash ih)
	{
		m_similar.push_back(ih);
	}

	void create_torrent::add_collection(string_view c)
	{
		m_collections.emplace_back(c);
	}

	void create_torrent::set_hash(piece_index_t index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= piece_index_t(0));
		TORRENT_ASSERT(index < m_piece_hash.end_index());
		m_piece_hash[index] = h;
	}

	void create_torrent::set_file_hash(file_index_t index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= file_index_t(0));
		TORRENT_ASSERT(index < m_files.end_file());
		if (m_filehashes.empty()) m_filehashes.resize(m_files.num_files());
		m_filehashes[index] = h;
	}

	void create_torrent::add_node(std::pair<std::string, int> node)
	{
		m_nodes.emplace_back(std::move(node));
	}

	void create_torrent::add_url_seed(string_view url)
	{
		m_url_seeds.emplace_back(url);
	}

	void create_torrent::add_http_seed(string_view url)
	{
		m_http_seeds.emplace_back(url);
	}

	void create_torrent::set_comment(char const* str)
	{
		if (str == nullptr) m_comment.clear();
		else m_comment = str;
	}

	void create_torrent::set_creator(char const* str)
	{
		if (str == nullptr) m_created_by.clear();
		else m_created_by = str;
	}
}}
