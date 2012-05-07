/*

Copyright (c) 2012, Arvid Norberg
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

#include "libtorrent/block_cache.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/hash_thread.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/alert_dispatcher.hpp"

using namespace libtorrent;

struct print_alert : alert_dispatcher
{
	virtual bool post_alert(alert* a)
	{
		fprintf(stderr, "ALERT: %s\n", a->message().c_str());
		delete a;
		return true;
	}
};

struct dummy_hash_thread : hash_thread_interface
{
	virtual bool async_hash(cached_piece_entry* p, int start, int end)
	{
		return false;
	}
};

int test_main()
{
	io_service ios;
	dummy_hash_thread h;
	print_alert ad;
	block_cache bc(0x4000, h, ios, &ad);

	disk_io_job j;
	j.piece = 0;
//	j.storage = ...
//	cached_piece_entry* p = bc.allocate_piece(&j, 0);
	return 0;
}

