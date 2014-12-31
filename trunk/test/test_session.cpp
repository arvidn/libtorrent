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

#include "libtorrent/session.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/alert_types.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

int test_main()
{
	settings_pack p;
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	settings_pack sett;
	sett.set_int(settings_pack::cache_size, 100);
	sett.set_int(settings_pack::max_queued_disk_bytes, 1000 * 16 * 1024);

	ses.apply_settings(sett);

	// verify that we get the appropriate performance warning because
	// we're allowing a larger queue than we have cache.


	std::auto_ptr<alert> a;
	for (;;)
	{
		a = wait_for_alert(ses, performance_alert::alert_type, "ses1");

		if (a.get() == NULL) break;
		TEST_EQUAL(a.get()->type(), performance_alert::alert_type);

		if (alert_cast<performance_alert>(a.get())->warning_code
			== performance_alert::too_high_disk_queue_limit)
			break;
	}

	TEST_CHECK(a.get());

	sett.set_int(settings_pack::unchoke_slots_limit, 0);
	ses.apply_settings(sett);
	TEST_CHECK(ses.get_settings().get_int(settings_pack::unchoke_slots_limit) == 0);

	sett.set_int(settings_pack::unchoke_slots_limit, -1);
	ses.apply_settings(sett);
	TEST_CHECK(ses.get_settings().get_int(settings_pack::unchoke_slots_limit) == -1);

	sett.set_int(settings_pack::unchoke_slots_limit, 8);
	ses.apply_settings(sett);
	TEST_CHECK(ses.get_settings().get_int(settings_pack::unchoke_slots_limit) == 8);

	// make sure the destructor waits properly
	// for the asynchronous call to set the alert
	// mask completes, before it goes on to destruct
	// the session object
	return 0;
}

