/*

Copyright (c) 2003, Arvid Norberg, Daniel Wallin
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

#include <ios>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/ref.hpp>

#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/file.hpp"

#if defined(_MSC_VER)
#define for if (false) {} else for
#endif

namespace {

	struct lazy_hash
	{
		mutable libtorrent::sha1_hash digest;
		mutable libtorrent::hasher h;
		mutable const char* data;
		std::size_t size;

		lazy_hash(const char* data_, std::size_t size_)
			: data(data_)
			, size(size_)
		{
			assert(data_);
			assert(size_>0);
		}

		const libtorrent::sha1_hash& get() const
		{
			if (data)
			{
				h.update(data, size);
				digest = h.final();
				data = 0;
			}
			return digest;
		}
	};

} // namespace unnamed

namespace fs = boost::filesystem;

namespace {

	void print_to_log(const std::string& s)
	{
		static std::ofstream log("log.txt");
		log << s;
		log.flush();
	}

}

namespace libtorrent
{

	std::vector<size_type> get_filesizes(
		const torrent_info& t
		, const boost::filesystem::path& p)
	{
		std::vector<size_type> sizes;
		for (torrent_info::file_iterator i = t.begin_files();
			i != t.end_files();
			++i)
		{
			size_type file_size;
			try
			{
				file f(p / i->path / i->filename, file::in);
				f.seek(0, file::end);
				file_size = f.tell();
			}
			catch (file_error&)
			{
				file_size = 0;
			}
			sizes.push_back(file_size);
		}
		return sizes;
	}

	bool match_filesizes(
		const torrent_info& t
		, const boost::filesystem::path& p
		, const std::vector<size_type>& sizes)
	{
		if (sizes.size() != t.num_files()) return false;

		std::vector<size_type>::const_iterator s = sizes.begin();
		for (torrent_info::file_iterator i = t.begin_files();
			i != t.end_files();
			++i, ++s)
		{
			size_type file_size;
			try
			{
				file f(p / i->path / i->filename, file::in);
				f.seek(0, file::end);
				file_size = f.tell();
			}
			catch (file_error&)
			{
				file_size = 0;
			}
			if (file_size != *s) return false;
		}
		return true;
	}

	struct thread_safe_storage
	{
		thread_safe_storage(std::size_t n)
			: slots(n, false)
		{ assert(n>=0); }

		boost::mutex mutex;
		boost::condition condition;
		std::vector<bool> slots;
	};

	struct slot_lock
	{
		slot_lock(thread_safe_storage& s, int slot_)
			: storage_(s)
			, slot(slot_)
		{
			assert(slot_>=0 && (unsigned)slot_ < s.slots.size());
			boost::mutex::scoped_lock lock(storage_.mutex);

			while (storage_.slots[slot])
				storage_.condition.wait(lock);
			storage_.slots[slot] = true;
		}

		~slot_lock()
		{
			storage_.slots[slot] = false;
			storage_.condition.notify_all();
		}

		thread_safe_storage& storage_;
		int slot;
	};

	struct storage::impl : thread_safe_storage
	{
		impl(const torrent_info& info, const fs::path& path)
			: thread_safe_storage(info.num_pieces())
			, info(info)
			, save_path(path)
		{}

		impl(const impl& x)
			: thread_safe_storage(x.info.num_pieces())
			, info(x.info)
			, save_path(x.save_path)
		{}

		const torrent_info& info;
		const boost::filesystem::path save_path;
	};

	storage::storage(const torrent_info& info, const fs::path& path)
		: m_pimpl(new impl(info, path))
	{
		assert(info.begin_files() != info.end_files());
	}

	void storage::swap(storage& other)
	{
		m_pimpl.swap(other.m_pimpl);
	}

	size_type storage::read(
		char* buf
	  , int slot
	  , size_type offset
  	  , size_type size)
	{
		assert(buf);
		assert(slot >= 0 && slot < m_pimpl->info.num_pieces());
		assert(offset >= 0);
		assert(offset < m_pimpl->info.piece_size(slot));
		assert(size > 0);

		slot_lock lock(*m_pimpl, slot);

		size_type start = slot * m_pimpl->info.piece_length() + offset;

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = m_pimpl->info.begin_files();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
		}
/*
		fs::ifstream in(
			m_pimpl->save_path / file_iter->path / file_iter->filename
			, std::ios_base::binary
		);
*/
		file in(
			m_pimpl->save_path / file_iter->path / file_iter->filename
			, file::in);

		assert(file_offset < file_iter->size);

