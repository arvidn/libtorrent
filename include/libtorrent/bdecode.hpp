/*

Copyright (c) 2015-2016, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/cstdint.hpp>
#include <boost/system/error_code.hpp>
#include <vector>
#include <string>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/assert.hpp"

#ifndef TORRENT_BDECODE_HPP
#define TORRENT_BDECODE_HPP

/*

This is an efficient bdecoder. It decodes into a flat memory buffer of tokens.

Each token has an offset into the bencoded buffer where the token came from
and a next pointer, which is a relative number of tokens to skip forward to
get to the logical next item in a container.

strings and ints offset pointers point to the first character of the length
prefix or the 'i' character. This is to maintain uniformity with other types
and to allow easily calculating the span of a node by subtracting its offset
by the offset of the next node.

example layout:

{
	"a": { "b": 1, "c": "abcd" },
	"d": 3
}

  /---------------------------------------------------------------------------------------\
  |                                                                                       |
  |                                                                                       |
  |                  /--------------------------------------------\                       |
  |                  |                                            |                       |
  |                  |                                            |                       |
  |          /-----\ |       /----\  /----\  /----\  /----\       |  /----\  /----\       |
  | next     |     | |       |    |  |    |  |    |  |    |       |  |    |  |    |       |
  | pointers |     v |       |    v  |    v  |    v  |    v       v  |    v  |    v       v
+-+-----+----+--+----+--+----+--+----+--+----+--+----+--+-------+----+--+----+--+------+  X
| dict  | str   | dict  | str   | int   | str   | str   | end   | str   | int   | end  |
|       |       |       |       |       |       |       |       |       |       |      |
|       |       |       |       |       |       |       |       |       |       |      |
+-+-----+-+-----+-+-----+-+-----+-+-----+-+-----+-+-----+-+-----+-+-----+-+-----+-+----+
  | offset|       |       |       |       |       |       |       |       |       |
  |       |       |       |       |       |       |       |       |       |       |
  |/------/       |       |       |       |       |       |       |       |       |
  ||  /-----------/       |       |       |       |       |       |       |       |
  ||  |/------------------/       |       |       |       |       |       |       |
  ||  ||  /-----------------------/       |       |       |       |       |       |
  ||  ||  |  /----------------------------/       |       |       |       |       |
  ||  ||  |  |  /---------------------------------/       |       |       |       |
  ||  ||  |  |  |     /-----------------------------------/       |       |       |
  ||  ||  |  |  |     |/------------------------------------------/       |       |
  ||  ||  |  |  |     ||  /-----------------------------------------------/       |
  ||  ||  |  |  |     ||  |  /----------------------------------------------------/
  ||  ||  |  |  |     ||  |  |
  vv  vv  v  v  v     vv  v  v
``d1:ad1:bi1e1:c4:abcde1:di3ee``

*/

namespace libtorrent {

TORRENT_EXPORT boost::system::error_category& get_bdecode_category();

namespace bdecode_errors
{
	// libtorrent uses boost.system's ``error_code`` class to represent
	// errors. libtorrent has its own error category get_bdecode_category()
	// with the error codes defined by error_code_enum.
	enum error_code_enum
	{
		// Not an error
		no_error = 0,
		// expected string in bencoded string
		expected_digit,
		// expected colon in bencoded string
		expected_colon,
		// unexpected end of file in bencoded string
		unexpected_eof,
		// expected value (list, dict, int or string) in bencoded string
		expected_value,
		// bencoded recursion depth limit exceeded
		depth_exceeded,
		// bencoded item count limit exceeded
		limit_exceeded,
		// integer overflow
		overflow,

		// the number of error codes
		error_code_max
	};

	// hidden
	TORRENT_EXPORT boost::system::error_code make_error_code(error_code_enum e);
}
} // namespace libtorrent

namespace boost { namespace system {

	template<> struct is_error_code_enum<libtorrent::bdecode_errors::error_code_enum>
	{ static const bool value = true; };

	template<> struct is_error_condition_enum<libtorrent::bdecode_errors::error_code_enum>
	{ static const bool value = true; };
} }

namespace libtorrent {

	typedef boost::system::error_code error_code;

TORRENT_EXTRA_EXPORT char const* parse_int(char const* start
	, char const* end, char delimiter, boost::int64_t& val
	, bdecode_errors::error_code_enum& ec);

namespace detail
{
// internal
struct bdecode_token
{
	// the node with type 'end' is a logical node, pointing to the end
	// of the bencoded buffer.
	enum type_t
	{ none, dict, list, string, integer, end };

	enum limits_t
	{
		max_offset = (1 << 29) - 1,
		max_next_item = (1 << 29) - 1,
		max_header = (1 << 3) - 1
	};

	bdecode_token(boost::uint32_t off, bdecode_token::type_t t)
		: offset(off)
		, type(t)
		, next_item(0)
		, header(0)
	{
		TORRENT_ASSERT(off <= max_offset);
		TORRENT_ASSERT(t >= 0 && t <= end);
	}

