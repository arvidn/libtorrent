/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include <map>
#include <list>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#if TORRENT_USE_IOSTREAM
#include <iosfwd>
#endif

#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/aligned_union.hpp"

namespace libtorrent {

#if TORRENT_ABI_VERSION == 1
	struct lazy_entry;
	// backwards compatibility
	using type_error = system_error;
#endif
	struct bdecode_node;

namespace aux {

#if (__cplusplus > 201103) || (defined _MSC_VER && _MSC_VER >= 1900)
		// this enables us to compare a string_view against the std::string that's
		// held by the std::map
		// is_transparent was introduced in C++14
		struct strview_less
		{
			using is_transparent = std::true_type;
			template <typename T1, typename T2>
			bool operator()(T1 const& rhs, T2 const& lhs) const
			{ return rhs < lhs; }
		};

		template<class T> using map_string = std::map<std::string, T, aux::strview_less>;
#else
		template<class T>
		struct map_string : std::map<std::string, T>
		{
			using base = std::map<std::string, T>;

			typename base::iterator find(const string_view& key)
			{
				return this->base::find(key.to_string());
			}

			typename base::const_iterator find(const string_view& key) const
			{
				return this->base::find(key.to_string());
			}
		};
#endif
	}

	// The ``entry`` class represents one node in a bencoded hierarchy. It works as a
	// variant type, it can be either a list, a dictionary (``std::map``), an integer
	// or a string.
	class TORRENT_EXPORT entry
	{
	public:

		// the key is always a string. If a generic entry would be allowed
		// as a key, sorting would become a problem (e.g. to compare a string
		// to a list). The definition doesn't mention such a limit though.
		using dictionary_type = aux::map_string<entry>;
		using string_type = std::string;
		using list_type = std::vector<entry>;
		using integer_type = std::int64_t;
		using preformatted_type = std::vector<char>;

		// the types an entry can have
		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			undefined_t,
			preformatted_t
		};

		// returns the concrete type of the entry
		data_type type() const;

		// constructors directly from a specific type.
		// The content of the argument is copied into the
		// newly constructed entry
		entry(dictionary_type); // NOLINT
		entry(span<char const>); // NOLINT
		template <typename U, typename Cond = typename std::enable_if<
			std::is_same<U, entry::string_type>::value
			|| std::is_same<U, string_view>::value
			|| std::is_same<U, char const*>::value>::type>
		entry(U v) // NOLINT
			: m_type(undefined_t)
		{
#if TORRENT_USE_ASSERTS
			m_type_queried = true;
#endif
			new(&data) string_type(std::move(v));
			m_type = string_t;
		}
		entry(list_type); // NOLINT
		entry(integer_type); // NOLINT
		entry(preformatted_type); // NOLINT

		// construct an empty entry of the specified type.
		// see data_type enum.
		entry(data_type t); // NOLINT

		// hidden
		entry(entry const& e);
		entry(entry&& e) noexcept;

		// construct from bdecode_node parsed form (see bdecode())
		entry(bdecode_node const& n); // NOLINT

		// hidden
		entry();

		// hidden
		~entry();

		// copies the structure of the right hand side into this
		// entry.
#if TORRENT_ABI_VERSION == 1
		entry& operator=(lazy_entry const&) &;
#endif
		entry& operator=(bdecode_node const&) &;
		entry& operator=(entry const&) &;
		entry& operator=(entry&&) & noexcept;
		entry& operator=(dictionary_type) &;
		entry& operator=(span<char const>) &;
		template <typename U, typename Cond = typename std::enable_if<
			std::is_same<U, entry::string_type>::value
			|| std::is_same<U, char const*>::value>::type>
		entry& operator=(U v) &
		{
			destruct();
			new(&data) string_type(std::move(v));
			m_type = string_t;
#if TORRENT_USE_ASSERTS
			m_type_queried = true;
#endif
			return *this;
		}
		entry& operator=(list_type) &;
		entry& operator=(integer_type) &;
		entry& operator=(preformatted_type) &;

		// The ``integer()``, ``string()``, ``list()`` and ``dict()`` functions
		// are accessors that return the respective type. If the ``entry`` object
		// isn't of the type you request, the accessor will throw
		// system_error. You can ask an ``entry`` for its type through the
		// ``type()`` function.
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
		preformatted_type& preformatted();
		const preformatted_type& preformatted() const;

		// swaps the content of *this* with ``e``.
		void swap(entry& e);

		// All of these functions requires the entry to be a dictionary, if it
		// isn't they will throw ``system_error``.
		//
		// The non-const versions of the ``operator[]`` will return a reference
		// to either the existing element at the given key or, if there is no
		// element with the given key, a reference to a newly inserted element at
		// that key.
		//
		// The const version of ``operator[]`` will only return a reference to an
		// existing element at the given key. If the key is not found, it will
		// throw ``system_error``.
		entry& operator[](string_view key);
		const entry& operator[](string_view key) const;

		// These functions requires the entry to be a dictionary, if it isn't
		// they will throw ``system_error``.
		//
		// They will look for an element at the given key in the dictionary, if
		// the element cannot be found, they will return nullptr. If an element
		// with the given key is found, the return a pointer to it.
		entry* find_key(string_view key);
		entry const* find_key(string_view key) const;

		// returns a pretty-printed string representation
		// of the bencoded structure, with JSON-style syntax
		std::string to_string(bool single_line = false) const;

	protected:

		void construct(data_type t);
		void copy(const entry& e);
		void destruct();

	private:

		aux::aligned_union<1
#if TORRENT_COMPLETE_TYPES_REQUIRED
			// for implementations that require complete types, use char and hope
			// for the best
			, std::list<char>
			, std::map<std::string, char>
#else
			, list_type
			, dictionary_type
#endif
			, preformatted_type
			, string_type
			, integer_type
		>::type data;

		// the bitfield is used so that the m_type_queried field still fits, so
		// that the ABI is the same for debug builds and release builds. It
		// appears to be very hard to match debug builds with debug versions of
		// libtorrent
		std::uint8_t m_type:7;

	public:
		// in debug mode this is set to false by bdecode to indicate that the
		// program has not yet queried the type of this entry, and should not
		// assume that it has a certain type. This is asserted in the accessor
		// functions. This does not apply if exceptions are used.
		mutable std::uint8_t m_type_queried:1;
	};

	// hidden
	TORRENT_EXPORT bool operator==(entry const& lhs, entry const& rhs);
	inline bool operator!=(entry const& lhs, entry const& rhs) { return !(lhs == rhs); }

namespace detail {

	// internal
	TORRENT_EXPORT string_view integer_to_str(span<char> buf
		, entry::integer_type val);
}

#if TORRENT_USE_IOSTREAM
	// prints the bencoded structure to the ostream as a JSON-style structure.
	inline std::ostream& operator<<(std::ostream& os, const entry& e)
	{
		os << e.to_string();
		return os;
	}
#endif

}

#endif // TORRENT_ENTRY_HPP_INCLUDED
