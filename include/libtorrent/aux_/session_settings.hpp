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

#ifndef TORRENT_AUX_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_AUX_SESSION_SETTINGS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/assert.hpp"

#include <string>
#include <array>
#include <bitset>

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {

TORRENT_EXTRA_EXPORT void initialize_default_settings(aux::session_settings& s);

namespace aux {

	struct TORRENT_EXTRA_EXPORT session_settings
	{
		friend TORRENT_EXTRA_EXPORT void lt::LIBTORRENT_VERSION_NAMESPACE_NAME::save_settings_to_dict(
			aux::session_settings const& s, entry::dictionary_type& sett);

		void set_str(int name, std::string value)
		{ set<std::string>(m_strings, name, std::move(value), settings_pack::string_type_base); }
		void set_int(int name, int value)
		{ set<int>(m_ints, name, value, settings_pack::int_type_base); }
		void set_bool(int name, bool value)
		{ set<bool>(m_bools, name, value, settings_pack::bool_type_base); }

		std::string const& get_str(int name) const
		{ return get<std::string const&>(m_strings, name, settings_pack::string_type_base); }
		int get_int(int name) const
		{ return get<int>(m_ints, name, settings_pack::int_type_base); }
		bool get_bool(int name) const
		{ return get<bool>(m_bools, name, settings_pack::bool_type_base); }

		session_settings();

	private:

		template <typename T, typename Container>
		void set(Container& c, int const name, T val
			, int const type) const
		{
			TORRENT_ASSERT((name & settings_pack::type_mask) == type);
			if ((name & settings_pack::type_mask) != type) return;
			size_t const index = name & settings_pack::index_mask;
			TORRENT_ASSERT(index < c.size());
			c[index] = std::move(val);
		}

		template <typename T, typename Container>
		T get(Container const& c, int const name, int const type) const
		{
			static typename std::remove_reference<T>::type empty;
			TORRENT_ASSERT((name & settings_pack::type_mask) == type);
			if ((name & settings_pack::type_mask) != type) return empty;
			size_t const index = name & settings_pack::index_mask;
			TORRENT_ASSERT(index < c.size());
			return c[index];
		}

		std::array<std::string, settings_pack::num_string_settings> m_strings;
		std::array<int, settings_pack::num_int_settings> m_ints;
		std::bitset<settings_pack::num_bool_settings> m_bools;
	};

}}}

#endif
