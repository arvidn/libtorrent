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

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/bind.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/invariant_check.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/hasher.hpp>

#include <fstream>

using boost::posix_time::ptime;
using boost::posix_time::time_duration;
using boost::posix_time::microsec_clock;
using boost::posix_time::seconds;
using boost::posix_time::milliseconds;
using boost::shared_ptr;
using boost::bind;

namespace libtorrent { namespace dht
{

namespace io = libtorrent::detail;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(rpc)
#endif

node_id generate_id();

rpc_manager::rpc_manager(fun const& f, node_id const& our_id
	, routing_table& table, send_fun const& sf)
	: m_next_transaction_id(rand() % max_transactions)
	, m_oldest_transaction_id(m_next_transaction_id)
	, m_incoming(f)
	, m_send(sf)
	, m_our_id(our_id)
	, m_table(table)
	, m_timer(boost::posix_time::microsec_clock::universal_time())
	, m_random_number(generate_id())
{
	std::srand(time(0));
}

rpc_manager::~rpc_manager()
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Destructing";
#endif
}

#ifndef NDEBUG
void rpc_manager::check_invariant() const
{
	assert(m_oldest_transaction_id >= 0);
	assert(m_oldest_transaction_id < max_transactions);
	assert(m_next_transaction_id >= 0);
	assert(m_next_transaction_id < max_transactions);
	assert(!m_transactions[m_next_transaction_id]);

	for (int i = (m_next_transaction_id + 1) % max_transactions;
		i != m_oldest_transaction_id; i = (i + 1) % max_transactions)
	{
		assert(!m_transactions[i]);
	}
}
#endif

bool rpc_manager::incoming(msg const& m)
{
	INVARIANT_CHECK;

	if (m.reply)
	{
		// if we don't have the transaction id in our
		// request list, ignore the packet

		if (m.transaction_id.size() != 2)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with invalid transaction id size: " 
				<< m.transaction_id.size() << " from " << m.addr;
#endif
			return false;
		}
	
		std::string::const_iterator i = m.transaction_id.begin();	
		int tid = io::read_uint16(i);

		if (tid >= (int)m_transactions.size()
			|| tid < 0)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with unknown transaction id: " 
				<< tid << " from " << m.addr;
#endif
			return false;
		}
		
		boost::shared_ptr<observer> o = m_transactions[tid];

		if (!o)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with unknown transaction id: " 
				<< tid << " from " << m.addr;
#endif
			return false;
		}
		
		if (m.addr != o->target_addr)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with incorrect address and valid transaction id: " 
				<< tid << " from " << m.addr;
#endif
			return false;
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		std::ofstream reply_stats("libtorrent_logs/round_trip_ms.log", std::ios::app);
		reply_stats << m.addr << "\t" << (microsec_clock::universal_time()
			- o->sent).total_milliseconds() << std::endl;
#endif
		o->reply(m);
		m_transactions[tid].reset();
		
		if (m.piggy_backed_ping)
		{
			// there is a ping request piggy
			// backed in this reply
			msg ph;
			ph.message_id = messages::ping;
			ph.transaction_id = m.ping_transaction_id;
			ph.id = m_our_id;
			ph.addr = m.addr;

			msg empty;
			
			reply(empty, ph);
		}
		return m_table.node_seen(m.id, m.addr);
	}
	else
	{
		// this is an incoming request
		m_incoming(m);
	}
	return false;
}

time_duration rpc_manager::tick()
{
	INVARIANT_CHECK;

	using boost::posix_time::microsec_clock;

	const int timeout_ms = 20 * 1000;

	//	look for observers that has timed out

	if (m_next_transaction_id == m_oldest_transaction_id) return milliseconds(timeout_ms);

	for (;m_next_transaction_id != m_oldest_transaction_id;
		m_oldest_transaction_id = (m_oldest_transaction_id + 1) % max_transactions)
	{
		assert(m_oldest_transaction_id >= 0);
		assert(m_oldest_transaction_id < max_transactions);

		boost::shared_ptr<observer> o = m_transactions[m_oldest_transaction_id];
		if (!o) continue;

		time_duration diff = o->sent + milliseconds(timeout_ms)
			- microsec_clock::universal_time();
		if (diff > seconds(0))
		{
			if (diff < seconds(1)) return seconds(1);
			return diff;
		}
		
		try
		{
			m_transactions[m_oldest_transaction_id].reset();
			o->timeout();
		} catch (std::exception) {}
	}
	return milliseconds(timeout_ms);
}

