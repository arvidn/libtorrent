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
	struct resource_request {
		resource_request() : used(0), wanted(0), given(0) { }

		// I'm right now actively using:
		int used;

		// I would like to use this much:
		int wanted;

		// Reply: Okay, you're allowed to use this much (a compromise):
		int given;
	};

	// Function to allocate a limited resource fairly among many consumers.
	// It takes into account the current use, and the consumer's desired use.
	// Should be invoked periodically to allow it adjust to the situation (make
	// sure "used" is updated between calls!).
	
	void allocate_resources(int resources,
		std::vector<resource_request *> & requests);
}


#endif
