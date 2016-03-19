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
#include "libtorrent/file_pool.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_to_wstring
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/aux_/merkle.hpp" // for merkle_*()
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_manager.hpp"

#include <boost/bind.hpp>
#include <boost/next_prior.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

#include <sys/types.h>
#include <sys/stat.h>

#define MAX_SYMLINK_PATH 200

namespace libtorrent
{

	class alert;

	namespace
	{
		inline bool default_pred(std::string const&) { return true; }

		inline bool ignore_subdir(std::string const& leaf)
		{ return leaf == ".." || leaf == "."; }

		int get_file_attributes(std::string const& p)
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
			if (lstat(convert_to_native(p).c_str(), &s) < 0) return 0;
			int file_attr = 0;
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
			char buf[MAX_SYMLINK_PATH];
			std::string f = convert_to_native(path);
			int char_read = readlink(f.c_str(),buf,MAX_SYMLINK_PATH);
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
			std::string path = convert_to_native(p);
			return get_symlink_path_impl(p.c_str());
#endif
		}

		void add_files_impl(file_storage& fs, std::string const& p
			, std::string const& l, boost::function<bool(std::string)> pred, boost::uint32_t flags)
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
				int file_flags = get_file_attributes(f);

				// mask all bits to check if the file is a symlink
				if ((file_flags & file_storage::attribute_symlink)
					&& (flags & create_torrent::symlinks)) 
				{
					std::string sym_path = get_symlink_path(f);
					fs.add_file(l, 0, file_flags, s.mtime, sym_path);
				}
				else
				{
					fs.add_file(l, s.file_size, file_flags, s.mtime);
				}
			}
		}

		void on_hash(disk_io_job const* j, create_torrent* t
			, boost::shared_ptr<piece_manager> storage, disk_io_thread* iothread
			, int* piece_counter, int* completed_piece
			, boost::function<void(int)> const* f, error_code* ec)
		{
			if (j->ret != 0)
			{
				// on error
				*ec = j->error.ec;
				iothread->set_num_threads(0);
				return;
			}
			t->set_hash(j->piece, sha1_hash(j->d.piece_hash));
			(*f)(*completed_piece);
			++(*completed_piece);
			if (*piece_counter < t->num_pieces())
			{
				iothread->async_hash(storage.get(), *piece_counter
					, disk_io_job::sequential_access
					, boost::bind(&on_hash, _1, t, storage, iothread
					, piece_counter, completed_piece, f, ec), NULL);
				++(*piece_counter);
			}
			else
			{
				iothread->abort(true);
			}
			iothread->submit_jobs();
		}

	} // anonymous namespace

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE

	void add_files(file_storage& fs, std::wstring const& wfile
		, boost::function<bool(std::string)> p, boost::uint32_t flags)
	{
		std::string utf8;
		wchar_utf8(wfile, utf8);
		add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), p, flags);
	}

	void add_files(file_storage& fs
		, std::wstring const& wfile, boost::uint32_t flags)
	{
		std::string utf8;
		wchar_utf8(wfile, utf8);
		add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), default_pred, flags);
	}

	void set_piece_hashes(create_torrent& t, std::wstring const& p
		, boost::function<void(int)> f, error_code& ec)
	{
		std::string utf8;
		wchar_utf8(p, utf8);
		set_piece_hashes(t, utf8, f, ec);
	}

	void set_piece_hashes_deprecated(create_torrent& t, std::wstring const& p
		, boost::function<void(int)> f, error_code& ec)
	{
		std::string utf8;
		wchar_utf8(p, utf8);
		set_piece_hashes(t, utf8, f, ec);
	}
