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

#ifndef TORRENT_ALLOCATE_RESOURCES_HPP_INCLUDED
#define TORRENT_ALLOCATE_RESOURCES_HPP_INCLUDED

#include <vector>
#include <boost/any.hpp>

namespace libtorrent
{
	struct resource_consumer;


	// Function to allocate a limited resource fairly among many consumers.
	// It takes into account the current use, and the consumer's desired use.
	// Should be invoked periodically to allow it adjust to the situation.
	
	void allocate_resources(int resources,
		std::vector<resource_consumer> & consumers);

	// information needed by allocate_resources about each client.
	struct resource_consumer
	{
		resource_consumer(
			boost::any who,	// who is this info about?
			int desired_use, // the max that the consumer is willing/able to use
			int current_use // how many resources does it use right now?
			); 
			
		// who/what is this info about?
		boost::any const &who() const { return m_who; }

		// after the allocation process, this is the resulting
		// number of resources that this consumer is allowed to
		// use up. If it's currently using up more resources it
		// must free up resources accordingly.
		int allowed_use() const { return m_allowed_use; };

		// how many resources does it use right now?
		int current_use() const { return m_current_use; }

		// how many resources does it desire to use?
		// - the max that the consumer is willing/able to use
		int desired_use() const { return m_desired_use; }

		// give allowance to use num_resources more resources
		// than currently allowed. returns how many the consumer
		// accepts. used internally by allocate_resources.
		int give(int num_resources);

	private:
		boost::any m_who;
		int m_current_use;
		int m_desired_use;
		int m_allowed_use;
	};
}


#endif
