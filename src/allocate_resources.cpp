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
	namespace
	{
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


		// for use with std::sort
		bool by_used(const resource_request *a, const resource_request *b)
		{ return a->used < b->used; }

		// give num_resources to r,
		// return how how many were actually accepted.
		int give(resource_request *r, int num_resources)
		{
			assert(r);
			assert(num_resources > 0);
			assert(r->given <= r->wanted);
			
			int accepted=std::min(num_resources, r->wanted - r->given);
			assert(accepted >= 0);

			r->given += accepted;
			assert(r->given <= r->wanted);

			return accepted;
		}

		int total_wanted(std::vector<resource_request *> & requests)
		{
			int total_wanted=0;

			for(int i=0;i<(int)requests.size();i++)
			{
				total_wanted=
					saturated_add(total_wanted,requests[i]->wanted);
				
				if(total_wanted == std::numeric_limits<int>::max())
					break;
			}

			assert(total_wanted>=0);
			return total_wanted;
		}
	}

#ifndef NDEBUG
	class allocate_resources_contract_check
	{
		int resources;
		std::vector<resource_request *> & requests;
	public:
		allocate_resources_contract_check(int resources_,std::vector<resource_request *> & requests_)
			: resources(resources_)
			, requests(requests_)
		{
			assert(resources >= 0);
			for(int i=0;i<(int)requests.size();i++)
			{
				assert(requests[i]->used >= 0);
				assert(requests[i]->wanted >= 0);
				assert(requests[i]->given >= 0);
			}
		}

		~allocate_resources_contract_check()
		{
			int sum_given = 0;
			int sum_wanted = 0;
			for(int i=0;i<(int)requests.size();i++)
			{
				assert(requests[i]->used >= 0);
				assert(requests[i]->wanted >= 0);
				assert(requests[i]->given >= 0);
				assert(requests[i]->given <= requests[i]->wanted);

				sum_given = saturated_add(sum_given, requests[i]->given);
				sum_wanted = saturated_add(sum_wanted, requests[i]->wanted);
			}
			assert(sum_given == std::min(resources,sum_wanted));
		}
	};
#endif

	void allocate_resources(int resources,
		std::vector<resource_request *> & requests)
	{
#ifndef NDEBUG
		allocate_resources_contract_check
			contract_check(resources,requests);
#endif
		
		if(resources == std::numeric_limits<int>::max())
		{
			// No competition for resources.
			// Just give everyone what they want.
			for(int i=0;i<(int)requests.size();i++)
				requests[i]->given = requests[i]->wanted;
		}
		else
		{
			// Resources are scarce

			for(int i=0;i < (int)requests.size();i++)
				requests[i]->given = 0;

			if(resources == 0)
				return;

			int resources_to_distribute = 
				std::min(
					resources,
					total_wanted(requests));

			if (resources_to_distribute == 0)
				return;

			assert(resources_to_distribute>0);
				
			std::random_shuffle(requests.begin(),requests.end());
			std::sort(requests.begin(),requests.end(),by_used);

			while(resources_to_distribute > 0)
				for(int i = 0; i < (int)requests.size() && resources_to_distribute>0; i++)
					resources_to_distribute -=
						give(
							requests[i],
							std::min(
								requests[i]->used+1,
								round_up_division(
									(int)resources_to_distribute,
									(int)requests.size()-i)));
			assert(resources_to_distribute == 0);
		}
	}
}