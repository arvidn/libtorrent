/*

Copyright (c) 2005-2007, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017-2018, 2020, Alden Torres
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

#include <iterator> // for next

#include "libtorrent/ip_filter.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent {

	ip_filter::ip_filter() = default;
	ip_filter::ip_filter(ip_filter const&) = default;
	ip_filter::ip_filter(ip_filter&&) = default;
	ip_filter& ip_filter::operator=(ip_filter const&) = default;
	ip_filter& ip_filter::operator=(ip_filter&&) = default;
	ip_filter::~ip_filter() = default;

	bool ip_filter::empty() const { return m_filter4.empty() && m_filter6.empty(); }

	void ip_filter::add_rule(address const& first, address const& last, std::uint32_t flags)
	{
		if (first.is_v4())
		{
			TORRENT_ASSERT(last.is_v4());
			m_filter4.add_rule(first.to_v4().to_bytes(), last.to_v4().to_bytes(), flags);
		}
		else if (first.is_v6())
		{
			TORRENT_ASSERT(last.is_v6());
			m_filter6.add_rule(first.to_v6().to_bytes(), last.to_v6().to_bytes(), flags);
		}
		else
			TORRENT_ASSERT_FAIL();
	}

	std::uint32_t ip_filter::access(address const& addr) const
	{
		if (addr.is_v4())
			return m_filter4.access(addr.to_v4().to_bytes());
		TORRENT_ASSERT(addr.is_v6());
		return m_filter6.access(addr.to_v6().to_bytes());
	}

	ip_filter::filter_tuple_t ip_filter::export_filter() const
	{
		return std::make_tuple(m_filter4.export_filter<address_v4>()
			, m_filter6.export_filter<address_v6>());
	}

	port_filter::port_filter() = default;
	port_filter::port_filter(port_filter const&) = default;
	port_filter::port_filter(port_filter&&) = default;
	port_filter& port_filter::operator=(port_filter const&) = default;
	port_filter& port_filter::operator=(port_filter&&) = default;
	port_filter::~port_filter() = default;

	void port_filter::add_rule(std::uint16_t first, std::uint16_t last, std::uint32_t flags)
	{
		m_filter.add_rule(first, last, flags);
	}

	std::uint32_t port_filter::access(std::uint16_t port) const
	{
		return m_filter.access(port);
	}

namespace aux {

	template <typename Addr>
	Addr zero()
	{
		Addr zero;
		std::fill(zero.begin(), zero.end(), static_cast<typename Addr::value_type>(0));
		return zero;
	}

	template <typename Addr>
	Addr plus_one(Addr const& a)
	{
		Addr tmp(a);
		for (int i = int(tmp.size()) - 1; i >= 0; --i)
		{
			auto& t = tmp[std::size_t(i)];
			if (t < (std::numeric_limits<typename Addr::value_type>::max)())
			{
				t += 1;
				break;
			}
			t = 0;
		}
		return tmp;
	}

	template <typename Addr>
	Addr minus_one(Addr const& a)
	{
		Addr tmp(a);
		for (int i = int(tmp.size()) - 1; i >= 0; --i)
		{
			auto& t = tmp[std::size_t(i)];
			if (t > 0)
			{
				t -= 1;
				break;
			}
			t = (std::numeric_limits<typename Addr::value_type>::max)();
		}
		return tmp;
	}

	template <typename Addr>
	Addr max_addr()
	{
		Addr tmp;
		std::fill(tmp.begin(), tmp.end()
			, (std::numeric_limits<typename Addr::value_type>::max)());
		return tmp;
	}

#ifdef _MSC_VER
#define EXPORT_INST TORRENT_EXTRA_EXPORT
#else
#define EXPORT_INST
#endif

	template EXPORT_INST address_v4::bytes_type minus_one<address_v4::bytes_type>(address_v4::bytes_type const&);
	template EXPORT_INST address_v6::bytes_type minus_one<address_v6::bytes_type>(address_v6::bytes_type const&);
	template EXPORT_INST address_v4::bytes_type plus_one<address_v4::bytes_type>(address_v4::bytes_type const&);
	template EXPORT_INST address_v6::bytes_type plus_one<address_v6::bytes_type>(address_v6::bytes_type const&);
	template EXPORT_INST address_v4::bytes_type zero<address_v4::bytes_type>();
	template EXPORT_INST address_v6::bytes_type zero<address_v6::bytes_type>();
	template EXPORT_INST address_v4::bytes_type max_addr<address_v4::bytes_type>();
	template EXPORT_INST address_v6::bytes_type max_addr<address_v6::bytes_type>();

	template <typename Addr>
	filter_impl<Addr>::filter_impl()
	{
		// make the entire ip-range non-blocked
		m_access_list.insert(range(zero<Addr>(), 0));
	}

	template <typename Addr>
	bool filter_impl<Addr>::empty() const
	{
		return m_access_list.empty()
			|| (m_access_list.size() == 1 && *m_access_list.begin() == range(zero<Addr>(), 0));
	}

	template <typename Addr>
	void filter_impl<Addr>::add_rule(Addr first, Addr last, std::uint32_t const flags)
	{
		TORRENT_ASSERT(!m_access_list.empty());
		TORRENT_ASSERT(first < last || first == last);

		auto i = m_access_list.upper_bound(first);
		auto j = m_access_list.upper_bound(last);

		if (i != m_access_list.begin()) --i;

		TORRENT_ASSERT(j != m_access_list.begin());
		TORRENT_ASSERT(j != i);

		std::uint32_t first_access = i->access;
		std::uint32_t last_access = std::prev(j)->access;

		if (i->start != first && first_access != flags)
		{
			i = m_access_list.insert(i, range(first, flags));
		}
		else if (i != m_access_list.begin() && std::prev(i)->access == flags)
		{
			--i;
			first_access = i->access;
		}
		TORRENT_ASSERT(!m_access_list.empty());
		TORRENT_ASSERT(i != m_access_list.end());

		if (i != j) m_access_list.erase(std::next(i), j);
		if (i->start == first)
		{
			// This is an optimization over erasing and inserting a new element
			// here.
			// this const-cast is OK because we know that the new
			// start address will keep the set correctly ordered
			const_cast<Addr&>(i->start) = first;
			const_cast<std::uint32_t&>(i->access) = flags;
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

	template <typename Addr>
	std::uint32_t filter_impl<Addr>::access(Addr const& addr) const
	{
		TORRENT_ASSERT(!m_access_list.empty());
		auto i = m_access_list.upper_bound(addr);
		if (i != m_access_list.begin()) --i;
		TORRENT_ASSERT(i != m_access_list.end());
		TORRENT_ASSERT(i->start <= addr && (std::next(i) == m_access_list.end()
			|| addr < std::next(i)->start));
		return i->access;
	}

	template <typename Addr>
	template <typename ExternalAddressType>
	std::vector<ip_range<ExternalAddressType>> filter_impl<Addr>::export_filter() const
	{
		std::vector<ip_range<ExternalAddressType>> ret;
		ret.reserve(m_access_list.size());

		for (auto i = m_access_list.begin()
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

	template class EXPORT_INST filter_impl<address_v4::bytes_type>;
	template class EXPORT_INST filter_impl<address_v6::bytes_type>;
	template class EXPORT_INST filter_impl<std::uint16_t>;

	template EXPORT_INST std::vector<ip_range<address_v4>> filter_impl<address_v4::bytes_type>::export_filter() const;
	template EXPORT_INST std::vector<ip_range<address_v6>> filter_impl<address_v6::bytes_type>::export_filter() const;
	template EXPORT_INST std::vector<ip_range<std::uint16_t>> filter_impl<std::uint16_t>::export_filter() const;

#undef EXPORT_INST

} // namespace aux
}