unsigned int rpc_manager::new_transaction_id()
{
	INVARIANT_CHECK;

	unsigned int tid = m_next_transaction_id;
	m_next_transaction_id = (m_next_transaction_id + 1) % max_transactions;
//	boost::shared_ptr<observer> o = m_transactions[m_next_transaction_id];
	if (m_transactions[m_next_transaction_id])
	{
		m_transactions[m_next_transaction_id].reset();
		assert(m_oldest_transaction_id == m_next_transaction_id);
	}
	if (m_oldest_transaction_id == m_next_transaction_id)
	{
		m_oldest_transaction_id = (m_oldest_transaction_id + 1) % max_transactions;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "WARNING: transaction limit reached! Too many concurrent"
			" messages! limit: " << (int)max_transactions;
#endif
		update_oldest_transaction_id();
	}

#ifndef NDEBUG
	assert(!m_transactions[m_next_transaction_id]);
	for (int i = (m_next_transaction_id + 1) % max_transactions;
		i != m_oldest_transaction_id; i = (i + 1) % max_transactions)
	{
		assert(!m_transactions[i]);
	}
#endif

// hopefully this wouldn't happen, but unfortunately, the
// traversal algorithm will simply fail in case its connections
// are overwritten. If timeout() is called, it will likely spawn
// another connection, which in turn will close the next one
// and so on.
//	if (o) o->timeout();
	return tid;
}

void rpc_manager::update_oldest_transaction_id()
{
	INVARIANT_CHECK;

	assert(m_oldest_transaction_id != m_next_transaction_id);
	while (!m_transactions[m_oldest_transaction_id])
	{
		m_oldest_transaction_id = (m_oldest_transaction_id + 1)
			% max_transactions;
		if (m_oldest_transaction_id == m_next_transaction_id)
			break;
	}
}

void rpc_manager::invoke(int message_id, udp::endpoint target_addr
	, shared_ptr<observer> o)
{
	INVARIANT_CHECK;

	msg m;
	m.message_id = message_id;
	m.reply = false;
	m.id = m_our_id;
	m.addr = target_addr;
	int tid = new_transaction_id();
	m.transaction_id.clear();
	std::back_insert_iterator<std::string> out(m.transaction_id);
	io::write_uint16(tid, out);
	
	o->send(m);

	m_transactions[tid] = o;
	o->sent = boost::posix_time::microsec_clock::universal_time();
	o->target_addr = target_addr;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Invoking " << messages::ids[message_id] 
		<< " -> " << target_addr;
#endif	
	m_send(m);
}

void rpc_manager::reply(msg& m, msg const& reply_to)
{
	INVARIANT_CHECK;

	if (m.message_id != messages::error)
		m.message_id = reply_to.message_id;
	m.addr = reply_to.addr;
	m.reply = true;
	m.piggy_backed_ping = false;
	m.id = m_our_id;
	m.transaction_id = reply_to.transaction_id;
	
	m_send(m);
}

namespace
{
	struct dummy_observer : observer
	{
		virtual void reply(msg const&) {}
		virtual void timeout() {}
		virtual void send(msg&) {}
	};
}

void rpc_manager::reply_with_ping(msg& m, msg const& reply_to)
{
	INVARIANT_CHECK;

	if (m.message_id != messages::error)
		m.message_id = reply_to.message_id;
	m.addr = reply_to.addr;
	m.reply = true;
	m.piggy_backed_ping = true;
	m.id = m_our_id;
	m.transaction_id = reply_to.transaction_id;

	int ptid = new_transaction_id();
	m.ping_transaction_id.clear();
	std::back_insert_iterator<std::string> out(m.ping_transaction_id);
	io::write_uint16(ptid, out);

	boost::shared_ptr<observer> o(new dummy_observer);
	m_transactions[ptid] = o;
	o->sent = boost::posix_time::microsec_clock::universal_time();
	o->target_addr = m.addr;
	
	m_send(m);
}



} } // namespace libtorrent::dht

