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
#include "libtorrent/size_type.hpp"
#include <cassert>
#include <algorithm>
#include <boost/limits.hpp>

namespace libtorrent
{
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
/*
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
*/
		// give num_resources to r,
		// return how how many were actually accepted.
		int give(resource_request *r, int num_resources)
		{
			assert(r);
			assert(num_resources >= 0);
			assert(r->given <= r->wanted);
			
			int accepted = std::min(num_resources, r->wanted - r->given);
			assert(accepted >= 0);

			r->given += accepted;
			assert(r->given <= r->wanted);

			return accepted;
		}

		// sum of requests' "wanted" field.
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

#ifndef NDEBUG

		class allocate_resources_contract_check
		{

			int resources;
			std::vector<resource_request *> & requests;

		public:

			allocate_resources_contract_check(
				int resources_
				, std::vector<resource_request *>& requests_)
				: resources(resources_)
				, requests(requests_)
			{
				assert(resources >= 0);
				for (int i = 0; i < (int)requests.size(); ++i)
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
				for (int i = 0; i < (int)requests.size(); ++i)
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
	} // namespace unnamed

	void allocate_resources(int resources
		, std::vector<resource_request *>& requests)
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

			for (int i = 0; i < (int)requests.size(); ++i)
				requests[i]->given = 0;

			if (resources == 0)
				return;

			int resources_to_distribute = 
				std::min(
					resources,
					total_wanted(requests));

			if (resources_to_distribute == 0)
				return;

			assert(resources_to_distribute > 0);

			while(resources_to_distribute > 0)
			{
#if 0
				int num_active=0;
				for(int i = 0;i < (int)requests.size();++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;
					num_active++;
				}

				int max_give=resources_to_distribute/num_active;
				max_give=std::max(max_give,1);
				
				for(int i = 0;i < (int)requests.size() && resources_to_distribute;++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;

					int toGive = 1+std::min(max_give-1,r->used);
					resources_to_distribute-=give(r,toGive);
				}
#elif 1
				size_type total_used=0;
				size_type max_used=0;
				for(int i = 0;i < (int)requests.size();++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;
					assert(r->given < r->wanted);

					max_used = std::max(max_used, (size_type)r->used + 1);
					total_used += (size_type)r->used + 1;
				}

				size_type kNumer=resources_to_distribute;
				size_type kDenom=total_used;

				{
					size_type numer=1;
					size_type denom=max_used;

					if(numer*kDenom >= kNumer*denom)
					{
						kNumer=numer;
						kDenom=denom;
					}
				}

				for(int i = 0;i < (int)requests.size();++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;
					assert(r->given < r->wanted);

					size_type numer = r->wanted - r->given;
					size_type denom = (size_type)r->used + 1;

					if(numer*kDenom <= kNumer*denom)
					{
						kNumer=numer;
						kDenom=denom;
					}
				}

				for(int i = 0;i < (int)requests.size() && resources_to_distribute;++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;
					assert(r->given < r->wanted);

					size_type used = (size_type)r->used + 1;
					size_type toGive = (used * kNumer) / kDenom;
					if(toGive>std::numeric_limits<int>::max())
						toGive=std::numeric_limits<int>::max();
					resources_to_distribute-=give(r,(int)toGive);
				}
#else
				size_type total_used=0;
				size_type max_used=0;
				for(int i = 0;i < (int)requests.size();++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;
					assert(r->given < r->wanted);

					max_used = std::max(max_used, (size_type)r->used + 1);
					total_used += (size_type)r->used + 1;
				}

				size_type kNumer=resources_to_distribute;
				size_type kDenom=total_used;

				if(kNumer*max_used <= kDenom)
				{
					kNumer=1;
					kDenom=max_used;
				}

				if(kNumer > kDenom)
				{
					kNumer=1;
					kDenom=1;
				}

				for(int i = 0;i < (int)requests.size() && resources_to_distribute;++i)
				{
					resource_request *r=requests[i];
					if(r->given == r->wanted)
						continue;
					assert(r->given < r->wanted);

					size_type used = (size_type)r->used + 1;
					size_type toGive = used * kNumer / kDenom;
					if(toGive>std::numeric_limits<int>::max())
						toGive=std::numeric_limits<int>::max();
					resources_to_distribute-=give(r,(int)toGive);
				}
/*
				while(resources_to_distribute != 0)
				{
					int num_active=0;
					for(int i = 0;i < (int)requests.size();++i)
					{
						resource_request *r=requests[i];
						if(r->given == r->wanted)
							continue;
						num_active++;
					}

					int max_give=resources_to_distribute/num_active;
					max_give=std::max(max_give,1);
					
					for(int i = 0;i < (int)requests.size() && resources_to_distribute;++i)
					{
						resource_request *r=requests[i];
						if(r->given == r->wanted)
							continue;

						int toGive = 1+std::min(max_give-1,r->used);
						resources_to_distribute-=give(r,toGive);
					}
				}
*/
#endif
				assert(resources_to_distribute >= 0);
			}
			assert(resources_to_distribute == 0);

		}
	}
}