/*

Copyright (c) 2010, Arvid Norberg
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

#include <stdlib.h>
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/storage_defs.hpp"

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	if (argc != 5)
	{
		fputs("usage: ./connection_tester torrent-file IP port num-connections\n"
			"to stop the client, press return.\n", stderr);
		return 1;
	}

	tcp::endpoint ip(address::from_string(argv[2]), atoi(argv[3]));
	int num_connections = atoi(argv[4]);

	std::list<session*> ses_list;

	add_torrent_params p;
	p.save_path = "./";
	error_code ec;
	p.ti = new torrent_info(argv[1], ec);
	p.storage = &disabled_storage_constructor;

	if (ec)
	{
		fprintf(stderr, "%s\n", ec.message().c_str());
		return 1;
	}

	fprintf(stderr, "starting %d connections\n", num_connections);
	for (int i = 0; i < num_connections; ++i)
	{
		session* s = new session(fingerprint("LT", 0, 0, 0, 0), 0);
		s->listen_on(std::make_pair(2000 + i*5, 200 + i*5 + 4));

		session_settings set;
		set.disable_hash_checks = true;

		s->set_settings(set);
		torrent_handle h = s->add_torrent(p, ec);

		if (ec)
		{
			fprintf(stderr, "%s\n", ec.message().c_str());
			return 1;
		}
		h.connect_peer(ip);
		ses_list.push_back(s);
	}

	// wait for the user to end
	char a;
	scanf("%c", &a);
	fprintf(stderr, "shutting down\n");

	// shut down all sessions in parallel
	std::list<session_proxy> shutdown;
	for (std::list<session*>::iterator i = ses_list.begin(),
		end(ses_list.end()); i != end; ++i)
		shutdown.push_back((*i)->abort());

	// wait for all session to complete shutdown
	for (std::list<session*>::iterator i = ses_list.begin(),
		end(ses_list.end()); i != end; ++i)
		delete *i;

	return 0;
}

