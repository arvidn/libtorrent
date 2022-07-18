/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef DEBUG_DISK_THREAD_HPP
#define DEBUG_DISK_THREAD_HPP

#ifndef DEBUG_DISK_THREAD
#define DEBUG_DISK_THREAD 0
#endif

#if !DEBUG_DISK_THREAD
#define DLOG(...) do {} while(false)
#else

#include <cstdarg> // for va_list
#include <sstream>
#include <cstdio> // for vsnprintf
#include <string>
#include <sstream>
#include <unordered_map>
#include <thread>

#include "libtorrent/aux_/disk_job.hpp"
#include "libtorrent/disk_interface.hpp"

#define DLOG(...) debug_log(__VA_ARGS__)

namespace libtorrent {

inline std::string print_job(aux::disk_job const& j)
{
	namespace job = libtorrent::aux::job;

	struct print_visitor
	{
		explicit print_visitor(std::stringstream& s) : m_ss(s) {}

		void operator()(job::read const& j) const {
			m_ss << "read ( size: " << j.buffer_size << " piece: " << j.piece << " offset: " << j.offset << " )";
		}

		void operator()(job::write const& j) const {
			m_ss << "write( size: " << j.buffer_size << " piece: " << j.piece << " offset: " << j.offset << " )";
		}

		void operator()(job::hash const& j) const {
			m_ss << "hash( piece: " << j.piece << " )";
		}

		void operator()(job::hash2 const& j) const {
			m_ss << "hash( piece: " << j.piece << " offset: " << j.offset << " )";
		}

		void operator()(job::move_storage const& j) const {
			m_ss << "move-storage( path: " << j.path << " flags: " << int(j.move_flags) << " )";
		}

		void operator()(job::release_files const&) const {
			m_ss << "move-storage( )";
		}

		void operator()(job::delete_files const& j) const {
			m_ss << "delete-files ( flags: " << j.flags << " )";
		}

		void operator()(job::check_fastresume const&) const {
			m_ss << "check-fastresume( )";
		}

		void operator()(job::rename_file const& j) const {
			m_ss << "rename-file( file: " << j.file_index << " name: " << j.name << " )";
		}

		void operator()(job::stop_torrent const&) const {
			m_ss << "stop-torrent( )";
		}

		void operator()(job::file_priority const& j) const {
			m_ss << "file-priority( num-files: " << j.prio.size() << " )";
		}

		void operator()(job::clear_piece const& j) const {
			m_ss << "clear-piece( piece: " << j.piece << " )";
		}

		void operator()(job::partial_read const& j) const {
			m_ss << "partial-read( piece: " << j.piece << " offset: " << j.offset
				<< " buf-offset: " << j.buffer_offset << " size: " << j.buffer_size << " )";
		}

		void operator()(job::kick_hasher const& j) const {
			m_ss << "kick-hasher( piece: " << j.piece << " )";
		}

	private:
		std::stringstream& m_ss;
	};

	std::stringstream ss;
	std::visit(print_visitor(ss), j.action);
	if (j.flags & aux::disk_job::fence) ss << "fence ";
	if (j.flags & disk_interface::force_copy) ss << "force_copy ";
	return ss.str();
}

namespace {

inline void debug_log(char const* fmt, ...)
{
	static std::mutex log_mutex;
	static const time_point start = clock_type::now();
	// map thread IDs to low numbers
	static std::unordered_map<std::thread::id, int> thread_ids;

	std::thread::id const self = std::this_thread::get_id();

	std::unique_lock<std::mutex> l(log_mutex);
	auto it = thread_ids.insert({self, int(thread_ids.size())}).first;

	va_list v;
	va_start(v, fmt);

	char usr[2048];
	int len = std::vsnprintf(usr, sizeof(usr), fmt, v);

	static bool prepend_time = true;
	if (!prepend_time)
	{
		prepend_time = (usr[len-1] == '\n');
		fputs(usr, stderr);
		return;
	}
	va_end(v);
	char buf[2300];
	int const t = int(total_milliseconds(clock_type::now() - start));
	std::snprintf(buf, sizeof(buf), "\x1b[3%dm%05d: [%d] %s\x1b[0m"
		, (it->second % 7) + 1, t, it->second, usr);
	prepend_time = (usr[len-1] == '\n');
	fputs(buf, stderr);
}

}
}
#endif // DEBUG_DISK_THREAD

#endif