	bdecode_token(boost::uint32_t off, boost::uint32_t next
		, bdecode_token::type_t t, boost::uint8_t header_size = 0)
		: offset(off)
		, type(t)
		, next_item(next)
		, header(type == string ? header_size - 2 : 0)
	{
		TORRENT_ASSERT(type != string || header_size >= 2);
		TORRENT_ASSERT(off <= max_offset);
		TORRENT_ASSERT(next <= max_next_item);
		// the string has 2 implied header bytes, to allow for longer prefixes
		TORRENT_ASSERT(header_size < 8 || (type == string && header_size < 10));
		TORRENT_ASSERT(t >= 0 && t <= end);
	}

	int start_offset() const { TORRENT_ASSERT(type == string); return header + 2; }

	// offset into the bdecoded buffer where this node is
	boost::uint32_t offset:29;

	// one of type_t enums
	boost::uint32_t type:3;

	// if this node is a member of a list, 'next_item' is the number of nodes
	// to jump forward in th node array to get to the next item in the list.
	// if it's a key in a dictionary, it's the number of step forwards to get
	// to its corresponding value. If it's a value in a dictionary, it's the
	// number of steps to the next key, or to the end node.
	// this is the _relative_ offset to the next node
	boost::uint32_t next_item:29;

	// this is the number of bytes to skip forward from the offset to get to the
	// first character of the string, if this is a string. This field is not
	// used for other types. Essentially this is the length of the length prefix
	// and the colon. Since a string always has at least one character of length
	// prefix and always a colon, those 2 characters are implied.
	boost::uint32_t header:3;
};
}

// a ``bdecode_node`` is used to traverse and hold the tree structure defined
// by bencoded data after it has been parse by bdecode().
// 
// There are primarily two kinds of bdecode_nodes. The ones that own the tree
// structure, and defines its lifetime, and nodes that are child nodes in the
// tree, pointing back into the root's tree.
// 
// The ``bdecode_node`` passed in to ``bdecode()`` becomes the one owning the
// tree structure. Make sure not to destruct that object for as long as you
// use any of its child nodes. Also, keep in mind that the buffer originally
// parsed also must remain valid while using it. (see switch_underlying_buffer()).
//
// Copying an owning node will create a copy of the whole tree, but will still
// point into the same parsed bencoded buffer as the first one.

// Sometimes it's important to get a non-owning reference to the root node (
// to be able to copy it as a reference for instance). For that, use the
// non_owning() member function.
//
// There are 5 different types of nodes, see type_t.
struct TORRENT_EXPORT bdecode_node
{
	TORRENT_EXPORT friend int bdecode(char const* start, char const* end, bdecode_node& ret
		, error_code& ec, int* error_pos, int depth_limit
		, int token_limit);

	// creates a default constructed node, it will have the type ``none_t``.
	bdecode_node();

	// For owning nodes, the copy will create a copy of the tree, but the
	// underlying buffer remains the same.
	bdecode_node(bdecode_node const&);
	bdecode_node& operator=(bdecode_node const&);

	// the types of bdecoded nodes
	enum type_t
	{
		// uninitialized or default constructed. This is also used
		// to indicate that a node was not found in some cases.
		none_t,
		// a dictionary node. The ``dict_find_`` functions are valid.
		dict_t,
		// a list node. The ``list_`` functions are valid.
		list_t,
		// a string node, the ``string_`` functions are valid.
		string_t,
		// an integer node. The ``int_`` functions are valid.
		int_t
	};

	// the type of this node. See type_t.
	type_t type() const;

	// returns true if type() != none_t.
	operator bool() const;

	// return a non-owning reference to this node. This is useful to refer to
	// the root node without copying it in assignments.
	bdecode_node non_owning() const;

	// returns the buffer and length of the section in the original bencoded
	// buffer where this node is defined. For a dictionary for instance, this
	// starts with ``d`` and ends with ``e``, and has all the content of the
	// dictionary in between.
	std::pair<char const*, int> data_section() const;

	// functions with the ``list_`` prefix operate on lists. These functions are
	// only valid if ``type()`` == ``list_t``. ``list_at()`` returns the item
	// in the list at index ``i``. ``i`` may not be greater than or equal to the
	// size of the list. ``size()`` returns the size of the list.
	bdecode_node list_at(int i) const;
	std::string list_string_value_at(int i
		, char const* default_val = "");
	boost::int64_t list_int_value_at(int i
		, boost::int64_t default_val = 0);
	int list_size() const;

