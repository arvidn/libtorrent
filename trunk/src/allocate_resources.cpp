/*

Copyright (c) 2003, Magnus Jonsson, Arvid Norberg
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

#include "libtorrent/allocate_resources.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/peer_connection.hpp"

#include <cassert>
#include <algorithm>
#include <boost/limits.hpp>
#include <boost/iterator/transform_iterator.hpp>

#if defined(_MSC_VER) && _MSC_VER < 1300
#define for if (false) {} else for
#endif

namespace libtorrent
{
	namespace
	{
		int saturated_add(int a, int b)
		{
			assert(a >= 0);
			assert(b >= 0);
			assert(std::numeric_limits<int>::max() + std::numeric_limits<int>::max() < 0);

			int sum = a + b;
			if(sum < 0)
				sum = std::numeric_limits<int>::max();

			assert(sum >= a && sum >= b);
			return sum;
		}

		// give num_resources to r,
		// return how how many were actually accepted.
		int give(resource_request& r, int num_resources)
		{
			assert(num_resources >= 0);
			assert(r.given <= r.wanted);
			
			int accepted = std::min(num_resources, r.wanted - r.given);
			assert(accepted >= 0);

			r.given += accepted;
			assert(r.given <= r.wanted);

			return accepted;
		}

#ifndef NDEBUG

		template<class It, class T>
		class allocate_resources_contract_check
		{
			int m_resources;
			It m_start;
			It m_end;
			resource_request T::* m_res;

		public:
			allocate_resources_contract_check(
				int resources
				, It start
				, It end
				, resource_request T::* res)
				: m_resources(resources)
				, m_start(start)
				, m_end(end)
				, m_res(res)
			{
				assert(m_resources >= 0);
				for (It i = m_start, end(m_end); i != end; ++i)
				{
					assert(((*i).*m_res).used >= 0);
					assert(((*i).*m_res).wanted >= 0);
					assert(((*i).*m_res).given >= 0);
				}
			}

			~allocate_resources_contract_check()
			{
				int sum_given = 0;
				int sum_wanted = 0;
				for (It i = m_start, end(m_end); i != end; ++i)
				{
					assert(((*i).*m_res).wanted >= 0);
					assert(((*i).*m_res).given >= 0);
					assert(((*i).*m_res).given <= ((*i).*m_res).wanted);

					sum_given = saturated_add(sum_given, ((*i).*m_res).given);
					sum_wanted = saturated_add(sum_wanted, ((*i).*m_res).wanted);
				}
				assert(sum_given == std::min(m_resources, sum_wanted));
			}
		};

#endif

		template<class It, class T>
		void allocate_resources_impl(
			int resources
			, It start
			, It end
			, resource_request T::* res)
		{
	#ifndef NDEBUG
			allocate_resources_contract_check<It, T> contract_check(
				resources
				, start
				, end
				, res);
	#endif

			if(resources == std::numeric_limits<int>::max())
			{
				// No competition for resources.
				// Just give everyone what they want.
				for (It i = start; i != end; ++i)
				{
					((*i).*res).given = ((*i).*res).wanted;
				}
				return;
			}

			// Resources are scarce

			int total_wanted = 0;
			for (It i = start; i != end; ++i)
			{
				((*i).*res).given = 0;
				total_wanted = saturated_add(total_wanted, ((*i).*res).wanted);
			}

			if (resources == 0 || total_wanted == 0)
				return;

			int resources_to_distribute = std::min(resources, total_wanted);
			assert(resources_to_distribute > 0);

			while (resources_to_distribute > 0)
			{
				size_type total_used = 0;
				size_type max_used = 0;
				for (It i = start; i != end; ++i)
				{
					resource_request& r = (*i).*res;
					if(r.given == r.wanted) continue;

					assert(r.given < r.wanted);

					max_used = std::max(max_used, (size_type)r.used + 1);
					total_used += (size_type)r.used + 1;
				}

				size_type kNumer = resources_to_distribute;
				size_type kDenom = total_used;

				if (kNumer * max_used <= kDenom)
				{
					kNumer = 1;
					kDenom = max_used;
				}

				for (It i = start; i != end && resources_to_distribute > 0; ++i)
				{
					resource_request& r = (*i).*res;
					if(r.given == r.wanted) continue;

					assert(r.given < r.wanted);

					size_type used = (size_type)r.used + 1;
					size_type toGive = used * kNumer / kDenom;
					if(toGive > std::numeric_limits<int>::max())
						toGive = std::numeric_limits<int>::max();
					resources_to_distribute -= give(r, (int)toGive);
				}

				assert(resources_to_distribute >= 0);
			}
		}

		peer_connection& pick_peer(
			std::pair<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> > const& p)
		{
			return *p.second;
		}

	} // namespace anonymous


	void allocate_resources(
		int resources
		, std::map<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> >& c
		, resource_request peer_connection::* res)
	{
		typedef std::map<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> >::iterator orig_iter;
		typedef std::pair<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> > in_param;
		typedef boost::transform_iterator<peer_connection& (*)(in_param const&), orig_iter> new_iter;

		allocate_resources_impl(
			resources
			, new_iter(c.begin(), &pick_peer)
			, new_iter(c.end(), &pick_peer)
			, res);
	}

} // namespace libtorrent