//		in.seekg(file_offset);
		in.seek(file_offset);

//		assert(size_type(in.tellg()) == file_offset);
#ifndef NDEBUG
		size_type in_tell = in.tell();
		assert(in_tell == file_offset);
#endif

		size_type left_to_read = size;
		size_type slot_size = m_pimpl->info.piece_size(slot);

		if (offset + left_to_read > slot_size)
			left_to_read = slot_size - offset;

		assert(left_to_read >= 0);

		int result = left_to_read;
		int buf_pos = 0;

		while (left_to_read > 0)
		{
			int read_bytes = left_to_read;
			if (file_offset + read_bytes > file_iter->size)
				read_bytes = file_iter->size - file_offset;

			assert(read_bytes > 0);

//			in.read(buf + buf_pos, read_bytes);
//			int actual_read = in.gcount();
			int actual_read = in.read(buf + buf_pos, read_bytes);

			assert(read_bytes == actual_read);

			left_to_read -= read_bytes;
			buf_pos += read_bytes;
			assert(buf_pos >= 0);
			file_offset += read_bytes;

			if (left_to_read > 0)
			{
				++file_iter;
				fs::path path = m_pimpl->save_path / file_iter->path / file_iter->filename;

				file_offset = 0;
//				in.close();
//				in.clear();
//				in.open(path, std::ios_base::binary);
				in.open(path, file::in);
			}
		}

		return result;
	}

	void storage::write(const char* buf, int slot, size_type offset, size_type size)
	{
		assert(buf);
		assert(slot >= 0 && slot < m_pimpl->info.num_pieces());
		assert(offset >= 0);
		assert(size > 0);

		slot_lock lock(*m_pimpl, slot);

		size_type start = slot * m_pimpl->info.piece_length() + offset;

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = m_pimpl->info.begin_files();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
		}

		fs::path path(m_pimpl->save_path / file_iter->path / file_iter->filename);
/*
		fs::ofstream out;

		if (fs::exists(path))
			out.open(path, std::ios_base::binary | std::ios_base::in);
		else
			out.open(path, std::ios_base::binary);
*/
		file out(path, file::out);

		assert(file_offset < file_iter->size);

//		out.seekp(file_offset);
		out.seek(file_offset);

//		assert(file_offset == out.tellp());
#ifndef NDEBUG
		size_type out_tell = out.tell();
		assert(file_offset == out_tell);
