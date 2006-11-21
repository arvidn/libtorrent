/*

Copyright (c) 2006, Magnus Jonsson, Arvid Norberg
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

//The Standard Library defines the two template functions std::min() 
//and std::max() in the <algorithm> header. In general, you should 
//use these template functions for calculating the min and max values 
//of a pair. Unfortunately, Visual C++ does not define these function
// templates. This is because the names min and max clash with 
//the traditional min and max macros defined in <windows.h>. 
//As a workaround, Visual C++ defines two alternative templates with 
//identical functionality called _cpp_min() and _cpp_max(). You can 
//use them instead of std::min() and std::max().To disable the 
//generation of the min and max macros in Visual C++, #define 
//NOMINMAX before #including <windows.h>.

#ifdef _WIN32
    //support boost1.32.0(2004-11-19 18:47)
    //now all libs can be compiled and linked with static module
	#define NOMINMAX
#endif

#include "libtorrent/allocate_resources.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/aux_/allocate_resources_impl.hpp"

#include <cassert>
#include <algorithm>
#include <boost/limits.hpp>

#if defined(_MSC_VER) && _MSC_VER < 1310
#define for if (false) {} else for
#else
#include <boost/iterator/transform_iterator.hpp>
#endif

namespace libtorrent
{
	int saturated_add(int a, int b)
	{
		assert(a >= 0);
		assert(b >= 0);
		assert(a <= resource_request::inf);
		assert(b <= resource_request::inf);
		assert(resource_request::inf + resource_request::inf < 0);

		unsigned int sum = unsigned(a) + unsigned(b);
		if (sum > unsigned(resource_request::inf))
			sum = resource_request::inf;

		assert(sum >= unsigned(a) && sum >= unsigned(b));
		return int(sum);
	}

#if defined(_MSC_VER) && _MSC_VER < 1310

	namespace detail
	{
		struct iterator_wrapper
		{
			typedef std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator orig_iter;

			orig_iter iter;

			iterator_wrapper(orig_iter i): iter(i) {}
			void operator++() { ++iter; }
			torrent& operator*() { return *(iter->second); }
			bool operator==(const iterator_wrapper& i) const
			{ return iter == i.iter; }
			bool operator!=(const iterator_wrapper& i) const
			{ return iter != i.iter; }
		};

		struct iterator_wrapper2
		{
			typedef std::map<tcp::endpoint, peer_connection*>::iterator orig_iter;

			orig_iter iter;

			iterator_wrapper2(orig_iter i): iter(i) {}
			void operator++() { ++iter; }
			peer_connection& operator*() { return *(iter->second); }
			bool operator==(const iterator_wrapper2& i) const
			{ return iter == i.iter; }
			bool operator!=(const iterator_wrapper2& i) const
			{ return iter != i.iter; }
		};

	}

	void allocate_resources(
		int resources
		, std::map<sha1_hash, boost::shared_ptr<torrent> >& c
		, resource_request torrent::* res)
	{
		aux::allocate_resources_impl(
			resources
			, detail::iterator_wrapper(c.begin())
			, detail::iterator_wrapper(c.end())
			, res);
	}

	void allocate_resources(
		int resources
		, std::map<tcp::endpoint, peer_connection*>& c
		, resource_request peer_connection::* res)
	{
		aux::allocate_resources_impl(
			resources
			, detail::iterator_wrapper2(c.begin())
			, detail::iterator_wrapper2(c.end())
			, res);
	}

#else

	namespace aux
	{
		peer_connection& pick_peer(
			std::pair<boost::shared_ptr<stream_socket>
			, boost::intrusive_ptr<peer_connection> > const& p)
		{
			return *p.second;
		}

		peer_connection& pick_peer2(
			std::pair<tcp::endpoint, peer_connection*> const& p)
		{
			return *p.second;
		}

		torrent& deref(std::pair<sha1_hash, boost::shared_ptr<torrent> > const& p)
		{
			return *p.second;
		}

		session& deref(session* p)
		{
			return *p;
		}
	}

	void allocate_resources(
		int resources
		, std::map<sha1_hash, boost::shared_ptr<torrent> >& c
		, resource_request torrent::* res)
	{
		typedef std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator orig_iter;
		typedef std::pair<sha1_hash, boost::shared_ptr<torrent> > in_param;
		typedef boost::transform_iterator<torrent& (*)(in_param const&), orig_iter> new_iter;

		aux::allocate_resources_impl(
			resources
			, new_iter(c.begin(), &aux::deref)
			, new_iter(c.end(), &aux::deref)
			, res);
	}

	void allocate_resources(
		int resources
		, std::map<tcp::endpoint, peer_connection*>& c
		, resource_request peer_connection::* res)
	{
		typedef std::map<tcp::endpoint, peer_connection*>::iterator orig_iter;
		typedef std::pair<tcp::endpoint, peer_connection*> in_param;
		typedef boost::transform_iterator<peer_connection& (*)(in_param const&), orig_iter> new_iter;

		aux::allocate_resources_impl(
			resources
			, new_iter(c.begin(), &aux::pick_peer2)
			, new_iter(c.end(), &aux::pick_peer2)
			, res);
	}

	void allocate_resources(
		int resources
		, std::vector<session*>& _sessions
		, resource_request session::* res)
	{
		typedef std::vector<session*>::iterator orig_iter;
		typedef session* in_param;
		typedef boost::transform_iterator<session& (*)(in_param), orig_iter> new_iter;

		aux::allocate_resources_impl(
			resources
			, new_iter(_sessions.begin(), &aux::deref)
			, new_iter(_sessions.end(), &aux::deref)
			, res);
	}

#endif

} // namespace libtorrent