#endif
#endif

	void add_files(file_storage& fs, std::string const& file
		, boost::function<bool(std::string)> p, boost::uint32_t flags)
	{
		add_files_impl(fs, parent_path(complete(file)), filename(file), p, flags);
	}

	void add_files(file_storage& fs, std::string const& file, boost::uint32_t flags)
	{
		add_files_impl(fs, parent_path(complete(file)), filename(file)
			, default_pred, flags);
	}

	void set_piece_hashes(create_torrent& t, std::string const& p
		, boost::function<void(int)> const& f, error_code& ec)
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
			ec = error_code(errors::no_files_in_torrent, get_libtorrent_category());
			return;
		}

		// dummy torrent object pointer
		boost::shared_ptr<char> dummy;
		counters cnt;
		disk_io_thread disk_thread(ios, cnt, 0);
		disk_thread.set_num_threads(1);

		storage_params params;
		params.files = &t.files();
		params.mapped_files = NULL;
		params.path = path;
		params.pool = &disk_thread.files();
		params.mode = storage_mode_sparse;

		storage_interface* storage_impl = default_storage_constructor(params);

		boost::shared_ptr<piece_manager> storage = boost::make_shared<piece_manager>(
			storage_impl, dummy, const_cast<file_storage*>(&t.files()));

		settings_pack sett;
		sett.set_int(settings_pack::cache_size, 0);
		sett.set_int(settings_pack::aio_threads, 2);

		// TODO: this should probably be optional
		alert_manager dummy2(0, 0);
		disk_thread.set_settings(&sett, dummy2);

		int piece_counter = 0;
		int completed_piece = 0;
		int piece_read_ahead = 15 * 1024 * 1024 / t.piece_length();
		if (piece_read_ahead < 1) piece_read_ahead = 1;

		for (int i = 0; i < piece_read_ahead; ++i)
		{
			disk_thread.async_hash(storage.get(), i, disk_io_job::sequential_access
				, boost::bind(&on_hash, _1, &t, storage, &disk_thread
				, &piece_counter, &completed_piece, &f, &ec), NULL);
			++piece_counter;
			if (piece_counter >= t.num_pieces()) break;
		}
		disk_thread.submit_jobs();
		ios.run(ec);
	}

	create_torrent::~create_torrent() {}

	create_torrent::create_torrent(file_storage& fs, int piece_size
		, int pad_file_limit, int flags, int alignment)
		: m_files(fs)
		, m_creation_date(time(0))
		, m_multifile(fs.num_files() > 1)
		, m_private(false)
		, m_merkle_torrent((flags & merkle) != 0)
		, m_include_mtime((flags & modification_time) != 0)
		, m_include_symlinks((flags & symlinks) != 0)
	{
		TORRENT_ASSERT(fs.num_files() > 0);

		// return instead of crash in release mode
		if (fs.num_files() == 0) return;

		if (!m_multifile && has_parent_path(m_files.file_path(0))) m_multifile = true;

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
		if (ti.creation_date()) m_creation_date = *ti.creation_date();

		if (!ti.creator().empty()) set_creator(ti.creator().c_str());
		if (!ti.comment().empty()) set_comment(ti.comment().c_str());

		torrent_info::nodes_t const& nodes = ti.nodes();
		for (torrent_info::nodes_t::const_iterator i = nodes.begin()
			, end(nodes.end()); i != end; ++i)
			add_node(*i);

		std::vector<libtorrent::announce_entry> const& trackers = ti.trackers();
		for (std::vector<libtorrent::announce_entry>::const_iterator i = trackers.begin()
			, end(trackers.end()); i != end; ++i)
			add_tracker(i->url, i->tier);

		std::vector<web_seed_entry> const& web_seeds = ti.web_seeds();
		for (std::vector<web_seed_entry>::const_iterator i = web_seeds.begin()
			, end(web_seeds.end()); i != end; ++i)
		{
			if (i->type == web_seed_entry::url_seed)
				add_url_seed(i->url);
			else if (i->type == web_seed_entry::http_seed)
				add_http_seed(i->url);
		}

		m_piece_hash.resize(m_files.num_pieces());
		for (int i = 0; i < num_pieces(); ++i) set_hash(i, ti.hash_for_piece(i));

		m_info_dict = bdecode(&ti.metadata()[0], &ti.metadata()[0] + ti.metadata_size());
		m_info_hash = ti.info_hash();
	}

	entry create_torrent::generate() const
	{
		TORRENT_ASSERT(m_files.piece_length() > 0);

		entry dict;

		if (m_files.num_files() == 0)
			return dict;

		if (!m_urls.empty()) dict["announce"] = m_urls.front().first;
		
		if (!m_nodes.empty())
		{
			entry& nodes = dict["nodes"];
			entry::list_type& nodes_list = nodes.list();
			for (nodes_t::const_iterator i = m_nodes.begin()
				, end(m_nodes.end()); i != end; ++i)
			{
				entry::list_type node;
				node.push_back(entry(i->first));
				node.push_back(entry(i->second));
				nodes_list.push_back(entry(node));
			}
		}

		if (m_urls.size() > 1)
		{
			entry trackers(entry::list_t);
			entry tier(entry::list_t);
			int current_tier = m_urls.front().second;
			for (std::vector<announce_entry>::const_iterator i = m_urls.begin();
				i != m_urls.end(); ++i)
			{
				if (i->second != current_tier)
				{
					current_tier = i->second;
					trackers.list().push_back(tier);
					tier.list().clear();
				}
				tier.list().push_back(entry(i->first));
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
				for (std::vector<std::string>::const_iterator i
					= m_url_seeds.begin(); i != m_url_seeds.end(); ++i)
				{
					list.list().push_back(entry(*i));
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
				for (std::vector<std::string>::const_iterator i
					= m_http_seeds.begin(); i != m_http_seeds.end(); ++i)
				{
					list.list().push_back(entry(*i));
				}
			}
		}

		entry& info = dict["info"];
		if (m_info_dict.type() == entry::dictionary_t)
		{
			info = m_info_dict;
			return dict;
		}

		if (!m_collections.empty())
		{
			entry& list = info["collections"];
			for (std::vector<std::string>::const_iterator i
				= m_collections.begin(); i != m_collections.end(); ++i)
			{
				list.list().push_back(entry(*i));
			}
		}

		if (!m_similar.empty())
		{
			entry& list = info["similar"];
			for (std::vector<sha1_hash>::const_iterator i
				= m_similar.begin(); i != m_similar.end(); ++i)
			{
				list.list().push_back(entry(i->to_string()));
			}
		}

		info["name"] = m_files.name();

		if (!m_root_cert.empty())
			info["ssl-cert"] = m_root_cert;

		if (m_private) info["private"] = 1;

		if (!m_multifile)
		{
			if (m_include_mtime) info["mtime"] = m_files.mtime(0);
			info["length"] = m_files.file_size(0);
			int flags = m_files.file_flags(0);
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

				std::string split = split_path(m_files.symlink(0));
				for (char const* e = split.c_str(); e != 0; e = next_path_element(e))
					sympath_e.list().push_back(entry(e));
			}
			if (!m_filehashes.empty())
			{
				info["sha1"] = m_filehashes[0].to_string();
			}
		}
		else
		{
			if (!info.find_key("files"))
			{
				entry& files = info["files"];

				for (int i = 0; i < m_files.num_files(); ++i)
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
							e != 0; e = next_path_element(e))
							path_e.list().push_back(entry(e));
					}

					int flags = m_files.file_flags(i);
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
						for (char const* e = split.c_str(); e != 0; e = next_path_element(e))
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
			int num_leafs = merkle_num_leafs(m_files.num_pieces());
			int num_nodes = merkle_num_nodes(num_leafs);
			int first_leaf = num_nodes - num_leafs;
			m_merkle_tree.resize(num_nodes);
			int num_pieces = m_piece_hash.size();
			for (int i = 0; i < num_pieces; ++i)
				m_merkle_tree[first_leaf + i] = m_piece_hash[i];
			sha1_hash filler(0);
			for (int i = num_pieces; i < num_leafs; ++i)
				m_merkle_tree[first_leaf + i] = filler;

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
					h.update(m_merkle_tree[i].data(), 20);
					h.update(m_merkle_tree[i+1].data(), 20);
					m_merkle_tree[parent] = h.final();
				}
				level_start = merkle_get_parent(level_start);
				level_size /= 2;
			}
			TORRENT_ASSERT(level_size == 1);
			std::string& p = info["root hash"].string();
			p.assign(m_merkle_tree[0].data(), 20);
		}
		else
		{
			std::string& p = info["pieces"].string();

			for (std::vector<sha1_hash>::const_iterator i = m_piece_hash.begin();
				i != m_piece_hash.end(); ++i)
			{
				p.append(i->data(), sha1_hash::size);
			}
		}

		std::vector<char> buf;
		bencode(std::back_inserter(buf), info);
		m_info_hash = hasher(&buf[0], buf.size()).final();

		return dict;
	}

	void create_torrent::add_tracker(std::string const& url, int tier)
	{
		m_urls.push_back(announce_entry(url, tier));

		std::sort(m_urls.begin(), m_urls.end()
			, boost::bind(&announce_entry::second, _1) < boost::bind(&announce_entry::second, _2));
	}

	void create_torrent::set_root_cert(std::string const& cert)
	{
		m_root_cert = cert;
	}

	void create_torrent::add_similar_torrent(sha1_hash ih)
	{
		m_similar.push_back(ih);
	}

	void create_torrent::add_collection(std::string c)
	{
		m_collections.push_back(c);
	}

	void create_torrent::set_hash(int index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_hash.size()));
		m_piece_hash[index] = h;
	}

	void create_torrent::set_file_hash(int index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_files.num_files()));
		if (m_filehashes.empty()) m_filehashes.resize(m_files.num_files());
		m_filehashes[index] = h;
	}

	void create_torrent::add_node(std::pair<std::string, int> const& node)
	{
		m_nodes.push_back(node);
	}

	void create_torrent::add_url_seed(std::string const& url)
	{
		m_url_seeds.push_back(url);
	}

	void create_torrent::add_http_seed(std::string const& url)
	{
		m_http_seeds.push_back(url);
	}

	void create_torrent::set_comment(char const* str)
	{
		if (str == 0) m_comment.clear();
		else m_comment = str;
	}

	void create_torrent::set_creator(char const* str)
	{
		if (str == 0) m_created_by.clear();
		else m_created_by = str;
	}

}

