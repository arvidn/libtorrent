/*

Copyright (c) 2005, Arvid Norberg
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
//#include <iostream>


namespace libtorrent
{
	
	ip_filter::ip_filter()
	{
		// make the entire ip-range non-blocked
		m_access_list.insert(range(address_v4(0UL), 0));
	}

	void ip_filter::add_rule(address_v4 first, address_v4 last, int flags)
	{
		using boost::next;
		using boost::prior;
			  
		assert(!m_access_list.empty());
		assert(first <= last);
		range_t::iterator i = m_access_list.upper_bound(first);
		range_t::iterator j = m_access_list.upper_bound(last);

		if (i != m_access_list.begin()) --i;
		
		assert(j != m_access_list.begin());
		assert(j != i);
		
		int first_access = i->access;
		int last_access = prior(j)->access;

		if (i->start != first && first_access != flags)
		{
			i = m_access_list.insert(i, range(first, flags));
		}
		else if (i != m_access_list.begin() && prior(i)->access == flags)
		{
			--i;
			first_access = i->access;
		}
		assert(!m_access_list.empty());
		assert(i != m_access_list.end());

		if (i != j)
			m_access_list.erase(next(i), j);
		if (i->start == first)
		{
			// we can do this const-cast because we know that the new
			// start address will keep the set correctly ordered
			const_cast<address_v4&>(i->start) = first;
			const_cast<int&>(i->access) = flags;
		}
		else if (first_access != flags)
		{
			m_access_list.insert(i, range(first, flags));
		}
		
		if ((j != m_access_list.end() && j->start.to_ulong() - 1 != last.to_ulong())
			|| (j == m_access_list.end() && last.to_ulong() != 0xffffffff))
		{
			assert(j == m_access_list.end() || last.to_ulong() < j->start.to_ulong() - 1);
			if (last_access != flags)
				j = m_access_list.insert(j, range(address_v4(last.to_ulong() + 1), last_access));
		}

		if (j != m_access_list.end() && j->access == flags) m_access_list.erase(j);
		assert(!m_access_list.empty());
	}

	int ip_filter::access(address_v4 const& addr) const
	{
		assert(!m_access_list.empty());
		range_t::const_iterator i = m_access_list.upper_bound(addr);
		if (i != m_access_list.begin()) --i;
		assert(i != m_access_list.end());
		assert(i->start <= addr && (boost::next(i) == m_access_list.end()
			|| addr < boost::next(i)->start));
		return i->access;
	}


	std::vector<ip_filter::ip_range> ip_filter::export_filter() const
	{
		std::vector<ip_range> ret;
		ret.reserve(m_access_list.size());

		for (range_t::const_iterator i = m_access_list.begin()
			, end(m_access_list.end()); i != end;)
		{
			ip_range r;
			r.first = i->start;
			r.flags = i->access;

			++i;
			if (i == end)
				r.last = address_v4(0xffffffff);
			else
				r.last = address_v4(i->start.to_ulong() - 1);
		
			ret.push_back(r);
		}
		return ret;
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

