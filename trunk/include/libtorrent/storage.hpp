/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_STORAGE_HPP_INCLUDE
#define TORRENT_STORAGE_HPP_INCLUDE

#include <vector>

#include <boost/limits.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/opaque_value_ptr.hpp"

namespace libtorrent
{
	namespace detail
	{
		struct piece_checker_data;
	}
	class session;

	struct file_allocation_failed: std::exception
	{
		file_allocation_failed(const char* error_msg): m_msg(error_msg) {}
		virtual const char* what() const throw() { return m_msg.c_str(); }
		virtual ~file_allocation_failed() throw() {}
		std::string m_msg;
	};

	class storage
	{
	public:
		storage(
			const torrent_info& info
		  , const boost::filesystem::path& path);

		void swap(storage&);

		typedef entry::integer_type size_type;

		size_type read(char* buf, int slot, size_type offset, size_type size);
		void write(const char* buf, int slot, size_type offset, size_type size);

	private:
		struct impl;
		opaque_value_ptr<impl> m_pimpl;
	};

	class piece_manager : boost::noncopyable
	{
	public:
		typedef entry::integer_type size_type;

		piece_manager(
			const torrent_info& info
		  , const boost::filesystem::path& path);

		~piece_manager();

		void check_pieces(
			boost::mutex& mutex
		  , detail::piece_checker_data& data
		  , std::vector<bool>& pieces);

		void allocate_slots(int num_slots);

		size_type read(char* buf, int piece_index, size_type offset, size_type size);
		void write(const char* buf, int piece_index, size_type offset, size_type size);

		const boost::filesystem::path& save_path() const;

		// fills the vector that maps all allocated
		// slots to the piece that is stored (or
		// partially stored) there. -2 is the index
		// of unassigned pieces and -1 is unallocated
		void export_piece_map(std::vector<int>& pieces) const;

	private:
		struct impl;
		std::auto_ptr<impl> m_pimpl;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED

