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

#include <functional>
#include <boost/tuple/tuple.hpp> // for boost::tie

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/aux_/path.hpp"

namespace libtorrent
{

// TODO: get rid of these dependencies
using lt::exists;
using lt::remove;

std::vector<char> load_file(std::string const& filename, error_code& ec
	, int limit)
{
	ec.clear();
	std::vector<char> ret;
	file f;
	f.open(filename, open_mode::read_only, ec);
	if (ec) return ret;
	std::int64_t s = f.get_size(ec);
	if (ec) return ret;
	if (s > limit)
	{
		ec = error_code(errors::metadata_too_large, libtorrent_category());
		return ret;
	}
	ret.resize(s);
	if (s == 0) return ret;
	iovec_t b = {ret.data(), size_t(s) };
	std::int64_t const read = f.readv(0, b, ec);
	if (read != s) return ret;
	return ret;
}

int save_file(std::string const& filename, std::vector<char>& v, error_code& ec)
{
	file f;
	ec.clear();
	if (!f.open(filename, open_mode::write_only, ec)) return -1;
	if (ec) return -1;
	iovec_t b = {&v[0], v.size()};
	std::int64_t written = f.writev(0, b, ec);
	if (written != int(v.size())) return -3;
	if (ec) return -3;
	return 0;
}

save_settings::save_settings(session& s, settings_pack const& sett
	, std::string const& settings_file)
	: m_ses(s)
	, m_settings_file(settings_file)
{
	for (int i = settings_pack::string_type_base;
		i < settings_pack::string_type_base
		+ settings_pack::num_string_settings; ++i)
	{
		if (!sett.has_val(i)) continue;
		m_strings[name_for_setting(i)] = sett.get_str(i);
	}
	for (int i = settings_pack::bool_type_base;
		i < settings_pack::bool_type_base
		+ settings_pack::num_bool_settings; ++i)
	{
		if (!sett.has_val(i)) continue;
		m_ints[name_for_setting(i)] = sett.get_bool(i);
	}
	for (int i = settings_pack::int_type_base;
		i < settings_pack::int_type_base
		+ settings_pack::num_int_settings; ++i)
	{
		if (!sett.has_val(i)) continue;
		m_ints[name_for_setting(i)] = sett.get_int(i);
	}
}

save_settings::~save_settings() {}

void save_settings::save(error_code& ec) const
{
	// back-up current settings file as .bak before saving the new one
	std::string backup = m_settings_file + ".bak";
	bool const has_settings = exists(m_settings_file);
	bool const has_backup = exists(backup);

	if (has_settings && has_backup)
		remove(backup, ec);

	if (has_settings)
		rename(m_settings_file, backup, ec);

	ec.clear();

	entry sett;
	m_ses.save_state(sett);

	for (auto const& i : m_ints)
	{
		sett[i.first] = i.second;
	}

	for (auto const& i : m_strings)
	{
		sett[i.first] = i.second;
	}
	std::vector<char> buf;
	bencode(std::back_inserter(buf), sett);
	save_file(m_settings_file, buf, ec);
}

namespace {
void load_settings_impl(session_params& params, std::string const& filename
	, error_code& ec)
{
	ec.clear();
	std::vector<char> buf = load_file(filename, ec);
	if (ec) return;

	bdecode_node sett = bdecode(buf, ec);
	if (ec) return;

	// load the custom int and string keys
	if (sett.type() != bdecode_node::dict_t) return;

	bdecode_node dht = sett.dict_find_dict("dht");
	if (dht)
	{
		params.dht_settings = dht::read_dht_settings(dht);
	}

	bdecode_node dht_state = sett.dict_find_dict("dht state");
	if (dht_state)
	{
		params.dht_state = dht::read_dht_state(dht_state);
	}

	int num_items = sett.dict_size();
	for (int i = 0; i < num_items; ++i)
	{
		bdecode_node item;
		string_view key;
		boost::tie(key, item) = sett.dict_at(i);

		int const n = setting_by_name(std::string(key));
		if (n < 0) continue;
		if ((n & settings_pack::type_mask) == settings_pack::int_type_base)
		{
			if (item.type() != bdecode_node::int_t) continue;
			params.settings.set_int(n, item.int_value());
		}
		else if ((n & settings_pack::type_mask) == settings_pack::bool_type_base)
		{
			if (item.type() != bdecode_node::int_t) continue;
			params.settings.set_bool(n, item.int_value() != 0);
		}
		else if ((n & settings_pack::type_mask) == settings_pack::string_type_base)
		{
			if (item.type() != bdecode_node::string_t) continue;
			params.settings.set_str(n, std::string(item.string_value()));
		}
	}
}
}

void load_settings(session_params& params
	, std::string const& filename
	, error_code& ec)
{
	ec.clear();
	load_settings_impl(params, filename, ec);
	if (!ec) return;
	std::string const backup = filename + ".bak";
	load_settings_impl(params, backup, ec);
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
	auto const i = m_strings.find(key);
	if (i == m_strings.end()) return def;
	return i->second;
}

}