#endif

		size_type left_to_write = size;
		size_type slot_size = m_pimpl->info.piece_size(slot);

		if (offset + left_to_write > slot_size)
			left_to_write = slot_size - offset;

		assert(left_to_write >= 0);

		int buf_pos = 0;

		// TODO
		// handle case when we can't write size bytes.
		while (left_to_write > 0)
		{
			int write_bytes = left_to_write;
			if (file_offset + write_bytes > file_iter->size)
			{
				assert(file_iter->size > file_offset);
				write_bytes = file_iter->size - file_offset;
			}

			assert(buf_pos >= 0);
			assert(write_bytes > 0);
			out.write(buf + buf_pos, write_bytes);

			left_to_write -= write_bytes;
			buf_pos += write_bytes;
			assert(buf_pos >= 0);
			file_offset += write_bytes;
			assert(file_offset <= file_iter->size);

			if (left_to_write > 0)
			{
				++file_iter;

				assert(file_iter != m_pimpl->info.end_files());

				fs::path path = m_pimpl->save_path / file_iter->path / file_iter->filename;

				file_offset = 0;
/*
				out.close();
				out.clear();

				if (fs::exists(path))
					out.open(path, std::ios_base::binary | std::ios_base::in);
				else
					out.open(path, std::ios_base::binary);
*/
				out.open(path, file::out);
			}
		}
	}





	// -- piece_manager -----------------------------------------------------

	class piece_manager::impl
	{
	public:

		impl(
			const torrent_info& info
		  , const boost::filesystem::path& path);

		void check_pieces(
			boost::mutex& mutex
		  , detail::piece_checker_data& data
		  , std::vector<bool>& pieces);

		void allocate_slots(int num_slots);
		void mark_failed(int index);
		unsigned long piece_crc(
			int slot_index
			, int block_size
			, const std::bitset<256>& bitmask);

		int slot_for_piece(int piece_index) const;

		size_type read(char* buf, int piece_index, size_type offset, size_type size);
		void write(const char* buf, int piece_index, size_type offset, size_type size);

		const boost::filesystem::path& save_path() const
		{ return m_save_path; }

		void export_piece_map(std::vector<int>& p) const;
		
	private:
		// returns the slot currently associated with the given
		// piece or assigns the given piece_index to a free slot

		int allocate_slot_for_piece(int piece_index);
#ifndef NDEBUG
		void check_invariant() const;
#ifdef TORRENT_STORAGE_DEBUG
		void debug_log() const;
#endif
#endif
		storage m_storage;

		// a bitmask representing the pieces we have
		std::vector<bool> m_have_piece;

		const torrent_info& m_info;

		// maps piece index to slot index. -1 means the piece
		// doesn't exist
		enum { has_no_slot = -3 };
		std::vector<int> m_piece_to_slot;
		// slots that hasn't had any file storage allocated
		std::vector<int> m_unallocated_slots;
		// slots that has file storage, but isn't assigned to a piece
		std::vector<int> m_free_slots;

		// index here is a slot number in the file
		// if index>=0, the slot is assigned to this piece
		// otherwise it can have one of these values:
		enum
		{
			unallocated = -1, // the slot is unallocated
			unassigned = -2   // the slot is allocated but not assigned to a piece
		};

		std::vector<int> m_slot_to_piece;

		boost::filesystem::path m_save_path;

		mutable boost::recursive_mutex m_mutex;

		bool m_allocating;
		boost::mutex m_allocating_monitor;
		boost::condition m_allocating_condition;
	};

	piece_manager::impl::impl(
		const torrent_info& info
	  , const fs::path& save_path)
		: m_storage(info, save_path)
		, m_info(info)
		, m_save_path(save_path)
	{
	}

	piece_manager::piece_manager(
		const torrent_info& info
	  , const fs::path& save_path)
		: m_pimpl(new impl(info, save_path))
	{
	}

	piece_manager::~piece_manager()
	{
	}

	void piece_manager::impl::export_piece_map(
			std::vector<int>& p) const
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------
#ifndef NDEBUG
		check_invariant();
#endif
		p.clear();
		std::vector<int>::const_reverse_iterator last; 
		for (last = m_slot_to_piece.rbegin();
			last != m_slot_to_piece.rend();
			++last)
		{
			if (*last != unallocated) break;
		}

		for (std::vector<int>::const_iterator i =
			m_slot_to_piece.begin();
			i != last.base();
			++i)
		{
			p.push_back(*i);
		}

#ifndef NDEBUG
		check_invariant();
#endif
	}

	void piece_manager::export_piece_map(
			std::vector<int>& p) const
	{
		m_pimpl->export_piece_map(p);
	}

	void piece_manager::impl::mark_failed(int piece_index)
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

#ifndef NDEBUG
		check_invariant();
#endif
		assert(piece_index >= 0 && (unsigned)piece_index < m_piece_to_slot.size());
		assert(m_piece_to_slot[piece_index] >= 0);

		int slot_index = m_piece_to_slot[piece_index];

		assert(slot_index >= 0);

		m_slot_to_piece[slot_index] = unassigned;
		m_piece_to_slot[piece_index] = has_no_slot;
		m_free_slots.push_back(slot_index);

#ifndef NDEBUG
		check_invariant();
