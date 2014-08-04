/*

Copyright (c) 2010-2014, Arvid Norberg
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

#include "libtorrent/settings.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/assert.hpp"

#include <string>

namespace libtorrent
{

	// TODO: 4 eliminate all use of this mechanism
	void load_struct(lazy_entry const& e, void* s, bencode_map_entry const* m, int num)
	{
		for (int i = 0; i < num; ++i)
		{
			lazy_entry const* key = e.dict_find(m[i].name);
			if (key == 0) continue;
			void* dest = ((char*)s) + m[i].offset;
			switch (m[i].type)
			{
				case std_string:
				{
					if (key->type() != lazy_entry::string_t) continue;
					*((std::string*)dest) = key->string_value();
					break;
				}
				case character:
				case integer16:
				case boolean:
				case integer:
				case size_integer:
				case time_integer:
				case floating_point:
				{
					if (key->type() != lazy_entry::int_t) continue;
					size_type val = key->int_value();
					switch (m[i].type)
					{
						case character: *((char*)dest) = char(val); break;
						case integer16: *((boost::uint16_t*)dest) = boost::uint16_t(val); break;
						case integer: *((int*)dest) = int(val); break;
						case size_integer: *((size_type*)dest) = size_type(val); break;
						case time_integer: *((time_t*)dest) = time_t(val); break;
						case floating_point: *((float*)dest) = float(val) / 1000.f; break;
						case boolean: *((bool*)dest) = (val != 0); break;
					}
				}
			}
		}
	}

	void save_struct(entry& e, void const* s, bencode_map_entry const* m, int num, void const* def)
	{
		if (e.type() != entry::dictionary_t) e = entry(entry::dictionary_t);
		for (int i = 0; i < num; ++i)
		{
			char const* key = m[i].name;
			void const* src = ((char*)s) + m[i].offset;
			if (def)
			{
				// if we have a default value for this field
				// and it is the default, don't save it
				void const* default_value = ((char*)def) + m[i].offset;
				switch (m[i].type)
				{
					case std_string:
						if (*((std::string*)src) == *((std::string*)default_value)) continue;
						break;
					case character:
						if (*((char*)src) == *((char*)default_value)) continue;
						break;
					case integer:
						if (*((int*)src) == *((int*)default_value)) continue;
						break;
					case integer16:
						if (*((boost::uint16_t*)src) == *((boost::uint16_t*)default_value)) continue;
						break;
					case size_integer:
						if (*((size_type*)src) == *((size_type*)default_value)) continue;
						break;
					case time_integer:
						if (*((time_t*)src) == *((time_t*)default_value)) continue;
						break;
					case floating_point:
						if (*((float*)src) == *((float*)default_value)) continue;
						break;
					case boolean:
						if (*((bool*)src) == *((bool*)default_value)) continue;
						break;
					default: TORRENT_ASSERT(false);
				}
			}
			entry& val = e[key];
			TORRENT_ASSERT_VAL(val.type() == entry::undefined_t, val.type());
			switch (m[i].type)
			{
				case std_string: val = *((std::string*)src); break;
				case character: val = *((char*)src); break;
				case integer: val = *((int*)src); break;
				case integer16: val = *((boost::uint16_t*)src); break;
				case size_integer: val = *((size_type*)src); break;
				case time_integer: val = *((time_t*)src); break;
				case floating_point: val = size_type(*((float*)src) * 1000.f); break;
				case boolean: val = *((bool*)src); break;
				default: TORRENT_ASSERT(false);
			}
		}
	}

}

