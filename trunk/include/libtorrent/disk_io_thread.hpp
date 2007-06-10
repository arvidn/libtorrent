/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/storage.hpp"
#include <boost/thread/thread.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <boost/pool/pool.hpp>
#include <boost/noncopyable.hpp>
#include "libtorrent/config.hpp"

namespace libtorrent
{

	struct disk_io_job
	{
		disk_io_job()
			: action(read)
			, buffer(0)
			, buffer_size(0)
			, piece(0)
			, offset(0)
		{}

		enum action_t
		{
			read
			, write
			, hash
			, move_storage
			, release_files
		};

		action_t action;

		char* buffer;
		size_type buffer_size;
		boost::intrusive_ptr<piece_manager> storage;
		// arguments used for read and write
		int piece, offset;
		// used for move_storage. On errors, this is set
		// to the error message
		std::string str;

		// this is called when operation completes
		boost::function<void(int, disk_io_job const&)> callback;
	};

	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct disk_io_thread : boost::noncopyable
	{
		disk_io_thread(int block_size = 16 * 1024);
		~disk_io_thread();

		// aborts read operations
		void stop(boost::intrusive_ptr<piece_manager> s);
		void add_job(disk_io_job const& j
			, boost::function<void(int, disk_io_job const&)> const& f
			= boost::function<void(int, disk_io_job const&)>());

		// keep track of the number of bytes in the job queue
		// at any given time. i.e. the sum of all buffer_size.
		// this is used to slow down the download global download
		// speed when the queue buffer size is too big.
		size_type queue_buffer_size() const
		{ return m_queue_buffer_size; }

		void operator()();

		char* allocate_buffer();

	private:

		boost::mutex m_mutex;
		boost::condition m_signal;
		bool m_abort;
		std::deque<disk_io_job> m_jobs;
		size_type m_queue_buffer_size;

		// memory pool for read and write operations
		boost::pool<> m_pool;

		// thread for performing blocking disk io operations
		boost::thread m_disk_io_thread;
	};

}