#endif

	}

	void piece_manager::mark_failed(int index)
	{
		m_pimpl->mark_failed(index);
	}

	int piece_manager::slot_for_piece(int piece_index) const
	{
		return m_pimpl->slot_for_piece(piece_index);
	}

	int piece_manager::impl::slot_for_piece(int piece_index) const
	{
		assert(piece_index >= 0 && piece_index < m_info.num_pieces());
		return m_piece_to_slot[piece_index];
	}

	unsigned long piece_manager::piece_crc(
		int index
		, int block_size
		, const std::bitset<256>& bitmask)
	{
		return m_pimpl->piece_crc(index, block_size, bitmask);
	}

	unsigned long piece_manager::impl::piece_crc(
		int slot_index
		, int block_size
		, const std::bitset<256>& bitmask)
	{
		assert(slot_index >= 0 && slot_index < m_info.num_pieces());
		assert(block_size>0);

		adler32_crc crc;
		std::vector<char> buf(block_size);
		int num_blocks = m_info.piece_size(slot_index) / block_size;
		int last_block_size = m_info.piece_size(slot_index) % block_size;
		if (last_block_size == 0) last_block_size = block_size;

		for (int i = 0; i < num_blocks-1; ++i)
		{
			if (!bitmask[i]) continue;
			m_storage.read(
				&buf[0]
				, slot_index
				, i * block_size
				, block_size);
			crc.update(&buf[0], block_size);
		}
		if (bitmask[num_blocks - 1])
		{
			m_storage.read(
				&buf[0]
				, slot_index
				, block_size * (num_blocks - 1)
				, last_block_size);
			crc.update(&buf[0], last_block_size);
		}
		return crc.final();
	}

	size_type piece_manager::impl::read(
		char* buf
	  , int piece_index
	  , size_type offset
	  , size_type size)
	{
		assert(buf);
		assert(offset >= 0);
		assert(size > 0);
		assert(piece_index >= 0 && (unsigned)piece_index < m_piece_to_slot.size());
		assert(m_piece_to_slot[piece_index] >= 0 && (unsigned)m_piece_to_slot[piece_index] < m_slot_to_piece.size());
		int slot = m_piece_to_slot[piece_index];
		assert(slot >= 0 && (unsigned)slot < m_slot_to_piece.size());
		return m_storage.read(buf, slot, offset, size);
	}

	size_type piece_manager::read(
		char* buf
	  , int piece_index
	  , size_type offset
	  , size_type size)
	{
		return m_pimpl->read(buf, piece_index, offset, size);
	}

	void piece_manager::impl::write(
		const char* buf
	  , int piece_index
	  , size_type offset
	  , size_type size)
	{
		assert(buf);
		assert(offset >= 0);
		assert(size > 0);
		assert(piece_index >= 0 && (unsigned)piece_index < m_piece_to_slot.size());
		int slot = allocate_slot_for_piece(piece_index);
		assert(slot >= 0 && (unsigned)slot < m_slot_to_piece.size());
		m_storage.write(buf, slot, offset, size);
	}

	void piece_manager::write(
		const char* buf
	  , int piece_index
	  , size_type offset
	  , size_type size)
	{
		m_pimpl->write(buf, piece_index, offset, size);
	}

	// TODO: must handle the case where some hashes are identical
	// correctly
	void piece_manager::impl::check_pieces(
		boost::mutex& mutex
	  , detail::piece_checker_data& data
	  , std::vector<bool>& pieces)
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

#ifndef NDEBUG
		check_invariant();
#endif

		m_allocating = false;
		m_piece_to_slot.resize(m_info.num_pieces(), has_no_slot);
		m_slot_to_piece.resize(m_info.num_pieces(), unallocated);
		m_free_slots.resize(0);
		m_unallocated_slots.resize(0);

		const std::size_t piece_size = m_info.piece_length();
		const std::size_t last_piece_size = m_info.piece_size(
				m_info.num_pieces() - 1);



		// if we have fast-resume info
		// use it instead of doing the actual checking
		if (!data.piece_map.empty()
			&& data.piece_map.size() <= m_slot_to_piece.size())
		{
			for (int i = 0; (unsigned)i < data.piece_map.size(); ++i)
			{
				m_slot_to_piece[i] = data.piece_map[i];
				if (data.piece_map[i] >= 0)
				{
					m_piece_to_slot[data.piece_map[i]] = i;
					int found_piece = data.piece_map[i];

					// if the piece is not in the unfinished list
					// we have all of it
					if (std::find_if(
						data.unfinished_pieces.begin()
						, data.unfinished_pieces.end()
						, piece_picker::has_index(found_piece))
						== data.unfinished_pieces.end())
					{
						pieces[found_piece] = true;
					}
				}
				else if (data.piece_map[i] == unassigned)
				{
					m_free_slots.push_back(i);
				}
				else
				{
					assert(data.piece_map[i] == unallocated);
					m_unallocated_slots.push_back(i);
				}
			}

			for (int i = data.piece_map.size(); (unsigned)i < pieces.size(); ++i)
			{
				m_unallocated_slots.push_back(i);
			}
#ifndef NDEBUG
			check_invariant();
#endif

			return;
		}


		// do the real check (we didn't have valid resume data)

		bool changed_file = true;
