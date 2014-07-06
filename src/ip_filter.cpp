/*

Copyright (c) 2005-2014, Arvid Norberg
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

#include "libtorrent/ip_filter.hpp"
#include <boost/utility.hpp>


namespace libtorrent
{
	void ip_filter::add_rule(address first, address last, boost::uint32_t flags)
	{
		if (first.is_v4())
		{
			TORRENT_ASSERT(last.is_v4());
			m_filter4.add_rule(first.to_v4().to_bytes(), last.to_v4().to_bytes(), flags);
		}
#if TORRENT_USE_IPV6
		else if (first.is_v6())
		{
			TORRENT_ASSERT(last.is_v6());
			m_filter6.add_rule(first.to_v6().to_bytes(), last.to_v6().to_bytes(), flags);
		}
#endif
		else
			TORRENT_ASSERT(false);
	}

	int ip_filter::access(address const& addr) const
	{
		if (addr.is_v4())
			return m_filter4.access(addr.to_v4().to_bytes());
#if TORRENT_USE_IPV6
		TORRENT_ASSERT(addr.is_v6());
		return m_filter6.access(addr.to_v6().to_bytes());
#else
		return 0;
#endif
	}

	ip_filter::filter_tuple_t ip_filter::export_filter() const
	{
#if TORRENT_USE_IPV6
		return boost::make_tuple(m_filter4.export_filter<address_v4>()
			, m_filter6.export_filter<address_v6>());
#else
		return m_filter4.export_filter<address_v4>();
#endif
	}
	
	void port_filter::add_rule(boost::uint16_t first, boost::uint16_t last, boost::uint32_t flags)
	{
		m_filter.add_rule(first, last, flags);
	}

	int port_filter::access(boost::uint16_t port) const
	{
		return m_filter.access(port);
	}
/*
	void ip_filter::print() const
	{
		for (range_t::iterator i =  m_access_list.begin(); i != m_access_list.end(); ++i)
		{
			std::cout << i->start.as_string() << " " << i->access << "\n";
		}
	}
*/
}

