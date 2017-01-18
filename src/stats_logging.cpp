/*

Copyright (c) 2014, Arvid Norberg
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

#include <functional>

#include "stats_logging.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/file.hpp"
#include "alert_handler.hpp"
#include "libtorrent/session_stats.hpp"

using namespace libtorrent;
using namespace std::placeholders;

/*

#if defined TORRENT_STATS && defined __MACH__
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#endif

#if !defined __MACH__
	struct vm_statistics_data_t
	{
		std::uint64_t active_count;
		std::uint64_t inactive_count;
		std::uint64_t wire_count;
		std::uint64_t free_count;
		std::uint64_t pageins;
		std::uint64_t pageouts;
		std::uint64_t faults;
	};
#endif

	struct thread_cpu_usage
	{
		libtorrent::time_duration user_time;
		libtorrent::time_duration system_time;
	};

	libtorrent::counters m_last_stats_counters;

	libtorrent::cache_status m_last_cache_status;
	vm_statistics_data_t m_last_vm_stat;
	thread_cpu_usage m_network_thread_cpu_usage;
*/

stats_logging::stats_logging(session& s, alert_handler* h)
	: m_alerts(h)
	, m_ses(s)
	, m_stats_logger(NULL)
	, m_log_seq(0)
{
	m_alerts->subscribe(this, 0
		, session_stats_alert::alert_type
		, stats_alert::alert_type
		, 0);
	rotate_stats_log();
}

stats_logging::~stats_logging()
{
	m_alerts->unsubscribe(this);
}

void stats_logging::rotate_stats_log()
{
	if (m_stats_logger)
	{
		++m_log_seq;
		fclose(m_stats_logger);
	}

	error_code ec;
	char filename[100];
	create_directory("session_stats", ec);
#ifdef TORRENT_WINDOWS
	const int pid = GetCurrentProcessId();
#else
	const int pid = getpid();
#endif
	snprintf(filename, sizeof(filename), "session_stats/%d.%04d.log", pid, m_log_seq);
	m_stats_logger = fopen(filename, "w+");
	m_last_log_rotation = time_now();
	if (m_stats_logger == 0)
	{
		fprintf(stderr, "Failed to create session stats log file \"%s\": (%d) %s\n"
			, filename, errno, strerror(errno));
		return;
	}

	std::vector<stats_metric> cnts = session_stats_metrics();
	std::sort(cnts.begin(), cnts.end()
		, [](stats_metric const& lhs, stats_metric const& rhs)
		{ return lhs.value_index < rhs.value_index; });

	int idx = 0;
	fputs("second", m_stats_logger);
	for (int i = 0; i < cnts.size(); ++i)
	{
		// just in case there are some indices that don't have names
		// (it shouldn't really happen)
		while (idx < cnts[i].value_index)
		{
			fprintf(m_stats_logger, ":");
			++idx;
		}

		fprintf(m_stats_logger, ":%s", cnts[i].name);
		++idx;
	}
	fputs("\n\n", m_stats_logger);
}

void stats_logging::handle_alert(alert const* a)
{
	session_stats_alert const* s = alert_cast<session_stats_alert>(a);

	if (s == NULL)
	{
		m_ses.post_session_stats();
		return;
	}

	if (time_now_hires() - m_last_log_rotation > hours(1))
		rotate_stats_log();

	fprintf(m_stats_logger, "%f", double(total_microseconds(s->timestamp() - m_last_log_rotation)) / 1000000.0);
	for (int i = 0; i < counters::num_counters; ++i)
	{
		fprintf(m_stats_logger, "\t%" PRId64, s->values[i]);
	}
	fprintf(m_stats_logger, "\n");
}

