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

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/gregorian/greg_date.hpp>
#include <boost/bind.hpp>
#include <boost/next_prior.hpp>

#include <sys/types.h>
#include <sys/stat.h>

#define MAX_SYMLINK_PATH 200

namespace gr = boost::gregorian;

namespace libtorrent
{
	// defined in torrent_info.cpp
	int merkle_num_leafs(int);
	int merkle_num_nodes(int);
	int merkle_get_parent(int);
	int merkle_get_sibling(int);

	namespace detail
	{
		int TORRENT_EXPORT get_file_attributes(boost::filesystem::path const& p)
		{
#ifdef TORRENT_WINDOWS

#if TORRENT_USE_WPATH
			std::wstring path = convert_to_wstring(p.external_file_string());
			DWORD attr = GetFileAttributesW(path.c_str());
#else
			std::string path = convert_to_native(p.external_file_string());
			DWORD attr = GetFileAttributesA(path.c_str());
#endif
			if (attr == INVALID_FILE_ATTRIBUTES) return 0;
			if (attr & FILE_ATTRIBUTE_HIDDEN) return file_storage::attribute_hidden;
			return 0;
#else
			struct stat s;
			if (lstat(convert_to_native(p.external_file_string()).c_str(), &s) < 0) return 0;
			int file_attr = 0;
			if (s.st_mode & S_IXUSR) 
				file_attr += file_storage::attribute_executable;
			if(S_ISLNK(s.st_mode))
				file_attr += file_storage::attribute_symlink;
			return file_attr;
#endif
		}
	
#if TORRENT_USE_WPATH
		int TORRENT_EXPORT get_file_attributes(boost::filesystem::wpath const& p)
		{
#ifdef TORRENT_WINDOWS
			std::wstring const& path = p.external_file_string();
			DWORD attr = GetFileAttributesW(path.c_str());
			if (attr == INVALID_FILE_ATTRIBUTES) return 0;
			if (attr & FILE_ATTRIBUTE_HIDDEN) return file_storage::attribute_hidden;
			return 0;
#else
			std::string native;
			wchar_utf8(p.string(), native);
			native = convert_to_native(native);

			struct stat s;
			if (lstat(native.c_str(), &s) < 0) return 0;
			int file_attr = 0;
			if (s.st_mode & S_IXUSR) 
				file_attr += file_storage::attribute_executable;
			if (S_ISLNK(s.st_mode))
				file_attr += file_storage::attribute_symlink;
			return file_attr;
#endif
		}
#endif // TORRENT_USE_WPATH

		std::time_t get_file_mtime(char const* path)
		{
#ifdef TORRENT_WINDOWS
			struct _stat s;
			if (::_stat(path, &s) < 0) return 0;
#else
			struct stat s;
			if (lstat(path, &s) < 0) return 0;
#endif
			return s.st_mtime;
		}

		std::time_t TORRENT_EXPORT get_file_mtime(boost::filesystem::path const& p)
		{
#if defined TORRENT_WINDOWS && TORRENT_USE_WPATH
			std::wstring path = convert_to_wstring(p.external_file_string());
			struct _stat s;
			if (::_wstat(path.c_str(), &s) < 0) return 0;
			return s.st_mtime;
#else
			std::string path = convert_to_native(p.external_file_string());
			return get_file_mtime(p.string().c_str());
#endif
		}

#if TORRENT_USE_WPATH
		std::time_t TORRENT_EXPORT get_file_mtime(boost::filesystem::wpath const& p)
		{
#ifdef TORRENT_WINDOWS
			struct _stat s;
			if (::_wstat(p.string().c_str(), &s) < 0) return 0;
			return s.st_mtime;
#else
			std::string utf8;
			wchar_utf8(p.string(), utf8);
			utf8 = convert_to_native(utf8);
			return get_file_mtime(utf8.c_str());
#endif
		}
#endif // TORRENT_USE_WPATH

#ifndef TORRENT_WINDOWS
		boost::filesystem::path get_symlink_path_impl(char const* path)
		{
			char buf[MAX_SYMLINK_PATH];
			int char_read = readlink(path,buf,MAX_SYMLINK_PATH);
			if (char_read < 0) return "";
			if (char_read < MAX_SYMLINK_PATH) buf[char_read] = 0;
			else buf[0] = 0;
			return buf;
		}
#endif

