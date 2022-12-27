/*

Copyright (c) 2008-2022, Arvid Norberg
Copyright (c) 2016-2017, 2019-2020, Alden Torres
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Mike Tzou
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
#include "libtorrent/mmap_disk_io.hpp" // for hasher_thread_divisor
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/merkle.hpp" // for merkle_*()
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/session.hpp" // for default_disk_io_constructor
#include "libtorrent/aux_/directory.hpp"
#include "libtorrent/disk_interface.hpp"

#include <sys/types.h>
#include <sys/stat.h>

#include <functional>
#include <memory>

using namespace std::placeholders;

namespace libtorrent {

#if TORRENT_ABI_VERSION <= 2
	constexpr create_flags_t create_torrent::optimize_alignment;
#endif
#if TORRENT_ABI_VERSION == 1
	constexpr create_flags_t create_torrent::optimize;
#endif
#if TORRENT_ABI_VERSION <= 2
	constexpr create_flags_t create_torrent::merkle;
#endif
	constexpr create_flags_t create_torrent::modification_time;
	constexpr create_flags_t create_torrent::symlinks;
#if TORRENT_ABI_VERSION <= 2
	constexpr create_flags_t create_torrent::mutable_torrent_support;
#endif
	constexpr create_flags_t create_torrent::v2_only;
	constexpr create_flags_t create_torrent::v1_only;
	constexpr create_flags_t create_torrent::canonical_files;
	constexpr create_flags_t create_torrent::no_attributes;
	constexpr create_flags_t create_torrent::canonical_files_no_tail_padding;
	constexpr create_flags_t create_torrent::allow_odd_piece_size;

namespace {

	bool default_pred(std::string const&) { return true; }

	bool ignore_subdir(std::string const& leaf)
	{ return leaf == ".." || leaf == "."; }

#ifndef TORRENT_WINDOWS
	std::string get_symlink_path_impl(char const* path)
	{
		constexpr int MAX_SYMLINK_PATH = 200;

		char buf[MAX_SYMLINK_PATH];
		std::string f = convert_to_native_path_string(path);
		int char_read = int(readlink(f.c_str(), buf, MAX_SYMLINK_PATH));
		if (char_read < 0) return "";
		if (char_read < MAX_SYMLINK_PATH) buf[char_read] = 0;
		else buf[0] = 0;
		return convert_from_native_path(buf);
	}
#endif

	void add_files_impl(file_storage& fs, std::string const& p
		, std::string const& l, std::function<bool(std::string)> const& pred
		, create_flags_t const flags)
	{
		std::string const f = combine_path(p, l);
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
			for (aux::directory i(f, ec); !i.done(); i.next(ec))
			{
				std::string const leaf = i.file();
				if (ignore_subdir(leaf)) continue;
				add_files_impl(fs, p, combine_path(l, leaf), pred, flags);
			}
		}
		else
		{
			// #error use the fields from s
			file_flags_t const file_flags = (flags & create_torrent::no_attributes)
				? file_flags_t{}
				: aux::get_file_attributes(f);

			// mask all bits to check if the file is a symlink
			if ((file_flags & file_storage::flag_symlink)
				&& (flags & create_torrent::symlinks))
			{
				std::string const sym_path = aux::get_symlink_path(f);
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
		disk_interface& iothread;
		piece_index_t piece_counter;
		piece_index_t completed_piece;
		std::function<void(piece_index_t)> const& f;
		error_code& ec;
	};

	void on_hash(aux::vector<sha256_hash> v2_blocks, piece_index_t const piece
		, sha1_hash const& piece_hash, storage_error const& error, hash_state* st)
	{
		if (error)
		{
			// on error
			st->ec = error.ec;
			st->iothread.abort(true);
			return;
		}

		if (!st->ct.is_v2_only())
			st->ct.set_hash(piece, piece_hash);

		if (!st->ct.is_v1_only())
		{
			file_index_t const current_file = st->ct.files().file_index_at_piece(piece);
			if (!st->ct.files().pad_file_at(current_file))
			{
				piece_index_t const file_first_piece(int(st->ct.files().file_offset(current_file) / st->ct.piece_length()));
				TORRENT_ASSERT(st->ct.files().file_offset(current_file) % st->ct.piece_length() == 0);

				auto const file_piece_offset = piece - file_first_piece;
				auto const file_size = st->ct.files().file_size(current_file);
				TORRENT_ASSERT(file_size > 0);
				auto const file_blocks = st->ct.files().file_num_blocks(current_file);
				auto const piece_blocks = st->ct.files().blocks_in_piece2(piece);
				int const num_leafs = merkle_num_leafs(file_blocks);
				// If the file is smaller than one piece then the block hashes
				// should be padded to the next power of two instead of the next
				// piece boundary.
				int const padded_leafs = file_size < st->ct.piece_length()
					? num_leafs
					: st->ct.piece_length() / default_block_size;

				TORRENT_ASSERT(padded_leafs <= int(v2_blocks.size()));
				for (auto i = piece_blocks; i < padded_leafs; ++i)
					v2_blocks[i].clear();
				sha256_hash const piece_root = merkle_root(
					span<sha256_hash>(v2_blocks).first(padded_leafs));
				st->ct.set_hash2(current_file, file_piece_offset, piece_root);
			}
		}

		auto flags = disk_interface::sequential_access;
		if (!st->ct.is_v2_only()) flags |= disk_interface::v1_hash;

		st->f(st->completed_piece);
		++st->completed_piece;
		if (st->piece_counter < st->ct.files().end_piece())
		{
			span<sha256_hash> v2_span(v2_blocks);
			st->iothread.async_hash(st->storage, st->piece_counter, v2_span, flags
				, std::bind(&on_hash, std::move(v2_blocks), _1, _2, _3, st));
			++st->piece_counter;
			st->iothread.submit_jobs();
		}
		else if (st->completed_piece == st->ct.files().end_piece())
		{
			st->iothread.abort(true);
		}
	}

} // anonymous namespace

namespace aux {

	file_flags_t get_file_attributes(std::string const& p)
	{
		auto const path = convert_to_native_path_string(p);

#ifdef TORRENT_WINDOWS
		WIN32_FILE_ATTRIBUTE_DATA attr;
		GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr);
		if (attr.dwFileAttributes == INVALID_FILE_ATTRIBUTES) return {};
		if (attr.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) return file_storage::flag_hidden;
		return {};
#else
		struct ::stat s{};
		if (::lstat(path.c_str(), &s) < 0) return {};
		file_flags_t file_attr = {};
		if (s.st_mode & S_IXUSR)
			file_attr |= file_storage::flag_executable;
		if (S_ISLNK(s.st_mode))
			file_attr |= file_storage::flag_symlink;
		return file_attr;
#endif
	}

	std::string get_symlink_path(std::string const& p)
	{
#if defined TORRENT_WINDOWS
		TORRENT_UNUSED(p);
		return "";
#else
		return get_symlink_path_impl(p.c_str());
#endif
	}

} // anonymous aux

	void add_files(file_storage& fs, std::string const& file
		, std::function<bool(std::string)> p, create_flags_t const flags)
	{
		add_files_impl(fs, parent_path(complete(file)), filename(file), p, flags);
	}

	void add_files(file_storage& fs, std::string const& file, create_flags_t const flags)
	{
		add_files_impl(fs, parent_path(complete(file)), filename(file)
			, default_pred, flags);
	}

namespace {
	struct disk_aborter
	{
		explicit disk_aborter(disk_interface& dio) : m_dio(dio) {}
		~disk_aborter() { m_dio.abort(true); }
		disk_aborter(disk_aborter const&) = delete;
		disk_aborter& operator=(disk_aborter const&) = delete;
	private:
		disk_interface& m_dio;
	};
}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, std::function<void(piece_index_t)> const& f, error_code& ec)
	{
		aux::session_settings sett;
		set_piece_hashes(t, p, sett, f, ec);
	}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, settings_interface const& sett
		, std::function<void(piece_index_t)> const& f, error_code& ec)
	{
		set_piece_hashes(t, p, sett, default_disk_io_constructor, f, ec);
	}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, settings_interface const& sett, disk_io_constructor_type disk_io
		, std::function<void(piece_index_t)> const& f, error_code& ec)
	{
		// optimized path
#ifdef TORRENT_BUILD_SIMULATOR
		sim::default_config conf;
		sim::simulation sim{conf};
		io_context ios{sim};
#else
		io_context ios;
#endif

#if TORRENT_USE_UNC_PATHS
		std::string const path = canonicalize_path(p);
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
		int const num_threads = sett.get_int(settings_pack::hashing_threads);
		std::unique_ptr<disk_interface> disk_thread = disk_io(ios, sett, cnt);
		disk_aborter da(*disk_thread.get());

		aux::vector<download_priority_t, file_index_t> priorities;
		sha1_hash info_hash;
		storage_params params{
			t.files(),
			nullptr,
			path,
			storage_mode_t::storage_mode_sparse,
			priorities,
			info_hash
		};

		storage_holder storage = disk_thread->new_torrent(params
			, std::shared_ptr<void>());

		// have 4 outstanding hash requests per thread, and no less than 1 MiB
		int const jobs_per_thread = 4;
		int const piece_read_ahead = std::max(num_threads * jobs_per_thread
			, 1 * 1024 * 1024 / t.piece_length());

		hash_state st = { t, std::move(storage), *disk_thread.get(), piece_index_t(0), piece_index_t(0), f, ec };
		for (piece_index_t i(0); i < piece_index_t(piece_read_ahead); ++i)
		{
			aux::vector<sha256_hash> v2_blocks;

			if (!t.is_v1_only())
				v2_blocks.resize(t.piece_length() / default_block_size);

			auto flags = disk_interface::sequential_access;
			if (!t.is_v2_only()) flags |= disk_interface::v1_hash;

			// the span needs to be created before the call to async_hash to ensure that
			// it is constructed before the vector is moved into the bind context
			span<sha256_hash> v2_span(v2_blocks);
			disk_thread->async_hash(st.storage, i, v2_span, flags
				, std::bind(&on_hash, std::move(v2_blocks), _1, _2, _3, &st));
			++st.piece_counter;
			if (st.piece_counter >= t.files().end_piece()) break;
		}
		disk_thread->submit_jobs();

#ifdef TORRENT_BUILD_SIMULATOR
		sim.run();
#else
		ios.run();
#endif
		if (st.ec) {
			ec = st.ec;
		}
	}

	create_torrent::~create_torrent() = default;

	create_torrent::create_torrent(file_storage& fs, int piece_size
		, create_flags_t const flags)
		: m_files(fs)
		, m_creation_date(::time(nullptr))
		, m_multifile(fs.num_files() > 1)
		, m_private(false)
		, m_include_mtime(bool(flags & create_torrent::modification_time))
		, m_include_symlinks(bool(flags & create_torrent::symlinks))
		, m_v2_only(bool(flags & create_torrent::v2_only))
		, m_v1_only(bool(flags & create_torrent::v1_only))
	{
		// return instead of crash in release mode
		if (fs.num_files() == 0 || fs.total_size() == 0) return;

		if (!m_multifile && has_parent_path(m_files.file_path(file_index_t(0))))
			m_multifile = true;

		// a piece_size of 0 means automatic
		if (piece_size == 0)
		{
			// size_table is computed from the following:
			//   target_list_size = sqrt(total_size) * 2;
			//   target_piece_size = total_size / (target_list_size / hash_size);
			// Given hash_size = 20 bytes, target_piece_size = (16*1024 * pow(2, i))
			// we can determine size_table = (total_size = pow(2 * target_piece_size / hash_size, 2))
			std::array<std::int64_t, 10> const size_table{{
				       2684355LL // ->  16kiB
				,     10737418LL // ->  32 kiB
				,     42949673LL // ->  64 kiB
				,    171798692LL // -> 128 kiB
				,    687194767LL // -> 256 kiB
				,   2748779069LL // -> 512 kiB
				,  10995116278LL // -> 1 MiB
				,  43980465111LL // -> 2 MiB
				, 175921860444LL // -> 4 MiB
				, 703687441777LL}}; // -> 8 MiB

			int i = 0;
			for (auto const s : size_table)
			{
				if (s >= fs.total_size()) break;
				++i;
			}
			piece_size = default_block_size << i;
		}

		if (!(flags & v1_only))
		{
			// v2 torrents requires piece sizes to be at least 16 kiB
			piece_size = std::max(piece_size, 16 * 1024);

			// make sure the size is an even power of 2
			// i.e. only a single bit is set. This is required by v2 torrents
			if ((piece_size & (piece_size - 1)) != 0)
				aux::throw_ex<system_error>(errors::invalid_piece_size);
		}
		else if ((piece_size % (16 * 1024)) != 0
			&& (piece_size & (piece_size - 1)) != 0
			&& !(flags & allow_odd_piece_size))
		{
			// v1 torrents should have piece sizes divisible by 16 kiB
			aux::throw_ex<system_error>(errors::invalid_piece_size);
		}

		fs.set_piece_length(piece_size);
		if (!(flags & v1_only)
			|| (flags & canonical_files)
			|| (flags & canonical_files_no_tail_padding))
			fs.canonicalize_impl(bool(flags & canonical_files_no_tail_padding));

		fs.set_num_pieces(aux::calc_num_pieces(fs));
		TORRENT_ASSERT(fs.piece_length() > 0);
	}

	create_torrent::create_torrent(torrent_info const& ti)
		: m_files(ti.files())
		, m_creation_date(::time(nullptr))
		, m_multifile(ti.num_files() > 1)
		, m_private(ti.priv())
		, m_include_mtime(false)
		, m_include_symlinks(false)
		, m_v2_only(!ti.info_hashes().has_v1())
		, m_v1_only(!ti.info_hashes().has_v2())
	{
		bool const make_v1 = ti.info_hashes().has_v1();
		bool const make_v2 = ti.info_hashes().has_v2();

		TORRENT_ASSERT_PRECOND(make_v2 || make_v1);
		TORRENT_ASSERT_PRECOND(ti.is_valid());
		TORRENT_ASSERT_PRECOND(ti.num_pieces() > 0);
		TORRENT_ASSERT_PRECOND(ti.num_files() > 0);
		TORRENT_ASSERT_PRECOND(ti.total_size() > 0);

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

		if (make_v1)
		{
			m_piece_hash.resize(m_files.num_pieces());
			for (auto const i : m_files.piece_range())
				set_hash(i, ti.hash_for_piece(i));
		}

		if (make_v2)
		{
			m_fileroots.resize(m_files.num_files());
			m_file_piece_hash.resize(m_files.num_files());
			for (auto const i : m_files.file_range())
			{
				// don't include merkle hash trees for pad files
				if (m_files.pad_file_at(i)) continue;
				if (m_files.file_size(i) == 0) continue;

				auto const file_size = m_files.file_size(i);
				if (file_size <= m_files.piece_length())
				{
					set_hash2(i, piece_index_t::diff_type{0}, m_files.root(i));
					continue;
				}

				span<char const> pieces = ti.piece_layer(i);

				piece_index_t::diff_type p{0};
				for (int h = 0; h < int(pieces.size()); h += int(sha256_hash::size()))
					set_hash2(i, p++, sha256_hash(pieces.data() + h));
			}
		}

		auto const info = ti.info_section();
		m_info_dict.preformatted().assign(info.data(), info.data() + info.size());
	}

namespace {
	bool validate_v2_hashes(file_storage const& fs
		, aux::vector<aux::vector<sha256_hash, piece_index_t::diff_type>, file_index_t> const& file_piece_hash)
	{
		if (int(file_piece_hash.size()) != fs.num_files()) return false;

		int const piece_size = fs.piece_length();

		for (auto i : fs.file_range())
		{
			auto const& hashes = file_piece_hash[i];

			// pad files are not supposed to have any hashes
			if (fs.pad_file_at(i))
			{
				if (!hashes.empty()) return false;
				continue;
			}

			if (int(hashes.size()) != (fs.file_size(i) + piece_size - 1) / piece_size) return false;
			if (std::any_of(hashes.begin(), hashes.end(), [](sha256_hash const& h)
				{ return h.is_all_zeros(); }))
			{
				return false;
			}
		}
		return true;
	}

	bool validate_v1_hashes(file_storage const& fs
		, aux::vector<sha1_hash, piece_index_t> piece_hash)
	{
		int const piece_size = fs.piece_length();
		if (int(piece_hash.size()) != (fs.total_size() + piece_size - 1) / piece_size)
			return false;

		return !std::any_of(piece_hash.begin(), piece_hash.end()
			, [](sha1_hash const& h) { return h.is_all_zeros(); });
	}

	void add_file_attrs(entry& e, file_flags_t const flags, bool const include_symlinks)
	{
		if (!(flags & (file_storage::flag_pad_file
			| file_storage::flag_hidden
			| file_storage::flag_executable
			| file_storage::flag_symlink)))
		{
			return;
		}
		std::string& attr = e["attr"].string();
		if (flags & file_storage::flag_pad_file) attr += 'p';
		if (flags & file_storage::flag_hidden) attr += 'h';
		if (flags & file_storage::flag_executable) attr += 'x';
		if (include_symlinks && (flags & file_storage::flag_symlink)) attr += 'l';
	}

	void add_symlink_path(entry& e, std::string symlink_path)
	{
		entry& sympath_e = e["symlink path"];

		std::string const link = lexically_relative("", symlink_path);
		for (auto elems = lsplit_path(link); !elems.first.empty();
			elems = lsplit_path(elems.second))
			sympath_e.list().emplace_back(elems.first);
	}
}

	std::vector<char> create_torrent::generate_buf() const
	{
		// TODO: this can be optimized
		std::vector<char> ret;
		bencode(std::back_inserter(ret), generate());
		return ret;
	}

	entry create_torrent::generate() const
	{
		if (m_files.num_files() == 0 || m_files.total_size() == 0)
			aux::throw_ex<system_error>(errors::torrent_missing_file_tree);

		// if all v2 hashes are set correctly, generate the v2 parts of the
		// torrent
		bool const make_v2 = validate_v2_hashes(m_files, m_file_piece_hash);
		bool const make_v1 = validate_v1_hashes(m_files, m_piece_hash);

		// if neither v1 nor v2 hashes were set, we can't create a torrent
		if (!make_v1 && !make_v2)
			aux::throw_ex<system_error>(errors::invalid_hash_entry);

		TORRENT_ASSERT(m_files.piece_length() > 0);

		entry dict;

		if (!m_urls.empty()) dict["announce"] = m_urls.front().first;

		if (!m_nodes.empty())
		{
			entry& nodes = dict["nodes"];
			entry::list_type& nodes_list = nodes.list();
			for (auto const& n : m_nodes)
			{
				entry::list_type node(2);
				node[0] = n.first;
				node[1] = n.second;
				nodes_list.emplace_back(std::move(node));
			}
		}

		if (m_urls.size() > 1)
		{
			entry::list_type trackers;
			entry::list_type tier;
			int current_tier = m_urls.front().second;
			for (auto const& url : m_urls)
			{
				if (url.second != current_tier)
				{
					current_tier = url.second;
					trackers.emplace_back(std::move(tier));
					tier.clear();
				}
				tier.emplace_back(url.first);
			}
			trackers.emplace_back(std::move(tier));
			dict["announce-list"] = std::move(trackers);
		}

		if (!m_comment.empty())
			dict["comment"] = m_comment;

		if (m_creation_date != 0)
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
					list.list().emplace_back(url);
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
					list.list().emplace_back(url);
				}
			}
		}

		if (make_v2)
		{
			TORRENT_ASSERT(!m_file_piece_hash.empty());
			m_fileroots.resize(m_files.num_files());

			sha256_hash const pad_hash = merkle_pad(m_files.piece_length() / default_block_size, 1);
			auto& file_pieces = dict["piece layers"].dict();

			for (file_index_t fi : m_files.file_range())
			{
				if (files().file_flags(fi) & file_storage::flag_pad_file) continue;
				if (files().file_size(fi) == 0) continue;

				m_fileroots[fi] = merkle_root(m_file_piece_hash[fi], pad_hash);

				// files that only have one piece store the piece hash as the
				// root, we don't need a pieces layer entry for such files
				if (m_file_piece_hash[fi].size() < 2) continue;
				auto& pieces = file_pieces[m_fileroots[fi].to_string()].string();
				pieces.clear();
				pieces.reserve(m_file_piece_hash[fi].size() * sha256_hash::size());
				for (auto& p : m_file_piece_hash[fi])
					pieces.append(reinterpret_cast<const char*>(p.data()), p.size());
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
				list.list().emplace_back(c);
			}
		}

		if (!m_similar.empty())
		{
			entry& list = info["similar"];
			for (auto const& ih : m_similar)
			{
				list.list().emplace_back(ih.to_string());
			}
		}

		info["name"] = m_files.name();

		if (!m_root_cert.empty())
			info["ssl-cert"] = m_root_cert;

		if (m_private) info["private"] = 1;

		if (make_v1)
		{
			if (!m_multifile)
			{
				file_index_t const first(0);
				if (m_include_mtime) info["mtime"] = m_files.mtime(first);
				info["length"] = m_files.file_size(first);
				file_flags_t const flags = m_files.file_flags(first);
				add_file_attrs(info, flags, m_include_symlinks);
				if (m_include_symlinks
					&& (flags & file_storage::flag_symlink))
				{
					add_symlink_path(info, m_files.internal_symlink(first));
				}
#if TORRENT_ABI_VERSION < 3
				if (!m_filehashes.empty())
				{
					info["sha1"] = m_filehashes[first].to_string();
				}
#endif
			}
			else
			{
				entry& files = info["files"];

				for (auto const i : m_files.file_range())
				{
					files.list().emplace_back();
					entry& file_e = files.list().back();
					if (m_include_mtime && m_files.mtime(i)) file_e["mtime"] = m_files.mtime(i);
					file_e["length"] = m_files.file_size(i);

					TORRENT_ASSERT(has_parent_path(m_files.file_path(i)));

					{
						entry& path_e = file_e["path"];

						std::string const p = m_files.file_path(i);
						// deliberately skip the first path element, since that's the
						// "name" of the torrent already
						string_view path = lsplit_path(p).second;
						for (auto elems = lsplit_path(path); !elems.first.empty(); elems = lsplit_path(elems.second))
							path_e.list().emplace_back(elems.first);
					}

					file_flags_t const flags = m_files.file_flags(i);
					add_file_attrs(file_e, flags, m_include_symlinks);

					if (m_include_symlinks && (flags & file_storage::flag_symlink))
					{
						add_symlink_path(file_e, m_files.internal_symlink(i));
					}
#if TORRENT_ABI_VERSION < 3
					if (!m_filehashes.empty() && m_filehashes[i] != sha1_hash())
					{
						file_e["sha1"] = m_filehashes[i].to_string();
					}
#endif
				}
			}
		}

		if (make_v2)
		{
			auto& tree = info["file tree"];

			for (file_index_t i : m_files.file_range())
			{
				if (files().file_flags(i) & file_storage::flag_pad_file) continue;

				entry* file_e_ptr = &tree;

				{
					std::string const file_path = m_files.file_path(i);
					auto const split = m_multifile
						? lsplit_path(file_path)
						: std::pair<string_view, string_view>(file_path, file_path);
					TORRENT_ASSERT(split.first == m_files.name());

					for (auto e = lsplit_path(split.second);
						!e.first.empty();
						e = lsplit_path(e.second))
					{
						file_e_ptr = &(*file_e_ptr)[e.first];
						if (file_e_ptr->dict().find({}) != file_e_ptr->dict().end())
						{
							// path conflict
							// there is already a file with this name
							// refuse to generate a torrent with such a conflict
							aux::throw_ex<system_error>(errors::torrent_inconsistent_files);
						}
					}
				}

				if (!file_e_ptr->dict().empty())
				{
					// path conflict
					// there is already a directory with this name
					// refuse to generate a torrent with such a conflict
					aux::throw_ex<system_error>(errors::torrent_inconsistent_files);
				}

				entry& file_e = (*file_e_ptr)[{}];

				if (m_include_mtime && m_files.mtime(i)) file_e["mtime"] = m_files.mtime(i);

				file_flags_t const flags = m_files.file_flags(i);
				add_file_attrs(file_e, flags, m_include_symlinks);

				if (m_include_symlinks && (flags & file_storage::flag_symlink))
				{
					add_symlink_path(file_e, m_files.internal_symlink(i));
				}
				else
				{
					if (m_files.file_size(i) > 0)
						file_e["pieces root"] = m_fileroots[i];
					file_e["length"] = m_files.file_size(i);
				}
			}
			info["meta version"] = 2;
		}

		info["piece length"] = m_files.piece_length();

		if (make_v1)
		{
			std::string& p = info["pieces"].string();

			for (sha1_hash const& h : m_piece_hash)
				p.append(h.data(), h.size());
		}

		return dict;
	}

	void create_torrent::add_tracker(string_view url, int const tier)
	{
		if (url.empty()) return;
		using announce_entry = std::pair<std::string, int>;
		auto const i = std::find_if(m_urls.begin(), m_urls.end()
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
		m_similar.emplace_back(ih);
	}

	void create_torrent::add_collection(string_view c)
	{
		m_collections.emplace_back(c);
	}

	void create_torrent::set_hash(piece_index_t index, sha1_hash const& h)
	{
		if (m_v2_only)
			aux::throw_ex<system_error>(errors::invalid_hash_entry);

		if (m_piece_hash.empty())
			m_piece_hash.resize(m_files.num_pieces());

		TORRENT_ASSERT_PRECOND(index >= piece_index_t(0));
		TORRENT_ASSERT_PRECOND(index < m_piece_hash.end_index());
		m_piece_hash[index] = h;
	}

	void create_torrent::set_hash2(file_index_t file, piece_index_t::diff_type piece, sha256_hash const& h)
	{
		TORRENT_ASSERT_PRECOND(file >= file_index_t(0));
		TORRENT_ASSERT_PRECOND(file < m_files.end_file());
		TORRENT_ASSERT_PRECOND(piece >= piece_index_t::diff_type(0));
		TORRENT_ASSERT_PRECOND(piece < piece_index_t::diff_type(m_files.file_num_pieces(file)));
		TORRENT_ASSERT_PRECOND(!m_files.pad_file_at(file));
		TORRENT_ASSERT_PRECOND(!h.is_all_zeros());
		TORRENT_ASSERT_PRECOND(m_files.file_num_pieces(file) > 0);

		if (m_v1_only)
			aux::throw_ex<system_error>(errors::invalid_hash_entry);

		if (m_file_piece_hash.empty())
			m_file_piece_hash.resize(m_files.num_files());

		auto& fh = m_file_piece_hash[file];
		if (fh.empty())
			fh.resize(std::size_t(m_files.file_num_pieces(file)));
		fh[piece] = h;
	}

#if TORRENT_ABI_VERSION < 3
	void create_torrent::set_file_hash(file_index_t index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= file_index_t(0));
		TORRENT_ASSERT(index < m_files.end_file());
		if (m_filehashes.empty()) m_filehashes.resize(m_files.num_files());
		m_filehashes[index] = h;
	}
#endif

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

	void create_torrent::set_creation_date(std::time_t timestamp)
	{
		m_creation_date = timestamp;
	}
}
