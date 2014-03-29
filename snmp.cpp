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

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <libtorrent/session.hpp>
#include <libtorrent/thread.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/alert_observer.hpp>

#include "alert_handler.hpp"

using libtorrent::mutex;
using libtorrent::alert;
using libtorrent::session_stats_alert;
using libtorrent::alert_cast;
using libtorrent::alert_handler;

// TODO: could this be moved into snmp_interface?
// the callback function doesn't appear to have a userdata pointer...
mutex counter_mutex;
std::vector<boost::uint64_t> global_counters;

u_char* var_counter(struct variable *vp,
	oid* name, size_t* length, int exact, size_t* var_len
	, WriteMethod ** write_method)
{
	if (vp->namelen != 3) return NULL;

	int counter_index = (int(vp->name[0]) << 24) | (int(vp->name[1]) << 16) | int(vp->name[2]);

	static boost::uint32_t return_value;

	mutex::scoped_lock l(counter_mutex);
	if (counter_index < 0 || counter_index >= global_counters.size())
		return NULL;

	*var_len = sizeof(boost::uint32_t);
	// deliberately truncate to 32 bits. That's the SNMP integer size
	return_value = boost::uint32_t(global_counters[counter_index]);
	return (u_char*)return_value;
}


std::string write_mib()
{
	using libtorrent::stats_metric;

	std::vector<stats_metric> stats = libtorrent::session_stats_metrics();

	std::string ret = "LIBTORRENT-MIB DEFINITIONS ::= BEGIN\n"
		"IMPORTS\n"
		"\tOBJECT-TYPE FROM RFC-1212\n"
		"\tMODULE-IDENTITY FROM SNMPv2-SMI\n"
		"\tMODULE-COMPLIANCE, OBJECT-GROUP FROM SNMPv2-CONF;\n"
		"\tenterprises FROM RFC1155-SMI;\n"
		"libtorrent MODULE-IDENTITY\n"
		"\tLAST-UPDATED \"200205290000Z\"\n"
		"\tORGANIZATION \"rasterbar\"\n"
		"\tDESCRIPTION \"libtorrent performance counters and settings\""
		"\t::= { enterprises 1337 }\n"
		"performance_counters OBJECT IDENTIFIER ::= { libtorrent 1 }\n"
		"settings OBJECT IDENTIFIER ::= { libtorrent 2 }\n";

	for (std::vector<stats_metric>::iterator i = stats.begin()
		, end(stats.end()); i != end; ++i)
	{
		char buf[1024];
		snprintf(buf, sizeof(buf)
			, "%s OBJECT-TYPE\n"
			"\tSYNTAX INTEGER\n"
			"\tMAX-ACCESS read\n"
			"\tSTATUS current\n"
			"\tDESCRIPTION \"\"\n"
			"\tDEFVAL { 0 }\n"
			"\t::= { performance_counters %d }\n\n"
			, i->name, int(std::distance(stats.begin(), i)));

		ret += buf;
	}

	ret += "END\n";

	return ret;
}

bool quit = false;

void sighandler(int s)
{
	quit = true;
}

struct snmp_interface : libtorrent::alert_observer
{
	snmp_interface(libtorrent::alert_handler* h)
		: m_alerts(h)
	{
		m_alerts->subscribe(this, 0
			, session_stats_alert::alert_type, 0);

		using libtorrent::stats_metric;

		std::vector<stats_metric> stats = libtorrent::session_stats_metrics();

		// now we build up the mib entries based on the stats counters
		// libtorrent exports
		std::vector<variable2> mib_entries;

		for (std::vector<stats_metric>::iterator i = stats.begin()
			, end(stats.end()); i != end; ++i)
		{
			variable2 m;
			m.magic = 0;
			switch (i->type)
			{
				case stats_metric::type_counter:
					m.type = ASN_COUNTER;
					break;
				case stats_metric::type_gauge:
					m.type = ASN_GAUGE;
					break;
				default:
					continue;
			}

			// all performance counters are read-only
			m.acl = NETSNMP_OLDAPI_RONLY;

			// function to return the counter/gauge value
			m.findVar = &var_counter;

			// we store the counter index as a series of oids.
			// we don't need more than 24 bits.
			m.namelen = 2;

			m.name[0] = 1; // performance-counters
			m.name[1] = i->value_index;

			mib_entries.push_back(m);
		}

		oid libtorrent_oid_tree[] = { 1, 3, 6, 1, 4, 1, 1337 };

		if (register_mib("libtorrent"
				, (variable*)&mib_entries[0], sizeof(variable2)
				, mib_entries.size(), libtorrent_oid_tree
				, sizeof(libtorrent_oid_tree)/sizeof(oid)) != MIB_REGISTERED_OK)
		{
			fprintf(stderr, "Failed to register performance counter MIBs\n");
			return;
		}
	}

	~snmp_interface()
	{
		m_alerts->unsubscribe(this);
	}

	void handle_alert(alert const* a)
	{
		session_stats_alert const* su = alert_cast<session_stats_alert>(a);
		if (su == NULL) return;

		mutex::scoped_lock l(counter_mutex);
		global_counters.swap(const_cast<session_stats_alert*>(su)->values);
	}

private:

	libtorrent::alert_handler* m_alerts;
};

int main()
{
	libtorrent::session ses;

	FILE* f = fopen("test.mib", "w+");
	if (f)
	{
		fprintf(f, "%s", write_mib().c_str());
		fclose(f);
	}

	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	alert_handler alerts(ses);
	snmp_interface snmp(&alerts);

	while (!quit)
	{
		usleep(1000000);
		alerts.dispatch_alerts();
		ses.post_session_stats();
	}

}