//		fs::ifstream in;
		file in;

		std::vector<char> piece_data(m_info.piece_length());
		std::size_t piece_offset = 0;

		int current_slot = 0;
		std::size_t bytes_to_read = m_info.piece_size(0);
		size_type bytes_current_read = 0;
		size_type seek_into_next = 0;
		size_type filesize = 0;
		size_type start_of_read = 0;
		size_type start_of_file = 0;

		{
			boost::mutex::scoped_lock lock(mutex);
			data.progress = 0.f;
		}

		for (torrent_info::file_iterator file_iter = m_info.begin_files(),
			end_iter = m_info.end_files(); 
			file_iter != end_iter;)
		{
			assert(current_slot>=0 && current_slot<m_info.num_pieces());

			// Update progress meter and check if we've been requested to abort
			{
				boost::mutex::scoped_lock lock(mutex);

				data.progress = (float)current_slot / m_info.num_pieces();
				if (data.abort)
					return;
			}

			fs::path path(m_save_path / file_iter->path);

			// if the path doesn't exist, create the
			// entire directory tree
			if (!fs::exists(path))
				fs::create_directories(path);

			path /= file_iter->filename;

			if (changed_file)
			{
				try
				{
					changed_file = false;
					bytes_current_read = seek_into_next;
					in.open(path, file::in);

					in.seek(0, file::end);
					filesize = in.tell();
					in.seek(seek_into_next);
				}
				catch (file_error&)
				{
					filesize = 0;
				}
			}

			// we are at the start of a new piece
			// so we store the start of the piece
			if (bytes_to_read == m_info.piece_size(current_slot))
				start_of_read = current_slot * (size_type)piece_size;

			std::size_t bytes_read = 0;

			if (filesize > 0)
			{
				bytes_read = in.read(&piece_data[piece_offset], bytes_to_read);
				assert(bytes_read>0);
			}

			bytes_current_read += bytes_read;
			bytes_to_read -= bytes_read;

			assert(bytes_to_read >= 0);

			// bytes left to read, go on with next file
			if (bytes_to_read > 0)
			{
				if (bytes_current_read != file_iter->size)
				{
					size_type pos;
					size_type file_end = start_of_file + file_iter->size;

					for (pos = start_of_read; pos < file_end;
							pos += piece_size)
					{
						m_unallocated_slots.push_back(current_slot);
						++current_slot;
						assert(current_slot <= m_info.num_pieces());
					}

					seek_into_next = pos - file_end;
					bytes_to_read = m_info.piece_size(current_slot);
					piece_offset = 0;
				}
				else
				{
					seek_into_next = 0;
					piece_offset += bytes_read;
				}

				changed_file = true;
				start_of_file += file_iter->size;
				++file_iter;
				continue;
			}

			assert(current_slot < m_info.num_pieces());
			assert(m_slot_to_piece[current_slot]==unallocated);
			assert(m_piece_to_slot[current_slot]==has_no_slot ||
				(m_piece_to_slot[current_slot]>=0 &&
				 m_piece_to_slot[current_slot]<current_slot &&
				 m_slot_to_piece[m_piece_to_slot[current_slot]]==current_slot));

			// we need to take special actions if this is 
			// the last piece, since that piece might actually 
			// be smaller than piece_size.

			lazy_hash large_digest(&piece_data[0], piece_size);
			lazy_hash small_digest(&piece_data[0], last_piece_size);
			
			const lazy_hash* digest[2] = {
				&large_digest, &small_digest
			};

			int found_piece = -1;

			// TODO: there's still potential problems if some
			// pieces have the same hash
			// for the file not to be corrupt, piece_index <= slot_index
			for (int i = current_slot; i < m_info.num_pieces(); ++i)
			{
				if (pieces[i] && i != current_slot) continue;

				const sha1_hash& hash = digest[
					(i == m_info.num_pieces() - 1) ? 1 : 0]->get();

				if (hash == m_info.hash_for_piece(i))
				{
					found_piece = i;
					if (i == current_slot) break;
				}
			}

			if (found_piece != -1)
			{
				// if we have found this piece hash once already
				// move it to the free pieces and don't decrease
				// bytes_left
				if (pieces[found_piece])
				{
					assert(m_piece_to_slot[found_piece] == current_slot);
					m_slot_to_piece[m_piece_to_slot[found_piece]] = unassigned;
					m_free_slots.push_back(m_piece_to_slot[found_piece]);
					m_piece_to_slot[found_piece]=has_no_slot;
				}

				assert(m_piece_to_slot[found_piece]==has_no_slot);
				m_piece_to_slot[found_piece] = current_slot;
				m_slot_to_piece[current_slot] = found_piece;
				pieces[found_piece] = true;
			}
			else
			{
				assert(found_piece==-1);
				m_slot_to_piece[current_slot] = unassigned;

				m_free_slots.push_back(current_slot);
			}

			assert(m_slot_to_piece[current_slot]!=unallocated);

			// done with piece, move on to next
			piece_offset = 0;
			++current_slot;
			if(current_slot==m_info.num_pieces())
			{
				assert(file_iter == end_iter-1);
				break;
			}
			bytes_to_read = m_info.piece_size(current_slot);
		}

		// dirty "fix" for a bug when file is corrupt
		for(int i=0;(unsigned)i<m_info.num_pieces();i++)
		{
			if(m_piece_to_slot[i]!=has_no_slot && m_piece_to_slot[i]!=i && m_slot_to_piece[i]!=unallocated)
			{
				assert(m_piece_to_slot[i]>=0 && (unsigned)m_piece_to_slot[i]<m_slot_to_piece.size());
				assert(m_slot_to_piece[m_piece_to_slot[i]]==i);
				if(m_slot_to_piece[i]!=unassigned)
				{
					assert(m_slot_to_piece[i]>=0 && (unsigned)m_slot_to_piece[i]<m_piece_to_slot.size());
					assert(m_piece_to_slot[m_slot_to_piece[i]]==i);
					m_piece_to_slot[m_slot_to_piece[i]]=has_no_slot;
					m_slot_to_piece[i]=unassigned;
					m_free_slots.push_back(i);
				}
				m_slot_to_piece[m_piece_to_slot[i]]=unassigned;
				m_free_slots.push_back(m_piece_to_slot[i]);
				m_piece_to_slot[i]=has_no_slot;
			}
		}