		boost::filesystem::path TORRENT_EXPORT get_symlink_path(boost::filesystem::path const& p)
		{
#if defined TORRENT_WINDOWS
			return "";
#else
			std::string path = convert_to_native(p.external_file_string());
			return get_symlink_path_impl(p.string().c_str());
#endif
		}

#if TORRENT_USE_WPATH
		boost::filesystem::path TORRENT_EXPORT get_symlink_path(boost::filesystem::wpath const& p)
		{
#ifdef TORRENT_WINDOWS
			return "";
#else
			std::string utf8;
			wchar_utf8(p.string(), utf8);
			utf8 = convert_to_native(utf8);
			return get_symlink_path_impl(utf8.c_str());
#endif
		}
#endif // TORRENT_USE_WPATH

	}

	create_torrent::create_torrent(file_storage& fs, int piece_size, int pad_file_limit, int flags)
		: m_files(fs)
		, m_creation_date(pt::second_clock::universal_time())
		, m_multifile(fs.num_files() > 1)
		, m_private(false)
		, m_merkle_torrent(flags & merkle)
		, m_include_mtime(flags & modification_time)
		, m_include_symlinks(flags & symlinks)
	{
		TORRENT_ASSERT(fs.num_files() > 0);

		// return instead of crash in release mode
		if (fs.num_files() == 0) return;

#if BOOST_VERSION < 103600
		if (!m_multifile && m_files.at(0).path.has_branch_path()) m_multifile = true;
#else
		if (!m_multifile && m_files.at(0).path.has_parent_path()) m_multifile = true;
#endif

		// a piece_size of 0 means automatic
		if (piece_size == 0 && !m_merkle_torrent)
		{
			const int target_size = 40 * 1024;
			piece_size = fs.total_size() / (target_size / 20);
	
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
		, m_creation_date(pt::second_clock::universal_time())
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

		std::vector<std::string> const& web_seeds = ti.url_seeds();
		for (std::vector<std::string>::const_iterator i = web_seeds.begin()
			, end(web_seeds.end()); i != end; ++i)
			add_url_seed(*i);

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

		dict["creation date"] =
			(m_creation_date - pt::ptime(gr::date(1970, gr::Jan, 1))).total_seconds();

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

		// if this function is used with an older version of the library
		// this member won't exist
		// TODO: for distributions that needs to be binary compatible with
		// previos 0.15.x releases, this if-block should be removed
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

		if (m_private) info["private"] = 1;

		if (!m_multifile)
		{
			if (m_include_mtime) info["mtime"] = m_files.at(0).mtime;
			info["length"] = m_files.at(0).size;
			if (m_files.at(0).pad_file || m_files.at(0).hidden_attribute || m_files.at(0).executable_attribute || m_files.at(0).symlink_attribute)
			{
				std::string& attr = info["attr"].string();
				if (m_files.at(0).pad_file) attr += 'p';
				if (m_files.at(0).hidden_attribute) attr += 'h';
				if (m_files.at(0).executable_attribute) attr += 'x';
				if (m_include_symlinks && m_files.at(0).symlink_attribute) attr += 'l';
			}
			if (m_include_symlinks && m_files.at(0).symlink_attribute)
			{
				entry& sympath_e = info["symlink path"];
				
				for (fs::path::iterator j = (m_files.at(0).symlink_path.begin());
					j != m_files.at(0).symlink_path.end(); ++j)
				{
					sympath_e.list().push_back(entry(*j));
				}
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
					if (m_include_mtime) file_e["mtime"] = i->mtime; 
					file_e["length"] = i->size;
					entry& path_e = file_e["path"];

#if BOOST_VERSION < 103600
					TORRENT_ASSERT(i->path.has_branch_path());
#else
					TORRENT_ASSERT(i->path.has_parent_path());
#endif
					TORRENT_ASSERT(*i->path.begin() == m_files.name());

					for (fs::path::iterator j = boost::next(i->path.begin());
						j != i->path.end(); ++j)
					{
						path_e.list().push_back(entry(*j));
					}
					if (i->pad_file || i->hidden_attribute || i->executable_attribute || i->symlink_attribute)
					{
						std::string& attr = file_e["attr"].string();
						if (i->pad_file) attr += 'p';
						if (i->hidden_attribute) attr += 'h';
						if (i->executable_attribute) attr += 'x';
						if (m_include_symlinks && i->symlink_attribute) attr += 'l';
					}
					if (m_include_symlinks && i->symlink_attribute)
					{
						entry& sympath_e = file_e["symlink path"];

						for (fs::path::iterator j = (i->symlink_path.begin());
							j != i->symlink_path.end(); ++j)
						{
							sympath_e.list().push_back(entry(*j));
						}
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

	void create_torrent::set_hash(int index, sha1_hash const& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_hash.size());
		m_piece_hash[index] = h;
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
		m_comment = str;
	}

	void create_torrent::set_creator(char const* str)
	{
		m_created_by = str;
	}

}

