/*

Copyright (c) 2006-2016, Arvid Norberg, Thomas Yuan
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

#ifndef TORRENT_PUT_DATA_HPP
#define TORRENT_PUT_DATA_HPP

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/item.hpp>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/function/function1.hpp>
#include <boost/function/function2.hpp>
#include <vector>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent { namespace dht
{
struct msg;
class node;

struct put_data: traversal_algorithm
{
	typedef boost::function<void(item const&, int)> put_callback;

	put_data(node& node, put_callback const& callback);

	virtual char const* name() const TORRENT_OVERRIDE;
	virtual void start() TORRENT_OVERRIDE;

	void set_data(item const& data) { m_data = data; }

	void set_targets(std::vector<std::pair<node_entry, std::string> > const& targets);

protected:

	virtual void done() TORRENT_OVERRIDE;
	virtual bool invoke(observer_ptr o) TORRENT_OVERRIDE;

	put_callback m_put_callback;
	item m_data;
	bool m_done;
};

struct put_data_observer : traversal_observer
{
	put_data_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id, std::string const& token)
		: traversal_observer(algorithm, ep, id)
		, m_token(token)
	{
	}

	virtual void reply(msg const&) { done(); }

	std::string m_token;
};

} } // namespace libtorrent::dht

#endif // TORRENT_PUT_DATA_HPP

