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

#ifndef PACKET_ITERATOR_HPP
#define PACKET_ITERATOR_HPP

#include <boost/iterator/iterator_facade.hpp>
#include <vector>
#include <stdexcept>

namespace libtorrent { namespace dht
{

class packet_iterator: public boost::iterator_facade<
	packet_iterator, const char, boost::forward_traversal_tag>
{
public:
	typedef std::vector<char>::const_iterator base_iterator;

	packet_iterator() {}
	
	packet_iterator(std::vector<char>::const_iterator start
		, std::vector<char>::const_iterator end
		, std::string const& error_msg = "")
		: m_base(start)
		, m_end(end)
		, m_msg(error_msg)
	{}

	base_iterator base() const
	{ return m_base; }
	
	base_iterator end() const
	{ return m_end; }

	int left() const { return int(m_end - m_base); }

private:
	friend class boost::iterator_core_access;

	bool equal(packet_iterator const& other) const
	{ return m_base == other.m_base; }

	void advance(int n)
	{
		m_base += n;
	}

	void increment()
	{ ++m_base; }

	char const& dereference() const
	{
		if (m_base == m_end) throw std::runtime_error(m_msg);
		return *m_base;
	}

	base_iterator m_base;
	base_iterator m_end;
	std::string m_msg;
};

} } // namespace libtorrent::dht

#endif // PACKET_ITERATOR_HPP

