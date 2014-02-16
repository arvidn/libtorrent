/*

Copyright (c) 2008, Arvid Norberg
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
#include "libtorrent/escape_string.hpp"
#include "libtorrent/torrent_info.hpp" // for merkle_*()

#include <boost/bind.hpp>
#include <boost/next_prior.hpp>

#include <sys/types.h>
#include <sys/stat.h>

#define MAX_SYMLINK_PATH 200

namespace libtorrent
{

	namespace detail
	{
		int get_file_attributes(std::string const& p)
		{
#ifdef TORRENT_WINDOWS

#if TORRENT_USE_WSTRING
			std::wstring path = convert_to_wstring(p);
			DWORD attr = GetFileAttributesW(path.c_str());
#else
			std::string path = convert_to_native(p);
			DWORD attr = GetFileAttributesA(path.c_str());
#endif // TORRENT_USE_WSTRING
			if (attr == INVALID_FILE_ATTRIBUTES) return 0;
			if (attr & FILE_ATTRIBUTE_HIDDEN) return file_storage::attribute_hidden;
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
	}

	struct piece_holder
	{
		piece_holder(int bytes): m_piece(page_aligned_allocator::malloc(bytes)) {}
		~piece_holder() { page_aligned_allocator::free(m_piece); }
		char* bytes() { return m_piece; }
	private:
		char* m_piece;
	};

#if TORRENT_USE_WSTRING
	void set_piece_hashes(create_torrent& t, std::wstring const& p
		, boost::function<void(int)> const& f, error_code& ec)
	{
		file_pool fp;
		std::string utf8;
		wchar_utf8(p, utf8);
#if TORRENT_USE_UNC_PATHS
		utf8 = canonicalize_path(utf8);
#endif
		boost::scoped_ptr<storage_interface> st(
			default_storage_constructor(const_cast<file_storage&>(t.files()), 0, utf8, fp
			, std::vector<boost::uint8_t>()));

		// calculate the hash for all pieces
		int num = t.num_pieces();
		std::vector<char> buf(t.piece_length());
		for (int i = 0; i < num; ++i)
		{
			// read hits the disk and will block. Progress should
			// be updated in between reads
			st->read(&buf[0], i, 0, t.piece_size(i));
			if (st->error())
			{
				ec = st->error();
				return;
			}
			hasher h(&buf[0], t.piece_size(i));
			t.set_hash(i, h.final());
			f(i);
		}
	}
#endif

	void set_piece_hashes(create_torrent& t, std::string const& p
		, boost::function<void(int)> f, error_code& ec)
	{
		file_pool fp;
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

		boost::scoped_ptr<storage_interface> st(
			default_storage_constructor(const_cast<file_storage&>(t.files()), 0, path, fp
			, std::vector<boost::uint8_t>()));

		// if we're calculating file hashes as well, use this hasher
		hasher filehash;
		int file_idx = 0;
		size_type left_in_file = t.files().at(0).size;

		// calculate the hash for all pieces
		int num = t.num_pieces();
		piece_holder buf(t.piece_length());
		for (int i = 0; i < num; ++i)
		{
			// read hits the disk and will block. Progress should
			// be updated in between reads
			st->read(buf.bytes(), i, 0, t.piece_size(i));
			if (st->error())
			{
				ec = st->error();
				return;
			}
			
			if (t.should_add_file_hashes())
			{
				int left_in_piece = t.piece_size(i);
				int this_piece_size = left_in_piece;
				// the number of bytes from this file we just read
				while (left_in_piece > 0)
				{
					int to_hash_for_file = int((std::min)(size_type(left_in_piece), left_in_file));
					if (to_hash_for_file > 0)
					{
						int offset = this_piece_size - left_in_piece;
						filehash.update(buf.bytes() + offset, to_hash_for_file);
					}
					left_in_file -= to_hash_for_file;
					left_in_piece -= to_hash_for_file;
					if (left_in_file == 0)
					{
						if (!t.files().at(file_idx).pad_file)
							t.set_file_hash(file_idx, filehash.final());
						filehash.reset();
						file_idx++;
						if (file_idx >= t.files().num_files()) break;
						left_in_file = t.files().at(file_idx).size;
					}
				}
			}

			hasher h(buf.bytes(), t.piece_size(i));
			t.set_hash(i, h.final());
			f(i);
		}
	}

	create_torrent::~create_torrent() {}

	create_torrent::create_torrent(file_storage& fs, int piece_size, int pad_file_limit, int flags)
		: m_files(fs)
		, m_creation_date(time(0))
		, m_multifile(fs.num_files() > 1)
		, m_private(false)
		, m_merkle_torrent((flags & merkle) != 0)
		, m_include_mtime((flags & modification_time) != 0)
		, m_include_symlinks((flags & symlinks) != 0)
		, m_calculate_file_hashes((flags & calculate_file_hashes) != 0)
	{
		TORRENT_ASSERT(fs.num_files() > 0);

		// return instead of crash in release mode
		if (fs.num_files() == 0) return;

		if (!m_multifile && has_parent_path(m_files.file_path(*m_files.begin()))) m_multifile = true;

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
		if (flags & optimize)
			m_files.optimize(pad_file_limit);
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
		, m_calculate_file_hashes(false)
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

		info["name"] = m_files.name();

		if (!m_root_cert.empty())
			info["ssl-cert"] = m_root_cert;

		if (m_private) info["private"] = 1;

		if (!m_multifile)
		{
			file_entry e = m_files.at(0);
			if (m_include_mtime) info["mtime"] = e.mtime;
			info["length"] = e.size;
			if (e.pad_file
				|| e.hidden_attribute
				|| e.executable_attribute
				|| e.symlink_attribute)
			{
				std::string& attr = info["attr"].string();
				if (e.pad_file) attr += 'p';
				if (e.hidden_attribute) attr += 'h';
				if (e.executable_attribute) attr += 'x';
				if (m_include_symlinks && e.symlink_attribute) attr += 'l';
			}
			if (m_include_symlinks
				&& e.symlink_attribute)
			{
				entry& sympath_e = info["symlink path"];
				
				std::string split = split_path(e.symlink_path);
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

				for (file_storage::iterator i = m_files.begin();
					i != m_files.end(); ++i)
				{
					files.list().push_back(entry());
					entry& file_e = files.list().back();
					if (m_include_mtime && m_files.mtime(*i)) file_e["mtime"] = m_files.mtime(*i); 
					file_e["length"] = i->size;
					entry& path_e = file_e["path"];

					TORRENT_ASSERT(has_parent_path(m_files.file_path(*i)));

					std::string split = split_path(m_files.file_path(*i));
					TORRENT_ASSERT(split.c_str() == m_files.name());

					for (char const* e = next_path_element(split.c_str());
						e != 0; e = next_path_element(e))
						path_e.list().push_back(entry(e));

					if (i->pad_file
						|| i->hidden_attribute
						|| i->executable_attribute
						|| i->symlink_attribute)
					{
						std::string& attr = file_e["attr"].string();
						if (i->pad_file) attr += 'p';
						if (i->hidden_attribute) attr += 'h';
						if (i->executable_attribute) attr += 'x';
						if (m_include_symlinks && i->symlink_attribute) attr += 'l';
					}
					if (m_include_symlinks
						&& i->symlink_attribute
						&& i->symlink_index != -1)
					{
						entry& sympath_e = file_e["symlink path"];

						std::string split = split_path(m_files.symlink(*i));
						for (char const* e = split.c_str(); e != 0; e = next_path_element(e))
							sympath_e.list().push_back(entry(e));
					}
					int file_index = i - m_files.begin();
					if (!m_filehashes.empty() && m_filehashes[file_index] != sha1_hash())
					{
						file_e["sha1"] = m_filehashes[file_index].to_string();
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
					h.update((char const*)&m_merkle_tree[i][0], 20);
					h.update((char const*)&m_merkle_tree[i+1][0], 20);
					m_merkle_tree[parent] = h.final();
				}
				level_start = merkle_get_parent(level_start);
				level_size /= 2;
			}
			TORRENT_ASSERT(level_size == 1);
			std::string& p = info["root hash"].string();
			p.assign((char const*)&m_merkle_tree[0][0], 20);
		}
		else
		{
			std::string& p = info["pieces"].string();

			for (std::vector<sha1_hash>::const_iterator i = m_piece_hash.begin();
				i != m_piece_hash.end(); ++i)
			{
				p.append((char*)i->begin(), sha1_hash::size);
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

	void create_torrent::set_hash(int index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_hash.size());
		m_piece_hash[index] = h;
	}

	void create_torrent::set_file_hash(int index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_files.num_files());
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

