/*

Copyright (c) 2003-2016, Arvid Norberg
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


#include <map>
#include <list>
#include <string>
#include <stdexcept>

#include "libtorrent/size_type.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/max.hpp"

#if TORRENT_USE_IOSTREAM
#include <iosfwd>
#endif

namespace libtorrent
{
	struct lazy_entry;

	// thrown by any accessor function of entry if the accessor
	// function requires a type different than the actual type
	// of the entry object.
	struct TORRENT_EXPORT type_error: std::runtime_error
	{
		// internal
		type_error(const char* error): std::runtime_error(error) {}
	};

	// The ``entry`` class represents one node in a bencoded hierarchy. It works as a
	// variant type, it can be either a list, a dictionary (``std::map``), an integer
	// or a string.
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

		// the types an entry can have
		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			undefined_t
		};

		// returns the concrete type of the entry
		data_type type() const;

		// constructors directly from a specific type.
		// The content of the argument is copied into the
		// newly constructed entry
		entry(dictionary_type const&);
		entry(string_type const&);
		entry(list_type const&);
		entry(integer_type const&);

		// construct an empty entry of the specified type.
		// see data_type enum.
		entry(data_type t);

		// hidden
		entry(entry const& e);

		// hidden
		entry();

		// hidden
		~entry();

		// hidden
		bool operator==(entry const& e) const;
		bool operator!=(entry const& e) const { return !(*this == e); }
		
		// copies the structure of the right hand side into this
		// entry.
		void operator=(lazy_entry const&);
		void operator=(entry const&);
		void operator=(dictionary_type const&);
		void operator=(string_type const&);
		void operator=(list_type const&);
		void operator=(integer_type const&);

		// The ``integer()``, ``string()``, ``list()`` and ``dict()`` functions
		// are accessors that return the respective type. If the ``entry`` object
		// isn't of the type you request, the accessor will throw
		// libtorrent_exception (which derives from ``std::runtime_error``). You
		// can ask an ``entry`` for its type through the ``type()`` function.
		// 
		// If you want to create an ``entry`` you give it the type you want it to
		// have in its constructor, and then use one of the non-const accessors
		// to get a reference which you then can assign the value you want it to
		// have.
		// 
		// The typical code to get info from a torrent file will then look like
		// this:
		//
		// .. code:: c++
		// 
		// 	entry torrent_file;
		// 	// ...
		// 
		// 	// throws if this is not a dictionary
		// 	entry::dictionary_type const& dict = torrent_file.dict();
		// 	entry::dictionary_type::const_iterator i;
		// 	i = dict.find("announce");
		// 	if (i != dict.end())
		// 	{
		// 		std::string tracker_url = i->second.string();
		// 		std::cout << tracker_url << "\n";
		// 	}
		// 
		// 
		// The following code is equivalent, but a little bit shorter:
		//
		// .. code:: c++
		// 
		// 	entry torrent_file;
		// 	// ...
		// 
		// 	// throws if this is not a dictionary
		// 	if (entry* i = torrent_file.find_key("announce"))
		// 	{
		// 		std::string tracker_url = i->string();
		// 		std::cout << tracker_url << "\n";
		// 	}
		// 
		// 
		// To make it easier to extract information from a torrent file, the
		// class torrent_info exists.
		integer_type& integer();
		const integer_type& integer() const;
		string_type& string();
		const string_type& string() const;
		list_type& list();
		const list_type& list() const;
		dictionary_type& dict();
		const dictionary_type& dict() const;

		// swaps the content of *this* with ``e``.
		void swap(entry& e);

		// All of these functions requires the entry to be a dictionary, if it
		// isn't they will throw ``libtorrent::type_error``.
		//
		// The non-const versions of the ``operator[]`` will return a reference
		// to either the existing element at the given key or, if there is no
		// element with the given key, a reference to a newly inserted element at
		// that key.
		//
		// The const version of ``operator[]`` will only return a reference to an
		// existing element at the given key. If the key is not found, it will
		// throw ``libtorrent::type_error``.
 		entry& operator[](char const* key);
		entry& operator[](std::string const& key);
#ifndef BOOST_NO_EXCEPTIONS
		const entry& operator[](char const* key) const;
		const entry& operator[](std::string const& key) const;
#endif

		// These functions requires the entry to be a dictionary, if it isn't
		// they will throw ``libtorrent::type_error``.
		//
		// They will look for an element at the given key in the dictionary, if
		// the element cannot be found, they will return 0. If an element with
		// the given key is found, the return a pointer to it.
		entry* find_key(char const* key);
		entry const* find_key(char const* key) const;
		entry* find_key(std::string const& key);
		entry const* find_key(std::string const& key) const;

		// returns a pretty-printed string representation
		// of the bencoded structure, with JSON-style syntax
		std::string to_string() const;

	protected:

		void construct(data_type t);
		void copy(const entry& e);
		void destruct();

	private:

		void to_string_impl(std::string& out, int indent) const;

#if (defined(_MSC_VER) && _MSC_VER < 1310) || TORRENT_COMPLETE_TYPES_REQUIRED
		// workaround for msvc-bug.
		// assumes sizeof(map<string, char>) == sizeof(map<string, entry>)
		// and sizeof(list<char>) == sizeof(list<entry>)
		enum { union_size
			= max4<sizeof(std::list<char>)
			, sizeof(std::map<std::string, char>)
			, sizeof(string_type)
			, sizeof(integer_type)>::value
		};
#else
		enum { union_size
			= max4<sizeof(list_type)
			, sizeof(dictionary_type)
			, sizeof(string_type)
			, sizeof(integer_type)>::value
		};
#endif
		integer_type data[(union_size + sizeof(integer_type) - 1)
			/ sizeof(integer_type)];

		// the bitfield is used so that the m_type_queried field still fits, so
		// that the ABI is the same for debug builds and release builds. It
		// appears to be very hard to match debug builds with debug versions of
		// libtorrent
		boost::uint8_t m_type:7;

	public:
		// in debug mode this is set to false by bdecode to indicate that the
		// program has not yet queried the type of this entry, and sould not
		// assume that it has a certain type. This is asserted in the accessor
		// functions. This does not apply if exceptions are used.
		mutable boost::uint8_t m_type_queried:1;
	};

#if TORRENT_USE_IOSTREAM
	// prints the bencoded structure to the ostream as a JSON-style structure.
	inline std::ostream& operator<<(std::ostream& os, const entry& e)
	{
		os << e.to_string();
		return os;
	}
#endif

#ifndef BOOST_NO_EXCEPTIONS
	// internal
	inline void throw_type_error()
	{
		throw libtorrent_exception(error_code(errors::invalid_entry_type
			, get_libtorrent_category()));
	}
#endif

}

#endif // TORRENT_ENTRY_HPP_INCLUDED

