/*

Copyright (c) 2007, 2009, 2015, 2019-2020, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_KADEMLIA_MSG_HPP
#define TORRENT_KADEMLIA_MSG_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"

namespace lt {

struct bdecode_node;

namespace dht {

struct msg
{
	msg(bdecode_node const& m, udp::endpoint const& ep): message(m), addr(ep) {}

	// explicitly disallow assignment, to silence msvc warning
	msg& operator=(msg const&) = delete;

	// the message
	bdecode_node const& message;

	// the address of the process sending or receiving
	// the message.
	udp::endpoint addr;
};

struct key_desc_t
{
	char const* name;
	int type;
	int size;
	int flags;

	enum {
		// this argument is optional, parsing will not
		// fail if it's not present
		optional = 1,
		// for dictionaries, the following entries refer
		// to child nodes to this node, up until and including
		// the next item that has the last_child flag set.
		// these flags are nestable
		parse_children = 2,
		// this is the last item in a child dictionary
		last_child = 4,
		// the size argument refers to that the size
		// has to be divisible by the number, instead
		// of having that exact size
		size_divisible = 8
	};
};

// TODO: move this to its own .hpp/.cpp pair?
TORRENT_EXTRA_EXPORT bool verify_message_impl(bdecode_node const& message, span<key_desc_t const> desc
	, span<bdecode_node> ret, span<char> error);

// verifies that a message has all the required
// entries and returns them in ret
template <int Size>
bool verify_message(bdecode_node const& msg, key_desc_t const (&desc)[Size]
	, bdecode_node (&ret)[Size], span<char> error)
{
	return verify_message_impl(msg, desc, ret, error);
}

}
}

#endif