#ifndef NDEBUG
		std::stringstream s;

		s << " m_free_slots: " << m_free_slots.size() << "\n"
			" m_unallocated_slots: " << m_unallocated_slots.size() << "\n"
			" num pieces: " << m_info.num_pieces() << "\n"
			" have_pieces:\n";
		for (std::vector<bool>::iterator i = pieces.begin();
			i != pieces.end();
			++i)
		{
			if (((i - pieces.begin()) % 60) == 0) s << "\n";
			if (*i) s << "1"; else s << "0";
		}
		s << "\n";
		s << std::count(pieces.begin(), pieces.end(), true) << "\n";
		data.torrent_ptr->debug_log(s.str());
#endif

#ifndef NDEBUG
		check_invariant();
#endif
	}

	void piece_manager::check_pieces(
		boost::mutex& mutex
	  , detail::piece_checker_data& data
	  , std::vector<bool>& pieces)
	{
		m_pimpl->check_pieces(mutex, data, pieces);
	}
	
	int piece_manager::impl::allocate_slot_for_piece(int piece_index)
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

#ifndef NDEBUG
		check_invariant();
#endif

		assert(piece_index >= 0 && (unsigned)piece_index < m_piece_to_slot.size());
		assert(m_piece_to_slot.size() == m_slot_to_piece.size());

		int slot_index = m_piece_to_slot[piece_index];

		if (slot_index != has_no_slot)
		{
			assert(slot_index >= 0 && (unsigned)slot_index < m_slot_to_piece.size());

#ifndef NDEBUG
			check_invariant();
#endif

			return slot_index;
		}

		if (m_free_slots.empty())
		{
			allocate_slots(5);
			assert(!m_free_slots.empty());
		}

		std::vector<int>::iterator iter(
			std::find(
				m_free_slots.begin()
				, m_free_slots.end()
				, piece_index));

		if (iter == m_free_slots.end())
		{
			assert(m_slot_to_piece[piece_index] != unassigned);
			iter = m_free_slots.end() - 1;

			// special case to make sure we don't use the last slot
			// when we shouldn't, since it's smaller than ordinary slots
			if (*iter == m_info.num_pieces() - 1 && piece_index != *iter)
			{
				if (m_free_slots.size() == 1)
					allocate_slots(5);
				assert(m_free_slots.size() > 1);
				// TODO: assumes that all allocated slots
				// are put at the end of the free_slots vector
				iter = m_free_slots.end() - 1;
			}
		}

		slot_index = *iter;
		m_free_slots.erase(iter);

		assert(m_slot_to_piece[slot_index] == unassigned);

		m_slot_to_piece[slot_index] = piece_index;
		m_piece_to_slot[piece_index] = slot_index;
	
		// there is another piece already assigned to
		// the slot we are interested in, swap positions
		if (slot_index != piece_index
			&& m_slot_to_piece[piece_index] >= 0)
		{
#ifndef NDEBUG
			std::stringstream s;

			s << "there is another piece at our slot, swapping..";

			s << "\n   piece_index: ";
			s << piece_index;
			s << "\n   slot_index: ";
			s << slot_index;
			s << "\n   piece at our slot: ";
			s << m_slot_to_piece[piece_index];
			s << "\n";
#endif
			int piece_at_our_slot = m_slot_to_piece[piece_index];
			assert(m_piece_to_slot[piece_at_our_slot] == piece_index);
#ifndef NDEBUG
			print_to_log(s.str());
#ifdef TORRENT_STORAGE_DEBUG
			debug_log();
#endif
#endif
			std::swap(
				m_slot_to_piece[piece_index]
				, m_slot_to_piece[slot_index]);

			std::swap(
				m_piece_to_slot[piece_index]
				, m_piece_to_slot[piece_at_our_slot]);

			std::vector<char> buf(m_info.piece_length());
			m_storage.read(&buf[0], piece_index, 0, m_info.piece_length());
			m_storage.write(&buf[0], slot_index, 0, m_info.piece_length());

			assert(m_slot_to_piece[piece_index] == piece_index);
			assert(m_piece_to_slot[piece_index] == piece_index);

			slot_index = piece_index;
#if !defined(NDEBUG) && defined(TORRENT_STORAGE_DEBUG)
			debug_log();
#endif
		}

		assert(slot_index>=0 && (unsigned)slot_index < m_slot_to_piece.size());

