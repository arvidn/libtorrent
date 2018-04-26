/*

Copyright (c) 2006-2018, Arvid Norberg
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

#ifndef TORRENT_FILE_POOL_HPP
#define TORRENT_FILE_POOL_HPP

#include <map>
#include <mutex>
#include <vector>

#include "libtorrent/file.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/disk_interface.hpp" // for open_file_state

namespace libtorrent {

	class file_storage;
	struct open_file_state;

	// this is an internal cache of open file handles. It's primarily used by
	// storage_interface implementations. It provides semi weak guarantees of
	// not opening more file handles than specified. Given multiple threads,
	// each with the ability to lock a file handle (via smart pointer), there
	// may be windows where more file handles are open.
	struct TORRENT_EXPORT file_pool : boost::noncopyable
	{
		// ``size`` specifies the number of allowed files handles
		// to hold open at any given time.
		explicit file_pool(int size = 40);
		~file_pool();

		// return an open file handle to file at ``file_index`` in the
		// file_storage ``fs`` opened at save path ``p``. ``m`` is the
		// file open mode (see file::open_mode_t).
		file_handle open_file(storage_index_t st, std::string const& p
			, file_index_t file_index, file_storage const& fs, open_mode_t m
			, error_code& ec);
		// release all files belonging to the specified storage_interface (``st``)
		// the overload that takes ``file_index`` releases only the file with
		// that index in storage ``st``.
		void release();
		void release(storage_index_t st);
		void release(storage_index_t st, file_index_t file_index);

		// update the allowed number of open file handles to ``size``.
		void resize(int size);

		// returns the current limit of number of allowed open file handles held
		// by the file_pool.
		int size_limit() const { return m_size; }

		// internal
		void set_low_prio_io(bool b) { m_low_prio_io = b; }
		std::vector<open_file_state> get_status(storage_index_t st) const;

		// close the file that was opened least recently (i.e. not *accessed*
		// least recently). The purpose is to make the OS (really just windows)
		// clear and flush its disk cache associated with this file. We don't want
		// any file to stay open for too long, allowing the disk cache to accrue.
		void close_oldest();

	private:

		file_handle remove_oldest(std::unique_lock<std::mutex>&);

		int m_size;
		bool m_low_prio_io = false;

		struct lru_file_entry
		{
			file_handle file_ptr;
			time_point const opened{aux::time_now()};
			time_point last_use{opened};
			open_mode_t mode{};
		};

		// maps storage pointer, file index pairs to the
		// LRU entry for the file
		std::map<std::pair<storage_index_t, file_index_t>, lru_file_entry> m_files;
		mutable std::mutex m_mutex;
	};

}

#endif
