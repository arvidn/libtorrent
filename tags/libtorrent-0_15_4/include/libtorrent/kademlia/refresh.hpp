/*

Copyright (c) 2006, Arvid Norberg & Daniel Wallin
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

#ifndef REFRESH_050324_HPP
#define REFRESH_050324_HPP

#include <vector>

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <boost/function.hpp>

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(refresh);
#endif

class routing_table;
class rpc_manager;

class refresh : public traversal_algorithm
{
public:
	typedef std::vector<node_entry>::iterator InIt;
	typedef boost::function<void()> done_callback;

	void ping_reply(node_id id);
	void ping_timeout(node_id id, bool prevent_request = false);

	refresh(node_impl& node, node_id target, InIt first, InIt last
		, done_callback const& callback);

	virtual char const* name() const { return "refresh"; }

private:

	void done();
	void invoke(node_id const& id, udp::endpoint addr);

	void invoke_pings_or_finish(bool prevent_request = false);

	int m_max_active_pings;
	int m_active_pings;

	done_callback m_done_callback;
	
	std::vector<result>::iterator m_leftover_nodes_iterator;
};

class refresh_observer : public observer
{
public:
	refresh_observer(
		boost::intrusive_ptr<refresh> const& algorithm
		, node_id self)
		: observer(algorithm->allocator())
		, m_algorithm(algorithm)
		, m_self(self)
	{}
	~refresh_observer();

	void send(msg& m)
	{
		m.info_hash = m_algorithm->target();
	}

	void timeout();
	void reply(msg const& m);
	void abort() { m_algorithm = 0; }


private:
	boost::intrusive_ptr<refresh> m_algorithm;
	node_id const m_self;
};

class ping_observer : public observer
{
public:
	ping_observer(
		boost::intrusive_ptr<refresh> const& algorithm
		, node_id self)
		: observer(algorithm->allocator())
		, m_self(self)
		, m_algorithm(algorithm)
	{}
	~ping_observer();

	void send(msg& p) {}
	void timeout();
	void reply(msg const& m);
	void abort() { m_algorithm = 0; }


private:
	node_id const m_self;
	boost::intrusive_ptr<refresh> m_algorithm;
};

inline refresh::refresh(
	node_impl& node
	, node_id target
	, refresh::InIt first
	, refresh::InIt last
	, done_callback const& callback)
	: traversal_algorithm(node, target, first, last)
	, m_max_active_pings(10)
	, m_active_pings(0)
	, m_done_callback(callback)
{
	boost::intrusive_ptr<refresh> self(this);
	add_requests();
}

} } // namespace libtorrent::dht

#endif // REFRESH_050324_HPP