#ifndef NDEBUG
		check_invariant();
#endif
		return slot_index;
	}

	void piece_manager::impl::allocate_slots(int num_slots)
	{
		assert(num_slots>0);

		{
			boost::mutex::scoped_lock lock(m_allocating_monitor);

			while (m_allocating)
				m_allocating_condition.wait(lock);

			m_allocating = true;
		}

		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

#ifndef NDEBUG
		check_invariant();
#endif

		namespace fs = boost::filesystem;
		
		std::vector<int>::iterator iter
			= m_unallocated_slots.begin();
		std::vector<int>::iterator end_iter 
			= m_unallocated_slots.end();

		const size_type piece_size = m_info.piece_length();

		std::vector<char> zeros(piece_size, 0);

		for (int i = 0; i < num_slots; ++i, ++iter)
		{
			if (iter == end_iter)
				break;

			int pos = *iter;
			int piece_pos = pos;

			int new_free_slot = pos;

			if (m_piece_to_slot[pos] != has_no_slot)
			{
				assert(m_piece_to_slot[pos] >= 0);
				m_storage.read(&zeros[0], m_piece_to_slot[pos], 0, m_info.piece_size(pos));
				new_free_slot = m_piece_to_slot[pos];
				m_slot_to_piece[pos] = pos;
				m_piece_to_slot[pos] = pos;
			}

			m_slot_to_piece[new_free_slot] = unassigned;
			m_free_slots.push_back(new_free_slot);

			m_storage.write(&zeros[0], pos, 0, m_info.piece_size(pos));
		}

		m_unallocated_slots.erase(m_unallocated_slots.begin(), iter);

		m_allocating = false;

		assert(m_free_slots.size()>0);
		
#ifndef NDEBUG
		check_invariant();
#endif
	}

	void piece_manager::allocate_slots(int num_slots)
	{
		m_pimpl->allocate_slots(num_slots);
	}

	const boost::filesystem::path& piece_manager::save_path() const
	{
		return m_pimpl->save_path();
	}

