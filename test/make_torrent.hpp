/*

Copyright (c) 2016-2017, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef MAKE_TORRENT_HPP
#define MAKE_TORRENT_HPP

#include "libtorrent/torrent_info.hpp"
#include <vector>
#include <string>
#include "test.hpp"

enum flags_t
{
	private_torrent = 1
};

struct torrent_args
{
	torrent_args() : m_priv(false) {}
	torrent_args& name(char const* n) { m_name = n; return *this; }
	torrent_args& file(char const* f) { m_files.push_back(f); return *this; }
	torrent_args& url_seed(char const* u) { m_url_seed = u; return *this; }
	torrent_args& http_seed(char const* u) { m_http_seed = u; return *this; }
	torrent_args& priv() { m_priv = true; return *this; }
	torrent_args& collection(std::string c) { m_collection = c; return *this; }

	bool m_priv;
	std::string m_name;
	std::vector<std::string> m_files;
	std::string m_url_seed;
	std::string m_http_seed;
	std::string m_collection;
};

EXPORT std::shared_ptr<lt::torrent_info>
	make_test_torrent(torrent_args const& args);

EXPORT void generate_files(lt::torrent_info const& ti, std::string const& path, bool random = false);

#endif
