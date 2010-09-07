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


#include <iosfwd>
#include <map>
#include <list>
#include <string>
#include <stdexcept>

#include "libtorrent/size_type.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	struct TORRENT_EXPORT type_error: std::runtime_error
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

	class entry;

	class TORRENT_EXPORT entry
	{
	public:

		// the key is always a string. If a generic entry would be allowed
		// as a key, sorting would become a problem (e.g. to compare a string
		// to a list). The definition doesn't mention such a limit though.
		typedef std::map<std::string, entry> dictionary_type;
		typedef std::string string_type;
		typedef std::list<entry> list_type;
		typedef size_type integer_type;

		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			undefined_t
		};

		data_type type() const;

		entry(dictionary_type const&);
		entry(string_type const&);
		entry(list_type const&);
		entry(integer_type const&);

		entry();
		entry(data_type t);
		entry(entry const& e);
		~entry();

		bool operator==(entry const& e) const;
		
		void operator=(entry const&);
		void operator=(dictionary_type const&);
		void operator=(string_type const&);
		void operator=(list_type const&);
		void operator=(integer_type const&);

		integer_type& integer();
		const integer_type& integer() const;
		string_type& string();
		const string_type& string() const;
		list_type& list();
		const list_type& list() const;
		dictionary_type& dict();
		const dictionary_type& dict() const;

		void swap(entry& e);

		// these functions requires that the entry
		// is a dictionary, otherwise they will throw	
		entry& operator[](char const* key);
		entry& operator[](std::string const& key);
#ifndef BOOST_NO_EXCEPTIONS
		const entry& operator[](char const* key) const;
		const entry& operator[](std::string const& key) const;
#endif
		entry* find_key(char const* key);
		entry const* find_key(char const* key) const;
		entry* find_key(std::string const& key);
		entry const* find_key(std::string const& key) const;
		
		void print(std::ostream& os, int indent = 0) const;

	protected:

		void construct(data_type t);
		void copy(const entry& e);
		void destruct();

	private:

		data_type m_type;

#if (defined(_MSC_VER) && _MSC_VER < 1310) || TORRENT_COMPLETE_TYPES_REQUIRED
		// workaround for msvc-bug.
		// assumes sizeof(map<string, char>) == sizeof(map<string, entry>)
		// and sizeof(list<char>) == sizeof(list<entry>)
		union
		{
			char data[
				detail::max4<sizeof(std::list<char>)
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

#ifdef TORRENT_DEBUG
	public:
		// in debug mode this is set to false by bdecode
		// to indicate that the program has not yet queried
		// the type of this entry, and sould not assume
		// that it has a certain type. This is asserted in
		// the accessor functions. This does not apply if
		// exceptions are used.
		mutable bool m_type_queried;
#endif
	};

	inline std::ostream& operator<<(std::ostream& os, const entry& e)
	{
		e.print(os, 0);
		return os;
	}

	inline entry::data_type entry::type() const
	{
#ifdef TORRENT_DEBUG
		m_type_queried = true;
#endif
		return m_type;
	}

	inline entry::~entry() { destruct(); }

	inline void entry::operator=(const entry& e)
	{
		destruct();
		copy(e);
	}

	inline entry::integer_type& entry::integer()
	{
		if (m_type == undefined_t) construct(int_t);
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != int_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == int_t);
		return *reinterpret_cast<integer_type*>(data);
	}

	inline entry::integer_type const& entry::integer() const
	{
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != int_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == int_t);
		return *reinterpret_cast<const integer_type*>(data);
	}

	inline entry::string_type& entry::string()
	{
		if (m_type == undefined_t) construct(string_t);
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != string_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == string_t);
		return *reinterpret_cast<string_type*>(data);
	}

	inline entry::string_type const& entry::string() const
	{
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != string_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == string_t);
		return *reinterpret_cast<const string_type*>(data);
	}

	inline entry::list_type& entry::list()
	{
		if (m_type == undefined_t) construct(list_t);
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != list_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == list_t);
		return *reinterpret_cast<list_type*>(data);
	}

	inline entry::list_type const& entry::list() const
	{
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != list_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == list_t);
		return *reinterpret_cast<const list_type*>(data);
	}

	inline entry::dictionary_type& entry::dict()
	{
		if (m_type == undefined_t) construct(dictionary_t);
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != dictionary_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == dictionary_t);
		return *reinterpret_cast<dictionary_type*>(data);
	}

	inline entry::dictionary_type const& entry::dict() const
	{
#ifndef BOOST_NO_EXCEPTIONS
		if (m_type != dictionary_t) throw type_error("invalid type requested from entry");
#elif defined TORRENT_DEBUG
		TORRENT_ASSERT(m_type_queried);
#endif
		TORRENT_ASSERT(m_type == dictionary_t);
		return *reinterpret_cast<const dictionary_type*>(data);
	}

}

#endif // TORRENT_ENTRY_HPP_INCLUDED

