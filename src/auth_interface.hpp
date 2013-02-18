/*

Copyright (c) 2013, Arvid Norberg
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

#ifndef TORRENT_PERM_INTERFACE_HPP
#define TORRENT_PERM_INTERFACE_HPP

#include <string>

namespace libtorrent
{
	struct permissions_interface
	{
		virtual bool allow_start() const = 0;
		virtual bool allow_stop() const = 0;
		virtual bool allow_recheck() const = 0;
		virtual bool allow_list() const = 0;
		virtual bool allow_add() const = 0;
		virtual bool allow_remove() const = 0;
		virtual bool allow_remove_data() const = 0;
		virtual bool allow_queue_change() const = 0;
		virtual bool allow_get_settings() const = 0;
		virtual bool allow_set_settings() const = 0;
		virtual bool allow_get_data() const = 0;
	};

	struct auth_interface
	{
		// returns the persmissions object for the specified
		// account, or NULL in case the username doesn't exist
		// or if the password is incorrect
		virtual permissions_interface const* find_user(std::string username, std::string password) const = 0;
	};

}

#endif // TORRENT_PERM_INTERFACE_HPP

