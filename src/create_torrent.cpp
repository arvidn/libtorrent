/*

Copyright (c) 2008-2022, Arvid Norberg
Copyright (c) 2016-2017, 2019-2020, Alden Torres
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Mike Tzou
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/create_torrent.hpp"
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
#include "libtorrent/aux_/bencoder.hpp"
#include "libtorrent/aux_/time.hpp" // for posix_time

#include <sys/types.h>
#include <sys/stat.h>

#include <string>
#include <functional>
#include <optional>
#include <memory>
#include <cinttypes>

using namespace std::placeholders;

namespace libtorrent {
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

	void add_files_impl(std::vector<create_file_entry>& files
		, std::string const& p
		, std::string l
		, std::function<bool(std::string)> const& pred
		, create_flags_t const flags)
	{
		std::string f = combine_path(p, l);
		if (!pred(f)) return;
		error_code ec;
		file_status s;
		stat_file(f, &s, ec, (flags & create_torrent::symlinks) ? dont_follow_links : file_status_flag_t{});
		if (ec) return;

		// recurse into directories
		bool recurse = bool(s.mode & file_status::directory);

		// if the file is not a link or we're following links, and it's a directory
		// only then should we recurse
		if ((s.mode & file_status::symlink) && (flags & create_torrent::symlinks))
			recurse = false;

		if (recurse)
		{
			for (aux::directory i(std::move(f), ec); !i.done(); i.next(ec))
			{
				std::string leaf = i.file();
				if (ignore_subdir(leaf)) continue;
				add_files_impl(files, p, combine_path(std::move(l), std::move(leaf)), pred, flags);
			}
		}
		else
		{
			file_flags_t const file_flags = (flags & create_torrent::no_attributes)
				? file_flags_t{}
				: ((s.mode & file_status::hidden) ? file_storage::flag_hidden : file_flags_t{})
					| ((s.mode & file_status::executable) ? file_storage::flag_executable : file_flags_t{})
					| ((s.mode & file_status::symlink) ? file_storage::flag_symlink : file_flags_t{})
					;

			// mask all bits to check if the file is a symlink
			if ((file_flags & file_storage::flag_symlink)
				&& (flags & create_torrent::symlinks))
			{
				std::string sym_path = aux::get_symlink_path(f);
				files.emplace_back(std::move(l), 0, file_flags, std::time_t(s.mtime)
					, std::move(sym_path));
			}
			else
			{
				files.emplace_back(std::move(l), s.file_size, file_flags, std::time_t(s.mtime));
			}
		}
	}

#if TORRENT_ABI_VERSION < 4
	void add_files_impl(file_storage& fs, std::string const& p
		, std::string const& l, std::function<bool(std::string)> const& pred
		, create_flags_t const flags)
	{
		std::string const f = combine_path(p, l);
		if (!pred(f)) return;
		error_code ec;
		file_status s;
		stat_file(f, &s, ec, (flags & create_torrent::symlinks)
			? dont_follow_links : file_status_flag_t{});
		if (ec) return;

		// recurse into directories
		bool recurse = bool(s.mode & file_status::directory);

		// if the file is not a link or we're following links, and it's a directory
		// only then should we recurse
		if ((s.mode & file_status::symlink) && (flags & create_torrent::symlinks))
			recurse = false;

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
			file_flags_t const file_flags = (flags & create_torrent::no_attributes)
				? file_flags_t{}
				: ((s.mode & file_status::hidden) ? file_storage::flag_hidden : file_flags_t{})
					| ((s.mode & file_status::executable) ? file_storage::flag_executable : file_flags_t{})
					| ((s.mode & file_status::symlink) ? file_storage::flag_symlink : file_flags_t{})
					;

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
#endif

	struct hash_state
	{
		file_storage const& fs;
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
			file_index_t const current_file = st->fs.file_index_at_piece(piece);
			if (!(st->ct.file_at(current_file).flags & file_storage::flag_pad_file))
			{
				piece_index_t const file_first_piece(int(st->fs.file_offset(current_file) / st->ct.piece_length()));
				TORRENT_ASSERT(st->fs.file_offset(current_file) % st->ct.piece_length() == 0);

				auto const file_piece_offset = piece - file_first_piece;
				auto const file_size = st->fs.file_size(current_file);
				TORRENT_ASSERT(file_size > 0);
				auto const file_blocks = st->fs.file_num_blocks(current_file);
				auto const piece_blocks = st->fs.blocks_in_piece2(piece);
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
		if (st->piece_counter < st->ct.end_piece())
		{
			span<sha256_hash> v2_span(v2_blocks);
			st->iothread.async_hash(st->storage, st->piece_counter, v2_span, flags
				, std::bind(&on_hash, std::move(v2_blocks), _1, _2, _3, st));
			++st->piece_counter;
			st->iothread.submit_jobs();
		}
		else if (st->completed_piece == st->ct.end_piece())
		{
			st->iothread.abort(true);
		}
	}

	// this function only works for v2 torrents, where files are guaranteed to
	// be aligned to pieces
	int file_num_pieces(std::int64_t const size, int const piece_length)
	{
		return aux::numeric_cast<int>(
			(static_cast<std::int64_t>(size) + piece_length - 1) / piece_length);
	}

} // anonymous namespace

namespace aux {

	std::string get_symlink_path(std::string const& p)
	{
#if defined TORRENT_WINDOWS
		TORRENT_UNUSED(p);
		return "";
#else
		return get_symlink_path_impl(p.c_str());
#endif
	}

	// arrange files and padding to match the canonical form required
	// by BEP 52
	std::tuple<aux::vector<create_file_entry, file_index_t>, std::int64_t>
	canonicalize(aux::vector<create_file_entry, file_index_t> files
		, int const piece_length
		, bool const backwards_compatible)
	{
		// sort files by path/name
		std::sort(files.begin(), files.end()
			, [](create_file_entry const& lhs, create_file_entry const& rhs)
			{ return lhs.filename < rhs.filename; });

		std::int64_t off = 0;

		if (files.empty())
			return {std::move(files), off};

		std::string const top_level_name
			= std::string(lsplit_path(files.front().filename).first);

		// insert pad files as necessary reserve enough space for the worst case
		// after padding
		aux::vector<create_file_entry, file_index_t> new_files;
		new_files.reserve(files.size() * 2);

		auto add_pad_file = [&](create_file_entry const& fe) {
			if ((off % piece_length) != 0 && fe.size > 0)
			{
				auto const pad_size = piece_length - (off % piece_length);
				TORRENT_ASSERT(pad_size < piece_length);
				TORRENT_ASSERT(pad_size > 0);
				char name[30];

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR "\\"
#else
#define TORRENT_SEPARATOR "/"
#endif
				std::snprintf(name, sizeof(name), ".pad" TORRENT_SEPARATOR "%" PRIu64, pad_size);
				new_files.push_back({combine_path(top_level_name, name)
					, pad_size, file_storage::flag_pad_file, 0, {}});
				off += pad_size;
			}
		};

		for (auto const& fe : files)
		{
			// remove existing pad files
			if (fe.flags & file_storage::flag_pad_file)
				continue;

			if (backwards_compatible)
				add_pad_file(fe);

			new_files.emplace_back(std::move(fe));

			TORRENT_ASSERT(off < file_storage::max_file_offset - static_cast<std::int64_t>(fe.size));
			off += fe.size;

			// when making v2 torrents, pad the end of each file (if necessary) to
			// ensure it ends on a piece boundary.
			// we do this at the end of files rather in-front of files to conform to
			// the BEP52 reference implementation
			// we don't pad single-file torrents. That would make it impossible
			// to have single-file hybrid torrents.
			if (!backwards_compatible && files.size() > 1)
				add_pad_file(fe);
		}

		return {std::move(new_files), off};
	}
} // aux

namespace {
	std::int64_t compute_total_size(span<create_file_entry const> files)
	{
		std::int64_t ret = 0;
		for (auto const& f : files)
		{
			if (std::numeric_limits<std::int64_t>::max() - ret < f.size)
				aux::throw_ex<system_error>(make_error_code(
					boost::system::errc::file_too_large));
			ret += f.size;
		}
		return ret;
	}

	int calc_num_pieces(std::int64_t const total_size, int const piece_size)
	{
		return aux::numeric_cast<int>((total_size + piece_size - 1) / piece_size);
	}

#if TORRENT_ABI_VERSION < 4
	std::vector<create_file_entry> convert_file_storage(file_storage const& fs)
	{
		std::vector<create_file_entry> ret;
		ret.reserve(static_cast<std::size_t>(fs.num_files()));
		for (auto const i : fs.file_range())
		{
			if (fs.file_flags(i) & file_storage::flag_symlink)
				ret.emplace_back(fs.file_path(i), 0, fs.file_flags(i), fs.mtime(i), fs.internal_symlink(i));
			else
				ret.push_back({fs.file_path(i), fs.file_size(i), fs.file_flags(i), fs.mtime(i), {}});
		}
		return ret;
	}
#endif

	file_storage make_file_storage(span<create_file_entry const> files, int const piece_size)
	{
		file_storage ret;
		ret.set_piece_length(piece_size);
		for (auto const& f : files)
			ret.add_file(f.filename, f.size, f.flags);
		ret.set_num_pieces(aux::calc_num_pieces(ret));
		return ret;
	}
}

	std::vector<create_file_entry> list_files(std::string const& file
		, std::function<bool(std::string)> p, create_flags_t const flags)
	{
		std::vector<create_file_entry> ret;
		add_files_impl(ret, parent_path(complete(file)), filename(file), p, flags);
		return ret;
	}

	std::vector<create_file_entry> list_files(std::string const& file
		, create_flags_t const flags)
	{
		std::vector<create_file_entry> ret;
		add_files_impl(ret, parent_path(complete(file)), filename(file)
			, default_pred, flags);
		return ret;
	}

#if TORRENT_ABI_VERSION < 4
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
#endif

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
		settings_pack sett;
		set_piece_hashes(t, p, sett, f, ec);
	}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, settings_pack const& sett
		, std::function<void(piece_index_t)> const& f, error_code& ec)
	{
		set_piece_hashes(t, p, sett, default_disk_io_constructor, f, ec);
	}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, settings_pack const& sett, disk_io_constructor_type disk_io
		, std::function<void(piece_index_t)> const& f, error_code& ec)
	{
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

		if (t.file_list().empty())
		{
			ec = errors::no_files_in_torrent;
			return;
		}

		if (t.total_size() == 0)
		{
			ec = errors::torrent_invalid_length;
			return;
		}

		counters cnt;
		int const num_threads = sett.get_int(settings_pack::hashing_threads);
		std::unique_ptr<disk_interface> disk_thread = disk_io(ios, sett, cnt);
		disk_aborter da(*disk_thread);

		file_storage fs = make_file_storage(t.file_list(), t.piece_length());
		renamed_files rf;

		aux::vector<download_priority_t, file_index_t> priorities;
		storage_params params{
			fs,
			rf,
			path,
			storage_mode_t::storage_mode_sparse,
			priorities,
			sha1_hash{},
			!t.is_v2_only(), // v1-hashes
			!t.is_v1_only() // v2-hashes
		};

		storage_holder storage = disk_thread->new_torrent(params
			, std::shared_ptr<void>());

		// have 4 outstanding hash requests per thread, and no less than 1 MiB
		int const jobs_per_thread = 4;
		int const piece_read_ahead = std::max(num_threads * jobs_per_thread
			, 1 * 1024 * 1024 / t.piece_length());

		hash_state st = { fs, t, std::move(storage), *disk_thread, piece_index_t(0), piece_index_t(0), f, ec };
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
			if (st.piece_counter >= t.end_piece()) break;
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

TORRENT_VERSION_NAMESPACE_4

	create_torrent::~create_torrent() = default;

#if TORRENT_ABI_VERSION < 4
	create_torrent::create_torrent(file_storage& fs, int piece_size
		, create_flags_t const flags)
		: create_torrent(convert_file_storage(fs), piece_size, flags)
	{
		// for backwards compatibility
		fs = make_file_storage(m_files, m_piece_length);
		fs.set_num_pieces(m_num_pieces);
		fs.set_piece_length(m_piece_length);
	}
#endif

#if TORRENT_ABI_VERSION <= 2
	create_torrent::create_torrent(file_storage& fs, int piece_size
		, int, create_flags_t const flags, int)
		: create_torrent(convert_file_storage(fs), piece_size, flags)
	{
		// for backwards compatibility
		fs = make_file_storage(m_files, m_piece_length);
		fs.set_num_pieces(m_num_pieces);
		fs.set_piece_length(m_piece_length);
	}
#endif

	create_torrent::create_torrent(std::vector<create_file_entry> files, int piece_size
		, create_flags_t const flags)
		: m_files(std::move(files))
		, m_total_size(compute_total_size(m_files))
		, m_creation_date(aux::posix_time())
		, m_multifile(m_files.size() > 1)
		, m_private(false)
		, m_include_mtime(bool(flags & create_torrent::modification_time))
		, m_include_symlinks(bool(flags & create_torrent::symlinks))
		, m_v2_only(bool(flags & create_torrent::v2_only))
		, m_v1_only(bool(flags & create_torrent::v1_only))
	{
		// return instead of crash in release mode
		if (m_files.size() == 0)
			aux::throw_ex<system_error>(errors::no_files_in_torrent);

		if (m_total_size == 0)
			aux::throw_ex<system_error>(errors::torrent_invalid_length);

		if (!m_multifile && has_parent_path(m_files.front().filename))
			m_multifile = true;

		m_name = m_multifile
			? std::string(lsplit_path(m_files.front().filename).first)
			: m_files.front().filename;

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
				if (s >= m_total_size) break;
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

		// this is an unreasonably large piece size. Some clients don't support
		// pieces this large.
		if (piece_size > 128 * 1024 * 1024) {
			aux::throw_ex<system_error>(errors::invalid_piece_size);
		}

		m_piece_length = piece_size;
		TORRENT_ASSERT(m_piece_length > 0);
		if (!(flags & v1_only)
			|| (flags & canonical_files)
			|| (flags & canonical_files_no_tail_padding))
			std::tie(m_files, m_total_size) = canonicalize(std::move(m_files), m_piece_length, bool(flags & canonical_files_no_tail_padding));
		m_num_pieces = calc_num_pieces(m_total_size, m_piece_length);
	}

#if TORRENT_ABI_VERSION < 4
	create_torrent::create_torrent(torrent_info const& ti)
		: m_files(convert_file_storage(ti.files()))
		, m_total_size(ti.total_size())
		, m_piece_length(ti.piece_length())
		, m_num_pieces(ti.num_pieces())
		, m_name(ti.name())
		, m_creation_date(aux::posix_time())
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
			add_url_seed(s.url);

		if (make_v1)
		{
			m_piece_hash.resize(ti.num_pieces());
			for (auto const i : ti.piece_range())
				set_hash(i, ti.hash_for_piece(i));
		}

		if (make_v2)
		{
			m_file_piece_hash.resize(m_files.size());
			for (auto const i : file_range())
			{
				// don't include merkle hash trees for pad files
				if (m_files[i].flags & file_storage::flag_pad_file) continue;
				if (m_files[i].size == 0) continue;

				auto const file_size = m_files[i].size;
				if (file_size <= m_piece_length)
				{
					set_hash2(i, piece_index_t::diff_type{0}, ti.files().root(i));
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
#endif

TORRENT_VERSION_NAMESPACE_4_END

namespace {
	bool validate_v2_hashes(
		aux::vector<create_file_entry, file_index_t> const& files
		, aux::vector<aux::vector<sha256_hash, piece_index_t::diff_type>, file_index_t> const& file_piece_hash
		, int const piece_length)
	{
		if (file_piece_hash.size() != files.size()) return false;

		int const piece_size = piece_length;

		for (auto const i : files.range())
		{
			auto const& hashes = file_piece_hash[i];

			// pad files are not supposed to have any hashes
			if (files[i].flags & file_storage::flag_pad_file)
			{
				if (!hashes.empty()) return false;
				continue;
			}

			if (int(hashes.size()) != (files[i].size + piece_size - 1) / piece_size) return false;
			if (std::any_of(hashes.begin(), hashes.end(), [](sha256_hash const& h)
				{ return h.is_all_zeros(); }))
			{
				return false;
			}
		}
		return true;
	}

	bool validate_v1_hashes(aux::vector<sha1_hash, piece_index_t> piece_hash
		, std::int64_t const total_size
		, int const piece_length)
	{
		if (int(piece_hash.size()) != (total_size + piece_length - 1) / piece_length)
			return false;

		return !std::any_of(piece_hash.begin(), piece_hash.end()
			, [](sha1_hash const& h) { return h.is_all_zeros(); });
	}

	std::optional<std::string> get_file_attrs(file_flags_t const flags, bool const include_symlinks)
	{
		if (!(flags & (file_storage::flag_pad_file
			| file_storage::flag_hidden
			| file_storage::flag_executable
			| file_storage::flag_symlink)))
		{
			return std::nullopt;
		}
		std::string attr;
		if (flags & file_storage::flag_pad_file) attr += 'p';
		if (flags & file_storage::flag_hidden) attr += 'h';
		if (flags & file_storage::flag_executable) attr += 'x';
		if (include_symlinks && (flags & file_storage::flag_symlink)) attr += 'l';
		return attr;
	}

	void add_file_attrs(entry& e, file_flags_t const flags, bool const include_symlinks)
	{
		auto attr = get_file_attrs(flags, include_symlinks);
		if (attr) e["attr"] = *attr;
	}

	void add_symlink_path(entry& e, std::string symlink_path)
	{
		entry& sympath_e = e["symlink path"];

		std::string const link = lexically_relative("", symlink_path);
		for (auto elems = lsplit_path(link); !elems.first.empty();
			elems = lsplit_path(elems.second))
			sympath_e.list().emplace_back(elems.first);
	}

	void print_symlink_path(std::vector<char>& out, std::string symlink_path)
	{
		using namespace lt::aux::bencode;
		list path_elements(out);

		std::string const link = lexically_relative("", symlink_path);
		for (auto elems = lsplit_path(link); !elems.first.empty();
			elems = lsplit_path(elems.second))
			path_elements.add(elems.first);
	}
}

TORRENT_VERSION_NAMESPACE_4
	std::vector<char> create_torrent::generate_buf() const
	{
		if (m_files.empty() || m_total_size == 0)
			aux::throw_ex<system_error>(errors::torrent_missing_file_tree);

		// if all v2 hashes are set correctly, generate the v2 parts of the
		// torrent
		bool const make_v2 = validate_v2_hashes(m_files, m_file_piece_hash, m_piece_length);
		bool const make_v1 = validate_v1_hashes(m_piece_hash, m_total_size, m_piece_length);

		// if neither v1 nor v2 hashes were set, we can't create a torrent
		if (!make_v1 && !make_v2)
			aux::throw_ex<system_error>(errors::invalid_hash_entry);

		TORRENT_ASSERT(m_piece_length > 0);

		// compute file roots
		aux::vector<sha256_hash, file_index_t> fileroots;
		if (make_v2)
		{
			TORRENT_ASSERT(!m_file_piece_hash.empty());
			fileroots.resize(m_files.size());
			sha256_hash const pad_hash = merkle_pad(m_piece_length / default_block_size, 1);

			for (file_index_t fi : file_range())
			{
				if (m_files[fi].flags & file_storage::flag_pad_file) continue;
				if (m_files[fi].size == 0) continue;

				fileroots[fi] = merkle_root(m_file_piece_hash[fi], pad_hash);
			}
		}

		std::vector<char> ret;
		{
			using namespace lt::aux::bencode;

			dict torrent_file(ret);

			if (!m_urls.empty())
				torrent_file.add("announce", m_urls.front().first);

			if (m_urls.size() > 1)
			{
				torrent_file.add_key("announce-list");
				list announce_list(ret);
				list tier_list(ret);

				int current_tier = m_urls.front().second;
				for (auto const& url : m_urls)
				{
					if (url.second != current_tier)
					{
						ret.push_back('e');
						ret.push_back('l');
						current_tier = url.second;
					}
					tier_list.add(url.first);
				}
			}

			if (!m_comment.empty())
				torrent_file.add("comment", m_comment);

			if (!m_created_by.empty())
				torrent_file.add("created by", m_created_by);

			if (m_creation_date != 0)
				torrent_file.add("creation date", m_creation_date);

			torrent_file.add_key("info");
#if TORRENT_ABI_VERSION < 4
			if (m_info_dict.type() == entry::preformatted_t)
			{
				auto const& pre = m_info_dict.preformatted();
				ret.insert(ret.end(), pre.begin(), pre.end());
			}
			else
#endif

			{
				dict info(ret);

				if (make_v1 && !m_multifile)
				{
					auto attrs = get_file_attrs(m_files.front().flags, m_include_symlinks);
					if (attrs) info.add("attr", *attrs);
				}

				if (!m_collections.empty())
				{
					info.add_key("collections");
					list coll(ret);
					for (auto const& c : m_collections)
						coll.add(c);
				}

				if (make_v2)
				{
					info.add_key("file tree");
					dict tree(ret);

					// the file indices ordered by path, to create a correctly
					// ordered file tree
					std::vector<file_index_t> sorted_files;
					sorted_files.reserve(m_files.size());
					for (file_index_t i : file_range())
					{
						if (m_files[i].flags & file_storage::flag_pad_file) continue;
						sorted_files.push_back(i);
					}

					std::sort(sorted_files.begin(), sorted_files.end()
						, [&](file_index_t lhs, file_index_t rhs)
						{ return m_files[lhs].filename < m_files[rhs].filename; });

					std::vector<string_view> current_path;
					for (file_index_t i : sorted_files)
					{
						create_file_entry const& file = m_files[i];

						TORRENT_ASSERT(!(file.flags & file_storage::flag_pad_file));

						std::string const& file_path = file.filename;
						auto const split = m_multifile
							? lsplit_path(file_path)
							: std::pair<string_view, string_view>(file_path, file_path);
						TORRENT_ASSERT(split.first == m_name);

						std::size_t depth = 0;
						bool extended_branch = false;
						// the first file we add we'll only extend the branch,
						// se we initialize this to true since it's not a
						// collision yet
						bool truncated_branch = current_path.empty();
						for (auto e = lsplit_path(split.second);
							!e.first.empty();
							e = lsplit_path(e.second))
						{
							if (depth < current_path.size())
							{
								if (current_path[depth] == e.first)
								{
									++depth;
									continue;
								}
								TORRENT_ASSERT(current_path[depth] != e.first);

								while (current_path.size() > depth)
								{
									truncated_branch = true;
									ret.push_back('e');
									current_path.pop_back();
								}
							}
							if (depth == current_path.size())
							{
								extended_branch = true;
								tree.add_key(e.first);
								current_path.push_back(e.first);
								++depth;
								ret.push_back('d');
								continue;
							}
						}

						if (!extended_branch || !truncated_branch)
						{
							// you can't creata a torrent where a file name
							// conflicts with a directory or another file
							aux::throw_ex<system_error>(errors::torrent_inconsistent_files);
						}

						tree.add_key("");
						dict file_entry(ret);

						auto attrs = get_file_attrs(file.flags, m_include_symlinks);
						if (attrs) file_entry.add("attr", *attrs);

						if (!m_include_symlinks
							|| !(file.flags & file_storage::flag_symlink))
							file_entry.add("length", file.size);

						if (m_include_mtime && file.mtime)
							file_entry.add("mtime", file.mtime);

						if (m_include_symlinks
							&& (file.flags & file_storage::flag_symlink))
						{
							file_entry.add_key("symlink path");
							print_symlink_path(ret, file.symlink);
						}
						else
						{
							if (file.size > 0)
								file_entry.add("pieces root", fileroots[i].to_string());
						}
					}

					while (!current_path.empty())
					{
						ret.push_back('e');
						current_path.pop_back();
					}
				}

				if (make_v1 && m_multifile)
				{
					info.add_key("files");
					list files(ret);

					for (auto const i : file_range())
					{
						dict file_e(ret);

						auto attrs = get_file_attrs(m_files[i].flags, m_include_symlinks);
						if (attrs) file_e.add("attr", *attrs);

						file_e.add("length", m_files[i].size);

						if (m_include_mtime && m_files[i].mtime)
							file_e.add("mtime", m_files[i].mtime);

						TORRENT_ASSERT(has_parent_path(m_files[i].filename));
						{
							file_e.add_key("path");
							list path_e(ret);

							std::string const p = m_files[i].filename;
							// deliberately skip the first path element, since that's the
							// "name" of the torrent already
							string_view path = lsplit_path(p).second;
							for (auto elems = lsplit_path(path); !elems.first.empty(); elems = lsplit_path(elems.second))
								path_e.add(elems.first);
						}

#if TORRENT_ABI_VERSION < 3
						if (!m_filehashes.empty() && m_filehashes[i] != sha1_hash())
							file_e.add("sha1", m_filehashes[i].to_string());
#endif
						if (m_include_symlinks && (m_files[i].flags & file_storage::flag_symlink))
						{
							file_e.add_key("symlink path");
							print_symlink_path(ret, m_files[i].symlink);
						}
					}
				}

				if (make_v1 && !m_multifile)
					info.add("length", m_files.front().size);

				if (make_v2) info.add("meta version", 2);

				info.add("name", m_name);
				info.add("piece length", m_piece_length);

				if (make_v1)
				{
					info.add("pieces", string_view(reinterpret_cast<char const*>(m_piece_hash.data())
						, m_piece_hash.size() * sha1_hash::size()));
				}

				if (m_private)
					info.add("private", 1);

				if (!m_similar.empty())
				{
					info.add_key("similar");
					list sim(ret);
					for (auto const& ih : m_similar)
						sim.add(ih.to_string());
				}

				if (!m_root_cert.empty())
					info.add("ssl-cert", m_root_cert);

				if (make_v1 && !m_multifile)
				{
					if (m_include_mtime && m_files.front().mtime)
						info.add("mtime", m_files.front().mtime);

#if TORRENT_ABI_VERSION < 3
					if (!m_filehashes.empty())
						info.add("sha1", m_filehashes.front().to_string());
#endif
					if (m_include_symlinks
						&& (m_files.front().flags & file_storage::flag_symlink))
					{
						info.add_key("symlink path");
						print_symlink_path(ret, m_files.front().symlink);
					}
				}
			}

			if (!m_nodes.empty())
			{
				torrent_file.add_key("nodes");
				list nodes(ret);
				for (auto const& n : m_nodes)
				{
					list entry(ret);
					entry.add(n.first);
					entry.add(n.second);
				}
			}

			if (make_v2)
			{
				torrent_file.add_key("piece layers");
				dict file_pieces(ret);

				// the keys in this dictionary are file roots
				// in order to print the keys ordered correctly
				// we need to first create a sorted list of all file roots
				std::map<sha256_hash, file_index_t> sorted_roots;
				for (file_index_t fi : file_range())
				{
					if (m_files[fi].flags & file_storage::flag_pad_file) continue;
					if (m_files[fi].size == 0) continue;

					// files that only have one piece store the piece hash as the
					// root, we don't need a pieces layer entry for such files
					if (m_file_piece_hash[fi].size() < 2) continue;
					sorted_roots.emplace(fileroots[fi], fi);
				}

				std::string pieces;
				for (auto [root, fi] : sorted_roots)
				{
					// all the hashes are contiguous in memory, just copy them
					// in one go
					auto const& p = m_file_piece_hash[fi];
					pieces.assign(reinterpret_cast<const char*>(p.data())
						, p.size() * sha256_hash::size());
					file_pieces.add(root.to_string(), pieces);
				}
			}

			if (!m_url_seeds.empty())
			{
				torrent_file.add_key("url-list");
				if (m_url_seeds.size() == 1)
					torrent_file.add_value(m_url_seeds.front());
				else
				{
					list url_list(ret);
					for (auto const& url : m_url_seeds)
					{
						url_list.add(url);
					}
				}
			}
		}

#if TORRENT_USE_ASSERTS
		try {
			auto const expected = bencode(generate());
			TORRENT_ASSERT(ret == expected);
		} catch (...)
		{
			TORRENT_ASSERT_FAIL();
		}
#endif
		return ret;
	}

	entry create_torrent::generate() const
	{
		if (m_files.empty() || m_total_size == 0)
			aux::throw_ex<system_error>(errors::torrent_missing_file_tree);

		// if all v2 hashes are set correctly, generate the v2 parts of the
		// torrent
		bool const make_v2 = validate_v2_hashes(m_files, m_file_piece_hash, m_piece_length);
		bool const make_v1 = validate_v1_hashes(m_piece_hash, m_total_size, m_piece_length);

		// if neither v1 nor v2 hashes were set, we can't create a torrent
		if (!make_v1 && !make_v2)
			aux::throw_ex<system_error>(errors::invalid_hash_entry);

		TORRENT_ASSERT(m_piece_length > 0);

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

		aux::vector<sha256_hash, file_index_t> fileroots;
		if (make_v2)
		{
			TORRENT_ASSERT(!m_file_piece_hash.empty());
			fileroots.resize(m_files.size());

			sha256_hash const pad_hash = merkle_pad(m_piece_length / default_block_size, 1);
			auto& file_pieces = dict["piece layers"].dict();

			for (file_index_t fi : file_range())
			{
				if (m_files[fi].flags & file_storage::flag_pad_file) continue;
				if (m_files[fi].size == 0) continue;

				fileroots[fi] = merkle_root(m_file_piece_hash[fi], pad_hash);

				// files that only have one piece store the piece hash as the
				// root, we don't need a pieces layer entry for such files
				if (m_file_piece_hash[fi].size() < 2) continue;
				auto& pieces = file_pieces[fileroots[fi].to_string()].string();
				pieces.clear();
				pieces.reserve(m_file_piece_hash[fi].size() * sha256_hash::size());
				for (auto const& p : m_file_piece_hash[fi])
					pieces.append(reinterpret_cast<const char*>(p.data()), p.size());
			}
		}

		entry& info = dict["info"];
#if TORRENT_ABI_VERSION < 4
		if (m_info_dict.type() == entry::dictionary_t
			|| m_info_dict.type() == entry::preformatted_t)
		{
			info = m_info_dict;
			return dict;
		}
#endif

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
				list.list().emplace_back(ih);
			}
		}

		info["name"] = m_name;

		if (!m_root_cert.empty())
			info["ssl-cert"] = m_root_cert;

		if (m_private) info["private"] = 1;

		if (make_v1)
		{
			if (!m_multifile)
			{
				auto const& file = m_files.front();
				if (m_include_mtime && file.mtime)
					info["mtime"] = file.mtime;
				info["length"] = file.size;
				file_flags_t const flags = file.flags;
				add_file_attrs(info, flags, m_include_symlinks);
				if (m_include_symlinks
					&& (flags & file_storage::flag_symlink))
				{
					add_symlink_path(info, file.symlink);
				}
#if TORRENT_ABI_VERSION < 3
				if (!m_filehashes.empty())
				{
					info["sha1"] = m_filehashes.front();
				}
#endif
			}
			else
			{
				entry& files = info["files"];

				for (auto const i : file_range())
				{
					files.list().emplace_back();
					entry& file_e = files.list().back();
					if (m_include_mtime && m_files[i].mtime) file_e["mtime"] = m_files[i].mtime;
					file_e["length"] = m_files[i].size;

					TORRENT_ASSERT(has_parent_path(m_files[i].filename));

					{
						entry& path_e = file_e["path"];

						std::string const p = m_files[i].filename;
						// deliberately skip the first path element, since that's the
						// "name" of the torrent already
						string_view path = lsplit_path(p).second;
						for (auto elems = lsplit_path(path); !elems.first.empty(); elems = lsplit_path(elems.second))
							path_e.list().emplace_back(elems.first);
					}

					file_flags_t const flags = m_files[i].flags;
					add_file_attrs(file_e, flags, m_include_symlinks);

					if (m_include_symlinks && (flags & file_storage::flag_symlink))
					{
						add_symlink_path(file_e, m_files[i].symlink);
					}
#if TORRENT_ABI_VERSION < 3
					if (!m_filehashes.empty() && m_filehashes[i] != sha1_hash())
					{
						file_e["sha1"] = m_filehashes[i];
					}
#endif
				}
			}
		}

		if (make_v2)
		{
			auto& tree = info["file tree"];

			for (file_index_t i : file_range())
			{
				file_flags_t const flags = m_files[i].flags;
				if (flags & file_storage::flag_pad_file) continue;

				entry* file_e_ptr = &tree;

				{
					std::string const file_path = m_files[i].filename;
					auto const split = m_multifile
						? lsplit_path(file_path)
						: std::pair<string_view, string_view>(file_path, file_path);
					TORRENT_ASSERT(split.first == m_name);

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

				if (m_include_mtime && m_files[i].mtime) file_e["mtime"] = m_files[i].mtime;

				add_file_attrs(file_e, flags, m_include_symlinks);

				if (m_include_symlinks && (flags & file_storage::flag_symlink))
				{
					add_symlink_path(file_e, m_files[i].symlink);
				}
				else
				{
					if (m_files[i].size > 0)
						file_e["pieces root"] = fileroots[i];
					file_e["length"] = m_files[i].size;
				}
			}
			info["meta version"] = 2;
		}

		info["piece length"] = m_piece_length;

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
		m_urls.emplace_back(url, tier);

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
			m_piece_hash.resize(m_num_pieces);

		TORRENT_ASSERT_PRECOND(index >= piece_index_t(0));
		TORRENT_ASSERT_PRECOND(index < m_piece_hash.end_index());
		m_piece_hash[index] = h;
	}

	void create_torrent::set_hash2(file_index_t const file
		, piece_index_t::diff_type const piece
		, sha256_hash const& h)
	{
		TORRENT_ASSERT_PRECOND(file >= file_index_t(0));
		TORRENT_ASSERT_PRECOND(file < end_file());
		TORRENT_ASSERT_PRECOND(piece >= piece_index_t::diff_type(0));
		TORRENT_ASSERT_PRECOND(!h.is_all_zeros());

		auto const& f = m_files[file];

		TORRENT_ASSERT_PRECOND(!(f.flags & file_storage::flag_pad_file));
		TORRENT_ASSERT_PRECOND(piece < piece_index_t::diff_type(file_num_pieces(f.size, m_piece_length)));
		TORRENT_ASSERT_PRECOND(file_num_pieces(f.size, m_piece_length) > 0);

		if (m_v1_only)
			aux::throw_ex<system_error>(errors::invalid_hash_entry);

		if (m_file_piece_hash.empty())
			m_file_piece_hash.resize(m_files.size());

		auto& fh = m_file_piece_hash[file];
		if (fh.empty())
			fh.resize(std::size_t(file_num_pieces(f.size, m_piece_length)));
		fh[piece] = h;
	}

#if TORRENT_ABI_VERSION < 3
	void create_torrent::set_file_hash(file_index_t index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= file_index_t(0));
		TORRENT_ASSERT(index < m_files.end_index());
		if (m_filehashes.empty()) m_filehashes.resize(m_files.size());
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

#if TORRENT_ABI_VERSION < 4
	void create_torrent::add_http_seed(string_view) {}

	file_storage const& create_torrent::files() const
	{
		if (!m_file_storage_compat)
		{
			m_file_storage_compat = make_file_storage(m_files, m_piece_length);
			m_file_storage_compat->set_num_pieces(m_num_pieces);
			m_file_storage_compat->set_piece_length(m_piece_length);
		}
		return *m_file_storage_compat;
	}
#endif

	int create_torrent::piece_size(piece_index_t const i) const
	{
		TORRENT_ASSERT_PRECOND(i >= piece_index_t(0) && i < end_piece());
		if (i != piece_index_t{m_num_pieces - 1})
			return m_piece_length;

		std::int64_t const size_except_last
			= (m_num_pieces - 1) * std::int64_t(m_piece_length);
		std::int64_t const size = m_total_size - size_except_last;
		TORRENT_ASSERT(size > 0);
		TORRENT_ASSERT(size <= m_piece_length);
		return int(size);
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

TORRENT_VERSION_NAMESPACE_4_END

}
