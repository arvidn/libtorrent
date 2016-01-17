/*

Copyright (c) 2005-2016, Arvid Norberg
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

#ifndef TORRENT_IP_FILTER_HPP
#define TORRENT_IP_FILTER_HPP

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <set>
#include <vector>

#include <boost/limits.hpp>
#include <boost/utility.hpp>
#include <boost/cstdint.hpp>
#include <boost/tuple/tuple.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/address.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

// hidden
inline bool operator<=(address const& lhs
	, address const& rhs)
{
	return lhs < rhs || lhs == rhs;
}

template <class Addr>
struct ip_range
{
	Addr first;
	Addr last;
	boost::uint32_t flags;
};

namespace detail
{

	template<class Addr>
	Addr zero()
	{
		Addr zero;
		std::fill(zero.begin(), zero.end(), 0);
		return zero;
	}

	template<>
	inline boost::uint16_t zero<boost::uint16_t>() { return 0; }

	template<class Addr>
	Addr plus_one(Addr const& a)
	{
		Addr tmp(a);
		for (int i = int(tmp.size()) - 1; i >= 0; --i)
		{
			if (tmp[i] < (std::numeric_limits<typename Addr::value_type>::max)())
			{
				tmp[i] += 1;
				break;
			}
			tmp[i] = 0;
		}
		return tmp;
	}

	inline boost::uint16_t plus_one(boost::uint16_t val) { return val + 1; }
	
	template<class Addr>
	Addr minus_one(Addr const& a)
	{
		Addr tmp(a);
		for (int i = int(tmp.size()) - 1; i >= 0; --i)
		{
			if (tmp[i] > 0)
			{
				tmp[i] -= 1;
				break;
			}
			tmp[i] = (std::numeric_limits<typename Addr::value_type>::max)();
		}
		return tmp;
	}

	inline boost::uint16_t minus_one(boost::uint16_t val) { return val - 1; }

	template<class Addr>
	Addr max_addr()
	{
		Addr tmp;
		std::fill(tmp.begin(), tmp.end()
			, (std::numeric_limits<typename Addr::value_type>::max)());
		return Addr(tmp);
	}

	template<>
	inline boost::uint16_t max_addr<boost::uint16_t>()
	{ return (std::numeric_limits<boost::uint16_t>::max)(); }

	// this is the generic implementation of
	// a filter for a specific address type.
	// it works with IPv4 and IPv6
	template<class Addr>
	class filter_impl
	{
	public:

		filter_impl()
		{
			// make the entire ip-range non-blocked
			m_access_list.insert(range(zero<Addr>(), 0));
		}

		void add_rule(Addr first, Addr last, int flags)
		{
			TORRENT_ASSERT(!m_access_list.empty());
			TORRENT_ASSERT(first < last || first == last);

			typename range_t::iterator i = m_access_list.upper_bound(first);
			typename range_t::iterator j = m_access_list.upper_bound(last);

			if (i != m_access_list.begin()) --i;

			TORRENT_ASSERT(j != m_access_list.begin());
			TORRENT_ASSERT(j != i);

			boost::uint32_t first_access = i->access;
			boost::uint32_t last_access = boost::prior(j)->access;

			if (i->start != first && first_access != flags)
			{
				i = m_access_list.insert(i, range(first, flags));
			}
			else if (i != m_access_list.begin() && boost::prior(i)->access == flags)
			{
				--i;
				first_access = i->access;
			}
			TORRENT_ASSERT(!m_access_list.empty());
			TORRENT_ASSERT(i != m_access_list.end());

			if (i != j) m_access_list.erase(boost::next(i), j);
			if (i->start == first)
			{
				// we can do this const-cast because we know that the new
				// start address will keep the set correctly ordered
				const_cast<Addr&>(i->start) = first;
				const_cast<boost::uint32_t&>(i->access) = flags;
			}
			else if (first_access != flags)
			{
				m_access_list.insert(i, range(first, flags));
			}

			if ((j != m_access_list.end()
					&& minus_one(j->start) != last)
				|| (j == m_access_list.end()
					&& last != max_addr<Addr>()))
			{
				TORRENT_ASSERT(j == m_access_list.end() || last < minus_one(j->start));
				if (last_access != flags)
					j = m_access_list.insert(j, range(plus_one(last), last_access));
			}

			if (j != m_access_list.end() && j->access == flags) m_access_list.erase(j);
			TORRENT_ASSERT(!m_access_list.empty());
		}

		boost::uint32_t access(Addr const& addr) const
		{
			TORRENT_ASSERT(!m_access_list.empty());
			typename range_t::const_iterator i = m_access_list.upper_bound(addr);
			if (i != m_access_list.begin()) --i;
			TORRENT_ASSERT(i != m_access_list.end());
			TORRENT_ASSERT(i->start <= addr && (boost::next(i) == m_access_list.end()
				|| addr < boost::next(i)->start));
			return i->access;
		}

		template <class ExternalAddressType>
		std::vector<ip_range<ExternalAddressType> > export_filter() const
		{
			std::vector<ip_range<ExternalAddressType> > ret;
			ret.reserve(m_access_list.size());

			for (typename range_t::const_iterator i = m_access_list.begin()
				, end(m_access_list.end()); i != end;)
			{
				ip_range<ExternalAddressType> r;
				r.first = ExternalAddressType(i->start);
				r.flags = i->access;

				++i;
				if (i == end)
					r.last = ExternalAddressType(max_addr<Addr>());
				else
					r.last = ExternalAddressType(minus_one(i->start));

				ret.push_back(r);
			}
			return ret;
		}

	private:

		struct range
		{
			range(Addr addr, int a = 0): start(addr), access(a) {}
			bool operator<(range const& r) const
			{ return start < r.start; }
			bool operator<(Addr const& a) const
			{ return start < a; }
			Addr start;
			// the end of the range is implicit
			// and given by the next entry in the set
			boost::uint32_t access;
		};

		typedef std::set<range> range_t;
		range_t m_access_list;
	
	};

}

// The ``ip_filter`` class is a set of rules that uniquely categorizes all
// ip addresses as allowed or disallowed. The default constructor creates
// a single rule that allows all addresses (0.0.0.0 - 255.255.255.255 for
// the IPv4 range, and the equivalent range covering all addresses for the
// IPv6 range).
//
// A default constructed ip_filter does not filter any address.
struct TORRENT_EXPORT ip_filter
{
	// the flags defined for an IP range
	enum access_flags
	{
		// indicates that IPs in this range should not be connected
		// to nor accepted as incoming connections
		blocked = 1
	};

	// Adds a rule to the filter. ``first`` and ``last`` defines a range of
	// ip addresses that will be marked with the given flags. The ``flags``
	// can currently be 0, which means allowed, or ``ip_filter::blocked``, which
	// means disallowed.
	// 
	// precondition:
	// ``first.is_v4() == last.is_v4() && first.is_v6() == last.is_v6()``
	// 
	// postcondition:
	// ``access(x) == flags`` for every ``x`` in the range [``first``, ``last``]
	// 
	// This means that in a case of overlapping ranges, the last one applied takes
	// precedence.
	void add_rule(address first, address last, boost::uint32_t flags);

	// Returns the access permissions for the given address (``addr``). The permission
	// can currently be 0 or ``ip_filter::blocked``. The complexity of this operation
	// is O(``log`` n), where n is the minimum number of non-overlapping ranges to describe
	// the current filter.
	int access(address const& addr) const;

#if TORRENT_USE_IPV6
	typedef boost::tuple<std::vector<ip_range<address_v4> >
		, std::vector<ip_range<address_v6> > > filter_tuple_t;
#else
	typedef std::vector<ip_range<address_v4> > filter_tuple_t;
#endif

	// This function will return the current state of the filter in the minimum number of
	// ranges possible. They are sorted from ranges in low addresses to high addresses. Each
	// entry in the returned vector is a range with the access control specified in its
	// ``flags`` field.
	//
	// The return value is a tuple containing two range-lists. One for IPv4 addresses
	// and one for IPv6 addresses.
	filter_tuple_t export_filter() const;

//	void print() const;

private:

	detail::filter_impl<address_v4::bytes_type> m_filter4;
#if TORRENT_USE_IPV6
	detail::filter_impl<address_v6::bytes_type> m_filter6;
#endif
};

// the port filter maps non-overlapping port ranges to flags. This
// is primarily used to indicate whether a range of ports should
// be connected to or not. The default is to have the full port
// range (0-65535) set to flag 0.
class TORRENT_EXPORT port_filter
{
public:

	// the defined flags for a port range
	enum access_flags
	{
		// this flag indicates that destination ports in the
		// range should not be connected to
		blocked = 1
	};

	// set the flags for the specified port range (``first``, ``last``) to
	// ``flags`` overwriting any existing rule for those ports. The range
	// is inclusive, i.e. the port ``last`` also has the flag set on it.
	void add_rule(boost::uint16_t first, boost::uint16_t last, boost::uint32_t flags);

	// test the specified port (``port``) for whether it is blocked
	// or not. The returned value is the flags set for this port.
	// see acces_flags.
	int access(boost::uint16_t port) const;

private:

	detail::filter_impl<boost::uint16_t> m_filter;

};

}

#endif

