/*

Copyright (c) 2003, Magnus Jonsson
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
#include <cassert>
#include <algorithm>
#include <boost/limits.hpp>

namespace libtorrent {
	
	resource_consumer::resource_consumer(boost::any who, int desired_use, int current_use)
		: m_who(who)
		, m_desired_use(desired_use)
		, m_current_use(current_use)
		, m_allowed_use(0)
	{
		assert(desired_use>=0);
		assert(current_use>=0);
	}

	int resource_consumer::give(int num_resources)
	{
		assert(num_resources>0);
		
		int accepted_resources=std::min(num_resources, m_desired_use-m_allowed_use);
		assert(accepted_resources>=0);
		
		m_allowed_use+=accepted_resources;
		assert(m_allowed_use<=m_desired_use);
		
		return accepted_resources;
	}

	namespace
	{
		bool by_desired_use(
			const resource_consumer &a,
			const resource_consumer &b)
		{
			return a.desired_use() < b.desired_use();
		}
	
		int saturated_add(int a, int b)
		{
			assert(a>=0);
			assert(b>=0);

			int sum=a+b;
			if(sum<0)
				sum=std::numeric_limits<int>::max();

			assert(sum>=a && sum>=b);
			return sum;
		}

		int round_up_division(int numer, int denom)
		{
			assert(numer>0);
			assert(denom>0);
			int result=(numer+denom-1)/denom;
			assert(result>0);
			assert(result<=numer);
			return result;
		}

		int total_demand(std::vector<resource_consumer> & consumers)
		{
			int total_demand=0;

			for(int i=0;i<(int)consumers.size();i++)
			{
				total_demand=saturated_add(total_demand, consumers[i].desired_use());
				if(total_demand == std::numeric_limits<int>::max())
					break;
			}

			assert(total_demand>=0);
			return total_demand;
		}
	}

	void allocate_resources(int resources,
		std::vector<resource_consumer> & consumers)
	{
		assert(resources>=0);
		
		// no competition for resources?
		if(resources==std::numeric_limits<int>::max())
		{
			for(int i=0;i<(int)consumers.size();i++)
				consumers[i].give(std::numeric_limits<int>::max());
		}
		else
		{
			int resources_to_distribute = 
				std::min(
					resources,
					total_demand(consumers));

			if (resources_to_distribute != 0)
			{
				assert(resources_to_distribute>0);
				
				std::random_shuffle(consumers.begin(),consumers.end());
				std::sort(consumers.begin(),consumers.end(),by_desired_use);

				while(resources_to_distribute > 0)
					for(int i = 0; i < (int)consumers.size() && resources_to_distribute>0; i++)
						resources_to_distribute -=
							consumers[i].give(
								std::min(
									round_up_division(
										(int)resources_to_distribute,
										(int)consumers.size()),
									std::min(
										consumers[i].current_use()*2+1, // allow for fast growth
										consumers[i].desired_use())));
			}
			assert(resources_to_distribute == 0);
		}

#ifndef NDEBUG
		{
			int sum_given = 0;
			int sum_desired = 0;
			for (std::vector<resource_consumer>::iterator i = consumers.begin();
				i != consumers.end();
				++i)
			{
				assert(i->allowed_use() <= i->desired_use());

				sum_given = saturated_add(sum_given, i->allowed_use());
				sum_desired = saturated_add(sum_desired, i->desired_use());
			}
			assert(sum_given == std::min(resources,sum_desired));
		}
#endif
	}

}