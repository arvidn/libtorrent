/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_ENTRY_HPP_INCLUDED
#define TORRENT_ENTRY_HPP_INCLUDED

/*
 *
 * This file declares the entry class. It is a
 * variant-type that can be an integer, list,
 * dictionary (map) or a string. This type is
 * used to hold bdecoded data (which is the
 * encoding BitTorrent messages uses).
 *
 * it has 4 accessors to access the actual
 * type of the object. They are:
 * integer()
 * string()
 * list()
 * dict()
 * The actual type has to match the type you
 * are asking for, otherwise you will get an
 * assertion failure.
 * When you default construct an entry, it is
 * uninitialized. You can initialize it through the
 * assignment operator, copy-constructor or
 * the constructor that takes a data_type enum.
 *
 *
 */


#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <cassert>
#include <boost/cstdint.hpp>

namespace libtorrent
{

	struct type_error: std::runtime_error
	{
		type_error(const char* error): std::runtime_error(error) {}
	};

	namespace detail
	{
		template<int v1, int v2>
		struct max2 { enum { value = v1>v2?v1:v2 }; };

		template<int v1, int v2, int v3>
		struct max3
		{
			enum
			{
				temp = max2<v1,v2>::value,
				value = temp>v3?temp:v3
			};
		};

		template<int v1, int v2, int v3, int v4>
		struct max4
		{
			enum
			{
				temp = max3<v1,v2, v3>::value,
				value = temp>v4?temp:v4
			};
		};
	}


	class entry
	{
	public:

		typedef std::map<std::string, entry> dictionary_type;
		typedef std::string string_type;
		typedef std::vector<entry> list_type;

		typedef boost::int64_t integer_type;

		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			undefined_t
		};

		data_type type() const { return m_type; }

		entry(const dictionary_type&);
		entry(const string_type&);
		entry(const list_type&);
		entry(const integer_type&);

		entry(): m_type(undefined_t) {}
		entry(data_type t): m_type(t) { construct(t); }
		entry(const entry& e) { copy(e); }
		~entry() { destruct(); }

		void operator=(const entry& e)
		{
			destruct();
			copy(e);
		}

		void operator=(const dictionary_type&);
		void operator=(const string_type&);
		void operator=(const list_type&);
		void operator=(const integer_type&);

		integer_type& integer()
		{
			if (m_type != int_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<integer_type*>(data);
		}

		const integer_type& integer() const
		{
			if (m_type != int_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<const integer_type*>(data);
		}

		string_type& string()
		{
			if (m_type != string_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<string_type*>(data);
		}

		const string_type& string() const
		{
			if (m_type != string_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<const string_type*>(data);
		}

		list_type& list()
		{
			if (m_type != list_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<list_type*>(data);
		}

		const list_type& list() const
		{
			if (m_type != list_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<const list_type*>(data);
		}

		dictionary_type& dict()
		{
			if (m_type != dictionary_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<dictionary_type*>(data);
		}

		const dictionary_type& dict() const
		{
			if (m_type != dictionary_t) throw type_error("invalid type requested from entry");
			return *reinterpret_cast<const dictionary_type*>(data);
		}

		void print(std::ostream& os, int indent = 0) const;

	private:

		void construct(data_type t);
		void copy(const entry& e);
		void destruct();

		data_type m_type;

#if defined(_MSC_VER)

		// workaround for msvc-bug.
		// assumes sizeof(map<string, char>) == sizeof(map<string, entry>)
		union
		{
			char data[detail::max4<sizeof(list_type)
				, sizeof(std::map<std::string, char>)
				, sizeof(string_type)
				, sizeof(integer_type)>::value];
			integer_type dummy_aligner;
		};
#else
		union
		{
			char data[detail::max4<sizeof(list_type)
				, sizeof(dictionary_type)
				, sizeof(string_type)
				, sizeof(integer_type)>::value];
			integer_type dummy_aligner;
		};
#endif

	};

	inline std::ostream& operator<<(std::ostream& os, const entry& e)
	{
		e.print(os, 0);
		return os;
	}

}

#endif // TORRENT_ENTRY_HPP_INCLUDED
