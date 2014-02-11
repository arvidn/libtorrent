/*

Copyright (c) 2012, Arvid Norberg
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

#include "save_settings.hpp"

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp> // for boost::tie

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/file.hpp"

namespace libtorrent
{
/*
int load_file(std::string const& filename, std::vector<char>& v, error_code& ec, int limit)
{
	ec.clear();
	file f;
	if (!f.open(filename, file::read_only, ec)) return -1;
	size_type s = f.get_size(ec);
	if (ec) return -1;
	if (s > limit)
	{
		ec = error_code(errors::metadata_too_large, get_libtorrent_category());
		return -2;
	}
	v.resize(s);
	if (s == 0) return 0;
	file::iovec_t b = {&v[0], size_t(s) };
	size_type read = f.readv(0, &b, 1, ec);
	if (read != s) return -3;
	if (ec) return -3;
	return 0;
}
*/
int save_file(std::string const& filename, std::vector<char>& v, error_code& ec)
{
	file f;
	if (!f.open(filename, file::write_only, ec)) return -1;
	if (ec) return -1;
	file::iovec_t b = {&v[0], v.size()};
	size_type written = f.writev(0, &b, 1, ec);
	if (written != int(v.size())) return -3;
	if (ec) return -3;
	return 0;
}

save_settings::save_settings(session& s, std::string const& settings_file)
	: m_ses(s)
	, m_settings_file(settings_file)
{}

save_settings::~save_settings() {}

void save_settings::save(error_code& ec) const
{
	// back-up current settings file as .bak before saving the new one
	std::string backup = m_settings_file + ".bak";
	bool has_settings = exists(m_settings_file);
	bool has_backup = exists(backup);

	if (has_settings && has_backup)
		remove(backup, ec);

	if (has_settings)
		rename(m_settings_file, backup, ec);

	ec.clear();

	entry sett;
	m_ses.save_state(sett);

	for (std::map<std::string, int>::const_iterator i = m_ints.begin()
		, end(m_ints.end()); i != end; ++i)
	{
		sett[i->first] = i->second;
	}

	for (std::map<std::string, std::string>::const_iterator i = m_strings.begin()
		, end(m_strings.end()); i != end; ++i)
	{
		sett[i->first] = i->second;
	}
	std::vector<char> buf;
	bencode(std::back_inserter(buf), sett);
	save_file(m_settings_file, buf, ec);
}

void save_settings::load(error_code& ec)
{
	load_impl(m_settings_file, ec);
	if (!ec) return;
	ec.clear();
	std::string backup = m_settings_file + ".bak";
	load_impl(backup, ec);
}

void save_settings::load_impl(std::string filename, error_code& ec)
{
	std::vector<char> buf;
	if (load_file(filename, buf, ec) < 0)
		return;

	lazy_entry sett;
	if (lazy_bdecode(&buf[0], &buf[0] + buf.size(), sett, ec) != 0)
		return;

	m_ses.load_state(sett);

	// load the custom int and string keys
	if (sett.type() != lazy_entry::dict_t) return;
	int num_items = sett.dict_size();
	for (int i = 0; i < num_items; ++i)
	{
		lazy_entry const* item;
		std::string key;
		boost::tie(key, item) = sett.dict_at(i);
		if (item->type() == lazy_entry::string_t)
		{
			m_strings[key] = item->string_value();
		}
		else if (item->type() == lazy_entry::int_t)
		{
			m_ints[key] = item->int_value();
		}
	}
}

void save_settings::set_int(char const* key, int val)
{
	m_ints[key] = val;
}

void save_settings::set_str(char const* key, std::string val)
{
	m_strings[key] = val;
}

int save_settings::get_int(char const* key, int def) const
{
	std::map<std::string, int>::const_iterator i = m_ints.find(key);
	if (i == m_ints.end()) return def;
	return i->second;
}

std::string save_settings::get_str(char const* key, char const* def) const
{
	std::map<std::string, std::string>::const_iterator i = m_strings.find(key);
	if (i == m_strings.end()) return def;
	return i->second;
}

}