#ifndef NDEBUG
	void piece_manager::impl::check_invariant() const
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------
		if (m_piece_to_slot.empty()) return;

		assert(m_piece_to_slot.size() == m_info.num_pieces());
		assert(m_slot_to_piece.size() == m_info.num_pieces());

		for(int i=0;(unsigned)i<m_free_slots.size();++i)
		{
			unsigned slot=m_free_slots[i];
			assert(slot<m_slot_to_piece.size());
			assert(m_slot_to_piece[slot] == unassigned);
		}

		for(int i=0;(unsigned)i<m_unallocated_slots.size();++i)
		{
			unsigned slot=m_unallocated_slots[i];
			assert(slot<m_slot_to_piece.size());
			assert(m_slot_to_piece[slot] == unallocated);
		}

		for (int i = 0; i < m_info.num_pieces(); ++i)
		{
			// Check domain of piece_to_slot's elements
			assert(m_piece_to_slot[i]==has_no_slot
				||(m_piece_to_slot[i]>=0 && (unsigned)m_piece_to_slot[i]<m_slot_to_piece.size()));

			// Check domain of slot_to_piece's elements
			assert(m_slot_to_piece[i]==unallocated
				|| m_slot_to_piece[i]==unassigned
				||(m_slot_to_piece[i]>=0 && (unsigned)m_slot_to_piece[i]<m_piece_to_slot.size()));

			// do more detailed checks on piece_to_slot
			if (m_piece_to_slot[i]>=0)
			{
				assert(m_slot_to_piece[m_piece_to_slot[i]]==i);
				if (m_piece_to_slot[i] != i)
				{
					assert(m_slot_to_piece[i] == unallocated);
				}
			}
			else
			{
				assert(m_piece_to_slot[i]==has_no_slot);
			}

			// do more detailed checks on slot_to_piece

			if (m_slot_to_piece[i]>=0)
			{
				assert((unsigned)m_slot_to_piece[i]<m_piece_to_slot.size());
				assert(m_piece_to_slot[m_slot_to_piece[i]]==i);
#ifdef TORRENT_STORAGE_DEBUG
				assert(
					std::find(
						m_unallocated_slots.begin()
						, m_unallocated_slots.end()
						, i) == m_unallocated_slots.end()
				);
				assert(
					std::find(
						m_free_slots.begin()
						, m_free_slots.end()
						, i) == m_free_slots.end()
				);
#endif
			}
			else if (m_slot_to_piece[i] == unallocated)
			{
#ifdef TORRENT_STORAGE_DEBUG
				assert(
					std::find(
						m_unallocated_slots.begin()
						, m_unallocated_slots.end()
						, i) != m_unallocated_slots.end()
				);
#endif
			}
			else if (m_slot_to_piece[i] == unassigned)
			{
#ifdef TORRENT_STORAGE_DEBUG
				assert(
					std::find(
						m_free_slots.begin()
						, m_free_slots.end()
						, i) != m_free_slots.end()
				);
#endif
			}
			else
			{
				assert(false && "m_slot_to_piece[i] is invalid");
			}
		}
	}

#ifdef TORRENT_STORAGE_DEBUG
	void piece_manager::impl::debug_log() const
	{
		std::stringstream s;

		s << "index\tslot\tpiece\n";

		for (int i = 0; i < m_info.num_pieces(); ++i)
		{
			s << i << "\t" << m_slot_to_piece[i] << "\t";
			s << m_piece_to_slot[i] << "\n";
		}

		s << "---------------------------------\n";

		print_to_log(s.str());
	}
#endif
#endif
} // namespace libtorrent

