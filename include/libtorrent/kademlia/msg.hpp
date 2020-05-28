/*

Copyright (c) 2007, 2009, 2015, 2019, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
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

#ifndef TORRENT_KADEMLIA_MSG_HPP
#define TORRENT_KADEMLIA_MSG_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {

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