	// Functions with the ``dict_`` prefix operates on dictionaries. They are
	// only valid if ``type()`` == ``dict_t``. In case a key you're looking up
	// contains a 0 byte, you cannot use the null-terminated string overloads,
	// but have to use ``std::string`` instead. ``dict_find_list`` will return a
	// valid ``bdecode_node`` if the key is found _and_ it is a list. Otherwise
	// it will return a default-constructed bdecode_node.
	// 
	// Functions with the ``_value`` suffix return the value of the node
	// directly, rather than the nodes. In case the node is not found, or it has
	// a different type, a default value is returned (which can be specified).
	bdecode_node dict_find(std::string key) const;
	bdecode_node dict_find(char const* key) const;
	std::pair<std::string, bdecode_node> dict_at(int i) const;
	bdecode_node dict_find_dict(std::string key) const;
	bdecode_node dict_find_dict(char const* key) const;
	bdecode_node dict_find_list(char const* key) const;
	bdecode_node dict_find_string(char const* key) const;
	bdecode_node dict_find_int(char const* key) const;
	std::string dict_find_string_value(char const* key
		, char const* default_value = "") const;
	boost::int64_t dict_find_int_value(char const* key
		, boost::int64_t default_val = 0) const;
	int dict_size() const;

	// this function is only valid if ``type()`` == ``int_t``. It returns the
	// value of the integer.
	boost::int64_t int_value() const;

	// these functions are only valid if ``type()`` == ``string_t``. They return
	// the string values. Note that ``string_ptr()`` is *not* null-terminated.
	// ``string_length()`` returns the number of bytes in the string.
	std::string string_value() const;
	char const* string_ptr() const;
	int string_length() const;

	// resets the ``bdecoded_node`` to a default constructed state. If this is
	// an owning node, the tree is freed and all child nodes are invalidated.
	void clear();

	// Swap contents.
	void swap(bdecode_node& n);

	// pre-allocate memory for the specified numbers of tokens. This is
	// useful if you know approximately how many tokens are in the file
	// you are about to parse. Doing so will save realloc operations
	// while parsing. You should only call this on the root node, before
	// passing it in to bdecode().
	void reserve(int tokens);

	// this buffer *MUST* be identical to the one originally parsed. This
	// operation is only defined on owning root nodes, i.e. the one passed in to
	// decode().
	void switch_underlying_buffer(char const* buf);

private:
	bdecode_node(detail::bdecode_token const* tokens, char const* buf
		, int len, int idx);

	// if this is the root node, that owns all the tokens, they live in this
	// vector. If this is a sub-node, this field is not used, instead the
	// m_root_tokens pointer points to the root node's token.
	std::vector<detail::bdecode_token> m_tokens;

	// this points to the root nodes token vector
	// for the root node, this points to its own m_tokens member
	detail::bdecode_token const* m_root_tokens;

	// this points to the original buffer that was parsed
	char const* m_buffer;
	int m_buffer_size;

	// this is the index into m_root_tokens that this node refers to
	// for the root node, it's 0. -1 means uninitialized.
	int m_token_idx;

	// this is a cache of the last element index looked up. This only applies
	// to lists and dictionaries. If the next lookup is at m_last_index or
	// greater, we can start iterating the tokens at m_last_token.
	mutable int m_last_index;
	mutable int m_last_token;

	// the number of elements in this list or dict (computed on the first
	// call to dict_size() or list_size())
	mutable int m_size;
};

// print the bencoded structure in a human-readable format to a string
// that's returned.
TORRENT_EXPORT std::string print_entry(bdecode_node const& e
	, bool single_line = false, int indent = 0);

// This function decodes/parses bdecoded data (for example a .torrent file).
// The data structure is returned in the ``ret`` argument. the buffer to parse
// is specified by the ``start`` of the buffer as well as the ``end``, i.e. one
// byte past the end. If the buffer fails to parse, the function returns a
// non-zero value and fills in ``ec`` with the error code. The optional
// argument ``error_pos``, if set to non-null, will be set to the byte offset
// into the buffer where the parse failure occurred.
// 
// ``depth_limit`` specifies the max number of nested lists or dictionaries are
// allowed in the data structure. (This affects the stack usage of the
// function, be careful not to set it too high).
//
// ``token_limit`` is the max number of tokens allowed to be parsed from the
// buffer. This is simply a sanity check to not have unbounded memory usage.
// 
// The resulting ``bdecode_node`` is an *owning* node. That means it will
// be holding the whole parsed tree. When iterating lists and dictionaries,
// those ``bdecode_node`` objects will simply have references to the root or
// owning ``bdecode_node``. If the root node is destructed, all other nodes
// that refer to anything in that tree become invalid.
// 
// However, the underlying buffer passed in to this function (``start``, ``end``)
// must also remain valid while the bdecoded tree is used. The parsed tree
// produced by this function does not copy any data out of the buffer, but
// simply produces references back into it.
TORRENT_EXPORT int bdecode(char const* start, char const* end, bdecode_node& ret
	, error_code& ec, int* error_pos = 0, int depth_limit = 100
	, int token_limit = 1000000);

}

#endif // TORRENT_BDECODE_HPP

