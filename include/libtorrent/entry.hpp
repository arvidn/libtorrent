/*

Copyright (c) 2003-2009, 2013-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
#include <variant>
#if TORRENT_USE_IOSTREAM
#include <iosfwd>
#endif

#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/noexcept_movable.hpp"
#include "libtorrent/aux_/strview_less.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/container/map.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt {

#if TORRENT_ABI_VERSION == 1
	// backwards compatibility
	using type_error = system_error;
#endif
	struct bdecode_node;
	struct entry;

	namespace entry_types {

		using dictionary_type = boost::container::map<std::string, entry, aux::strview_less>;
		using string_type = std::string;
		using list_type = std::vector<entry>;
		using integer_type = std::int64_t;
		using preformatted_type = std::vector<char>;
		struct uninitialized_type {
			bool operator==(uninitialized_type const&) const { return true; }
		};

		// internal
		using variant_type = std::variant<integer_type
			, string_type
			, list_type
			, dictionary_type
			, preformatted_type
			, uninitialized_type>;

		static_assert(std::is_nothrow_move_constructible<variant_type>::value
			, "expected variant to be nothrow move constructible");
	}

	// The ``entry`` class represents one node in a bencoded hierarchy. It works as a
	// variant type, it can be either a list, a dictionary (``std::map``), an integer
	// or a string.
	struct TORRENT_EXPORT entry : entry_types::variant_type
	{
		// the key is always a string. If a generic entry would be allowed
		// as a key, sorting would become a problem (e.g. to compare a string
		// to a list). The definition doesn't mention such a limit though.
		using dictionary_type = entry_types::dictionary_type;
		using string_type = entry_types::string_type;
		using list_type = entry_types::list_type;
		using integer_type = entry_types::integer_type;
		using preformatted_type = entry_types::preformatted_type;
		using uninitialized_type = entry_types::uninitialized_type;

		using variant_type = entry_types::variant_type;

		// the types an entry can have
		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			preformatted_t,
			undefined_t,
		};

		// returns the concrete type of the entry
		data_type type() const;

		// constructors directly from a specific type.
		// The content of the argument is copied into the
		// newly constructed entry
		entry(dictionary_type); // NOLINT
		entry(span<char const>); // NOLINT
		entry(string_view); // NOLINT
		entry(string_type); // NOLINT
		entry(list_type); // NOLINT
		entry(integer_type); // NOLINT
		entry(preformatted_type); // NOLINT

		// hidden
		// this is here to disambiguate between std::string and string_view. It
		// needs to be a template to prevent implicit conversions from literal 0
		template <typename U, typename Cond = typename std::enable_if<
			std::is_same<U, char const*>::value>::type>
		entry(U v);

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
		~entry();

		// copies the structure of the right hand side into this
		// entry.
		entry& operator=(bdecode_node const&) &;
		entry& operator=(entry const&) &;
		entry& operator=(entry&&) & ;
		entry& operator=(dictionary_type) &;
		entry& operator=(span<char const>) &;
		entry& operator=(string_view) &;
		entry& operator=(string_type) &;
		entry& operator=(list_type) &;
		entry& operator=(integer_type) &;
		entry& operator=(preformatted_type) &;

		// hidden
		// this is here to disambiguate between std::string and string_view. It
		// needs to be a template to prevent implicit conversions from literal 0
		template <typename U, typename Cond = typename std::enable_if<
			std::is_same<U, char const*>::value>::type>
		entry& operator=(U v) &;

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
		integer_type const& integer() const;
		string_type& string();
		string_type const& string() const;
		list_type& list();
		list_type const& list() const;
		dictionary_type& dict();
		dictionary_type const& dict() const;
		preformatted_type& preformatted();
		preformatted_type const& preformatted() const;

		// swaps the content of *this* with ``e``.
		using variant_type::swap;

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
		entry const& operator[](string_view key) const;

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

	private:

		template <typename T> T& get();
		template <typename T> T const& get() const;
	};

	// hidden
	TORRENT_EXPORT bool operator==(entry const& lhs, entry const& rhs);
	inline bool operator!=(entry const& lhs, entry const& rhs) { return !(lhs == rhs); }

	// hidden
	// explicit template declaration
	extern template
	entry::entry(char const*);

	// hidden
	// explicit template declaration
	extern template
	entry& entry::operator=(char const*) &;

namespace aux {

	// internal
	TORRENT_EXPORT string_view integer_to_str(std::array<char, 21>& buf
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
