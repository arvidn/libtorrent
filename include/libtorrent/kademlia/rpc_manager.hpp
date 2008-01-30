/*

Copyright (c) 2006, Arvid Norberg
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

#ifndef RPC_MANAGER_HPP
#define RPC_MANAGER_HPP

#include <vector>
#include <map>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/cstdint.hpp>
#include <boost/array.hpp>

#include <libtorrent/socket.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/packet_iterator.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/node_entry.hpp>

namespace libtorrent { namespace dht
{

using asio::ip::udp;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(rpc);
#endif

typedef std::vector<char> packet_t;

namespace messages
{
	enum { ping = 0, find_node = 1, get_peers = 2, announce_peer = 3, error = 4 };
	char const* const ids[] = { "ping", "find_node", "get_peers", "announce_peer", "error" }; 
} // namespace messages

struct msg
{
	msg() : reply(false), piggy_backed_ping(false)
		, port(0) {}

	// true if this message is a reply
	bool reply;
	// true if this is a reply with a piggy backed ping
	bool piggy_backed_ping;
	// the kind if message
	int message_id;
	// if this is a reply, a copy of the transaction id
	// from the request. If it's a request, a transaction
	// id that should be sent back in the reply
	std::string transaction_id;
	// if this packet has a piggy backed ping, this
	// is the transaction id of that ping
	std::string ping_transaction_id;
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
	entry write_token;

	// the info has for peer_requests, announce_peer
	// and responses
	node_id info_hash;
	
	// port for announce_peer messages
	int port;
	
	// ERROR MESSAGES
	int error_code;
	std::string error_msg;
};

struct observer : boost::noncopyable
{
	observer()
		: sent(boost::posix_time::microsec_clock::universal_time())
	{}

	virtual ~observer() {}

	// this two callbacks lets the observer add
	// information to the message before it's sent
	virtual void send(msg& m) = 0;

	// this is called when a reply is received
	virtual void reply(msg const& m) = 0;

	// this is called when no reply has been received within
	// some timeout
	virtual void timeout() = 0;
	
	// if this is called the destructor should
	// not invoke any new messages, and should
	// only clean up. It means the rpc-manager
	// is being destructed
	virtual void abort() = 0;

	udp::endpoint target_addr;
	boost::posix_time::ptime sent;
};

class routing_table;

class rpc_manager
{
public:
	typedef boost::function1<void, msg const&> fun;
	typedef boost::function1<void, msg const&> send_fun;

	rpc_manager(fun const& incoming_fun, node_id const& our_id
		, routing_table& table, send_fun const& sf);
	~rpc_manager();

	// returns true if the node needs a refresh
	bool incoming(msg const&);
	boost::posix_time::time_duration tick();

	void invoke(int message_id, udp::endpoint target
		, boost::shared_ptr<observer> o);

	void reply(msg& m, msg const& reply_to);
	void reply_with_ping(msg& m, msg const& reply_to);

#ifndef NDEBUG
	void check_invariant() const;
#endif

private:

	enum { max_transactions = 2048 };

	unsigned int new_transaction_id(boost::shared_ptr<observer> o);
	void update_oldest_transaction_id();
	
	boost::uint32_t calc_connection_id(udp::endpoint addr);

	typedef boost::array<boost::shared_ptr<observer>, max_transactions>
		transactions_t;
	transactions_t m_transactions;
	std::vector<boost::shared_ptr<observer> > m_aborted_transactions;
	
	// this is the next transaction id to be used
	int m_next_transaction_id;
	// this is the oldest transaction id still
	// (possibly) in use. This is the transaction
	// that will time out first, the one we are
	// waiting for to time out
	int m_oldest_transaction_id;
	
	fun m_incoming;
	send_fun m_send;
	node_id m_our_id;
	routing_table& m_table;
	boost::posix_time::ptime m_timer;
	node_id m_random_number;
	bool m_destructing;
};

} } // namespace libtorrent::dht

#endif


