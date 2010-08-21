/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef MSG_HPP
#define MSG_HPP

#include <string>
#include <libtorrent/kademlia/node_id.hpp>
#if BOOST_VERSION < 103500
#include <asio/ip/udp.hpp>
#else
#include <boost/asio/ip/udp.hpp>
#endif

namespace libtorrent {
namespace dht {

typedef std::vector<char> packet_t;

namespace messages
{
	enum { ping = 0, find_node = 1, get_peers = 2, announce_peer = 3, error = 4 };
	char const* const ids[] = { "ping", "find_node", "get_peers", "announce_peer", "error" }; 
} // namespace messages

struct msg
{
	msg()
		: reply(false)
		, message_id(-1)
		, port(0)
	{}

	// true if this message is a reply
	bool reply;
	// the kind if message
	int message_id;
	// if this is a reply, a copy of the transaction id
	// from the request. If it's a request, a transaction
	// id that should be sent back in the reply
	std::string transaction_id;
	// the node id of the process sending the message
	node_id id;
	// the address of the process sending or receiving
	// the message.
	udp::endpoint addr;
	// if this is a nodes response, these are the nodes
	typedef std::vector<node_entry> nodes_t;
	nodes_t nodes;

	typedef std::vector<tcp::endpoint> peers_t;
	peers_t peers;
	
	// similar to transaction_id but for write operations.
	std::string write_token;

	// the info has for peer_requests, announce_peer
	// and responses
	node_id info_hash;
	
	// port for announce_peer messages
	int port;
	
	// ERROR MESSAGES
	int error_code;
	std::string error_msg;
};


} }

#endif
