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
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/ref.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/invariant_check.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
#define for if (false) {} else for
namespace std
{
	using ::srand;
	using ::rename;
}
#endif

using namespace boost::filesystem;

namespace
{

	void print_to_log(const std::string& s)
	{
		static std::ofstream log("log.txt");
		log << s;
		log.flush();
	}

	path get_filename(
		libtorrent::torrent_info const& t
		, path const& p)
	{
		assert(t.num_files() > 0);
		if (t.num_files() == 1)
			return p;
		else
			return t.name() / p;
	}
}

namespace libtorrent
{

	std::vector<size_type> get_filesizes(
		const torrent_info& t
		, path p)
	{
		p = complete(p);
		std::vector<size_type> sizes;
		for (torrent_info::file_iterator i = t.begin_files();
			i != t.end_files();
			++i)
		{
			size_type file_size;
			try
			{
				file f(p / get_filename(t, i->path), file::in);
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
		torrent_info const& t
		, path p
		, std::vector<size_type> const& sizes)
	{
		if ((int)sizes.size() != t.num_files()) return false;
		p = complete(p);

		std::vector<size_type>::const_iterator s = sizes.begin();
		for (torrent_info::file_iterator i = t.begin_files();
			i != t.end_files();
			++i, ++s)
		{
			size_type file_size;
			try
			{
				file f(p / get_filename(t, i->path), file::in);
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
		{}

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
			assert(slot_>=0 && slot_ < (int)s.slots.size());
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

	class storage::impl : public thread_safe_storage
	{
	public:
		impl(torrent_info const& info, path const& path)
			: thread_safe_storage(info.num_pieces())
			, info(info)
		{
			save_path = complete(path);
			assert(save_path.is_complete());
		}

		impl(impl const& x)
			: thread_safe_storage(x.info.num_pieces())
			, info(x.info)
			, save_path(x.save_path)
		{}

		torrent_info const& info;
		path save_path;
	};

	storage::storage(const torrent_info& info, const path& path)
		: m_pimpl(new impl(info, path))
	{
		assert(info.begin_files() != info.end_files());
	}

	void storage::swap(storage& other)
	{
		m_pimpl.swap(other.m_pimpl);
	}

	// returns true on success
	bool storage::move_storage(path save_path)
	{
		std::string old_path;
		std::string new_path;

		save_path = complete(save_path);

		if(!exists(save_path))
			create_directory(save_path);
		else if(!is_directory(save_path))
			return false;

		if (m_pimpl->info.num_files() == 1)
		{
			path single_file = m_pimpl->info.begin_files()->path;
			if (single_file.has_branch_path())
				create_directory(save_path / single_file.branch_path());

			old_path = (m_pimpl->save_path / single_file)
				.native_file_string();
			new_path = (save_path / m_pimpl->info.begin_files()->path)
				.native_file_string();
		}
		else
		{
			assert(m_pimpl->info.num_files() > 1);
			old_path = (m_pimpl->save_path / m_pimpl->info.name())
				.native_directory_string();
			new_path = (save_path / m_pimpl->info.name())
				.native_directory_string();
		}

		int ret = std::rename(old_path.c_str(), new_path.c_str()); 
		// This seems to return -1 even when it successfully moves the file
//		if (ret == 0)
		{
			m_pimpl->save_path = save_path;
			return true;
		}
		return false;
	}

#ifndef NDEBUG

	void storage::shuffle()
	{
		int num_pieces = m_pimpl->info.num_pieces();

		std::vector<int> pieces(num_pieces);
		for (std::vector<int>::iterator i = pieces.begin();
			i != pieces.end();
			++i)
		{
			*i = static_cast<int>(i - pieces.begin());
		}
		std::srand((unsigned int)std::time(0));
		std::vector<int> targets(pieces);
		std::random_shuffle(pieces.begin(), pieces.end());
		std::random_shuffle(targets.begin(), targets.end());

		for (int i = 0; i < std::max(num_pieces / 50, 1); ++i)
		{
			const int slot_index = targets[i];
			const int piece_index = pieces[i];
			const int slot_size =static_cast<int>(m_pimpl->info.piece_size(slot_index));
			std::vector<char> buf(slot_size);
			read(&buf[0], piece_index, 0, slot_size);
			write(&buf[0], slot_index, 0, slot_size);
		}
	}

#endif

	size_type storage::read(
		char* buf
	  , int slot
	  , int offset
  	  , int size)
	{
		assert(buf != 0);
		assert(slot >= 0 && slot < m_pimpl->info.num_pieces());
		assert(offset >= 0);
		assert(offset < m_pimpl->info.piece_size(slot));
		assert(size > 0);

		slot_lock lock(*m_pimpl, slot);

		size_type start = slot * (size_type)m_pimpl->info.piece_length() + offset;

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

		file in(
			m_pimpl->save_path / get_filename(m_pimpl->info, file_iter->path)
			, file::in);

		assert(file_offset < file_iter->size);

		in.seek(file_offset);
		if (in.tell() != file_offset)
		{
			// the file was not big enough
			throw file_error("slot has no storage");
		}

#ifndef NDEBUG
		size_type in_tell = in.tell();
		assert(in_tell == file_offset);
#endif

		int left_to_read = size;
		int slot_size = static_cast<int>(m_pimpl->info.piece_size(slot));

		if (offset + left_to_read > slot_size)
			left_to_read = slot_size - offset;

		assert(left_to_read >= 0);

		size_type result = left_to_read;
		int buf_pos = 0;

		while (left_to_read > 0)
		{
			int read_bytes = left_to_read;
			if (file_offset + read_bytes > file_iter->size)
				read_bytes = static_cast<int>(file_iter->size - file_offset);

			size_type actual_read = in.read(buf + buf_pos, read_bytes);

			if (read_bytes != actual_read)
			{
				// the file was not big enough
				throw file_error("slot has no storage");
			}

			left_to_read -= read_bytes;
			buf_pos += read_bytes;
			assert(buf_pos >= 0);
			file_offset += read_bytes;

			if (left_to_read > 0)
			{
				++file_iter;
				path path = m_pimpl->save_path / get_filename(m_pimpl->info, file_iter->path);

				file_offset = 0;
				in.open(path, file::in);
			}
		}

		return result;
	}

	// throws file_error if it fails to write
	void storage::write(
		const char* buf
		, int slot
		, int offset
		, int size)
	{
		assert(buf != 0);
		assert(slot >= 0);
		assert(slot < m_pimpl->info.num_pieces());
		assert(offset >= 0);
		assert(size > 0);

		slot_lock lock(*m_pimpl, slot);

		size_type start = slot * (size_type)m_pimpl->info.piece_length() + offset;

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

		path p(m_pimpl->save_path / get_filename(m_pimpl->info, file_iter->path));
		file out(p, file::out);

		assert(file_offset < file_iter->size);

		out.seek(file_offset);
		size_type pos = out.tell();

		if (pos != file_offset)
		{
			std::stringstream s;
			s << "no storage for slot " << slot;
			throw file_error(s.str());
		}

		int left_to_write = size;
		int slot_size = static_cast<int>(m_pimpl->info.piece_size(slot));

		if (offset + left_to_write > slot_size)
			left_to_write = slot_size - offset;

		assert(left_to_write >= 0);

		int buf_pos = 0;

		while (left_to_write > 0)
		{
			int write_bytes = left_to_write;
			if (file_offset + write_bytes > file_iter->size)
			{
				assert(file_iter->size > file_offset);
				write_bytes = static_cast<int>(file_iter->size - file_offset);
			}

			assert(buf_pos >= 0);
			assert(write_bytes > 0);
			size_type written = out.write(buf + buf_pos, write_bytes);

			if (written != write_bytes)
			{
				std::stringstream s;
				s << "no storage for slot " << slot;
				throw file_error(s.str());
			}

			left_to_write -= write_bytes;
			buf_pos += write_bytes;
			assert(buf_pos >= 0);
			file_offset += write_bytes;
			assert(file_offset <= file_iter->size);

			if (left_to_write > 0)
			{
				++file_iter;

				assert(file_iter != m_pimpl->info.end_files());
 				path p = m_pimpl->save_path / get_filename(m_pimpl->info, file_iter->path);
				file_offset = 0;
				out.open(p, file::out);
			}
		}
	}





	// -- piece_manager -----------------------------------------------------

	class piece_manager::impl
	{
	friend class invariant_access;
	public:

		impl(
			const torrent_info& info
		  , const path& path);

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

		size_type read(
			char* buf
			, int piece_index
			, int offset
			, int size);

		void write(
			const char* buf
			, int piece_index
			, int offset
			, int size);

		path const& save_path() const
		{ return m_save_path; }

		bool move_storage(path save_path)
		{
			if (m_storage.move_storage(save_path))
			{
				m_save_path = complete(save_path);
				return true;
			}
			return false;
		}

		void export_piece_map(std::vector<int>& p) const;
		
	private:
		// returns the slot currently associated with the given
		// piece or assigns the given piece_index to a free slot

		int identify_data(
			const std::vector<char>& piece_data
			, int current_slot
			, std::vector<bool>& have_pieces
			, const std::multimap<sha1_hash, int>& hash_to_piece);

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

		// slots that hasn't had any file storage allocated
		std::vector<int> m_unallocated_slots;
		// slots that has file storage, but isn't assigned to a piece
		std::vector<int> m_free_slots;

		enum
		{
			has_no_slot = -3 // the piece has no storage
		};

		// maps piece indices to slots. If a piece doesn't
		// have any storage, it is set to 'has_no_slot'
		std::vector<int> m_piece_to_slot;

		enum
		{
			unallocated = -1, // the slot is unallocated
			unassigned = -2   // the slot is allocated but not assigned to a piece
		};

		// maps slots to piece indices, if a slot doesn't have a piece
		// it can either be 'unassigned' or 'unallocated'
		std::vector<int> m_slot_to_piece;

		path m_save_path;

		mutable boost::recursive_mutex m_mutex;

		bool m_allocating;
		boost::mutex m_allocating_monitor;
		boost::condition m_allocating_condition;
	};

	piece_manager::impl::impl(
		const torrent_info& info
		, const path& save_path)
		: m_storage(info, save_path)
		, m_info(info)
		, m_save_path(complete(save_path))
		, m_allocating(false)
	{
		assert(m_save_path.is_complete());
	}

	piece_manager::piece_manager(
		const torrent_info& info
	  , const path& save_path)
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

		INVARIANT_CHECK;

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

		INVARIANT_CHECK;

		assert(piece_index >= 0 && piece_index < (int)m_piece_to_slot.size());
		assert(m_piece_to_slot[piece_index] >= 0);

		int slot_index = m_piece_to_slot[piece_index];

		assert(slot_index >= 0);

		m_slot_to_piece[slot_index] = unassigned;
		m_piece_to_slot[piece_index] = has_no_slot;
		m_free_slots.push_back(slot_index);
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
		assert(slot_index >= 0);
		assert(slot_index < m_info.num_pieces());
		assert(block_size > 0);

		adler32_crc crc;
		std::vector<char> buf(block_size);
		int num_blocks = static_cast<int>(m_info.piece_size(slot_index)) / block_size;
		int last_block_size = static_cast<int>(m_info.piece_size(slot_index)) % block_size;
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
	  , int offset
	  , int size)
	{
		assert(buf);
		assert(offset >= 0);
		assert(size > 0);
		assert(piece_index >= 0 && piece_index < (int)m_piece_to_slot.size());
		assert(m_piece_to_slot[piece_index] >= 0 && m_piece_to_slot[piece_index] < (int)m_slot_to_piece.size());
		int slot = m_piece_to_slot[piece_index];
		assert(slot >= 0 && slot < (int)m_slot_to_piece.size());
		return m_storage.read(buf, slot, offset, size);
	}

	size_type piece_manager::read(
		char* buf
	  , int piece_index
	  , int offset
	  , int size)
	{
		return m_pimpl->read(buf, piece_index, offset, size);
	}

	void piece_manager::impl::write(
		const char* buf
	  , int piece_index
	  , int offset
	  , int size)
	{
		assert(buf);
		assert(offset >= 0);
		assert(size > 0);
		assert(piece_index >= 0 && piece_index < (int)m_piece_to_slot.size());
		int slot = allocate_slot_for_piece(piece_index);
		assert(slot >= 0 && slot < (int)m_slot_to_piece.size());
		m_storage.write(buf, slot, offset, size);
	}

	void piece_manager::write(
		const char* buf
	  , int piece_index
	  , int offset
	  , int size)
	{
		m_pimpl->write(buf, piece_index, offset, size);
	}

	int piece_manager::impl::identify_data(
		const std::vector<char>& piece_data
		, int current_slot
		, std::vector<bool>& have_pieces
		, const std::multimap<sha1_hash, int>& hash_to_piece)
	{
		INVARIANT_CHECK;

		assert((int)have_pieces.size() == m_info.num_pieces());

		const int piece_size = static_cast<int>(m_info.piece_length());
		const int last_piece_size = static_cast<int>(m_info.piece_size(
			m_info.num_pieces() - 1));

		assert((int)piece_data.size() >= last_piece_size);

		// calculate a small digest, with the same
		// size as the last piece. And a large digest
		// which has the same size as a normal piece
		hasher small_digest;
		small_digest.update(&piece_data[0], last_piece_size);
		hasher large_digest(small_digest);
		assert(piece_size - last_piece_size >= 0);
		if (piece_size - last_piece_size > 0)
		{
			large_digest.update(
				&piece_data[last_piece_size]
				, piece_size - last_piece_size);
		}
		sha1_hash large_hash = large_digest.final();
		sha1_hash small_hash = small_digest.final();

		typedef std::multimap<sha1_hash, int>::const_iterator map_iter;
		map_iter begin1;
		map_iter end1;
		map_iter begin2;
		map_iter end2;

		// makes the lookups for the small digest and the large digest
		boost::tie(begin1, end1) = hash_to_piece.equal_range(small_hash);
		boost::tie(begin2, end2) = hash_to_piece.equal_range(large_hash);

		// copy all potential piece indices into this vector
		std::vector<int> matching_pieces;
		for (map_iter i = begin1; i != end1; ++i)
			matching_pieces.push_back(i->second);
		for (map_iter i = begin2; i != end2; ++i)
			matching_pieces.push_back(i->second);

		// no piece matched the data in the slot
		if (matching_pieces.empty())
			return unassigned;

		// ------------------------------------------
		// CHECK IF THE PIECE IS IN ITS CORRECT PLACE
		// ------------------------------------------

		if (std::find(
			matching_pieces.begin()
			, matching_pieces.end()
			, current_slot) != matching_pieces.end())
		{
			const int piece_index = current_slot;

			if (have_pieces[piece_index])
			{
				// we have already found a piece with
				// this index.
				int other_slot = m_piece_to_slot[piece_index];
				assert(other_slot >= 0);

				// take one of the other matching pieces
				// that hasn't already been assigned
				int other_piece = -1;
				for (std::vector<int>::iterator i = matching_pieces.begin();
					i != matching_pieces.end();
					++i)
				{
					if (have_pieces[*i] || *i == piece_index) continue;
					other_piece = *i;
					break;
				}
				if (other_piece >= 0)
				{
					// replace the old slot with 'other_piece'
					assert(have_pieces[other_piece] == false);
					have_pieces[other_piece] = true;
					m_slot_to_piece[other_slot] = other_piece;
					m_piece_to_slot[other_piece] = other_slot;
				}
				else
				{
					// this index is the only piece with this
					// hash. The previous slot we found with
					// this hash must be tha same piece. Mark
					// that piece as unassigned, since this slot
					// is the correct place for the piece.
					m_slot_to_piece[other_slot] = unassigned;
					m_free_slots.push_back(other_slot);
				}
				assert(m_piece_to_slot[piece_index] != current_slot);
				assert(m_piece_to_slot[piece_index] >= 0);
				m_piece_to_slot[piece_index] = has_no_slot;
				have_pieces[piece_index] = false;
			}
			
			assert(have_pieces[piece_index] == false);
			assert(m_piece_to_slot[piece_index] == has_no_slot);
			have_pieces[piece_index] = true;
			return piece_index;
		}

		// find a matching piece that hasn't
		// already been assigned
		int free_piece = unassigned;
		for (std::vector<int>::iterator i = matching_pieces.begin();
			i != matching_pieces.end();
			++i)
		{
			if (have_pieces[*i]) continue;
			free_piece = *i;
			break;
		}

		if (free_piece >= 0)
		{
			assert(have_pieces[free_piece] == false);
			assert(m_piece_to_slot[free_piece] == has_no_slot);
			have_pieces[free_piece] = true;

			return free_piece;
		}
		else
		{
			assert(free_piece == unassigned);
			return unassigned;
		}
	}

	void piece_manager::impl::check_pieces(
		boost::mutex& mutex
	  , detail::piece_checker_data& data
	  , std::vector<bool>& pieces)
	{
		assert(m_info.piece_length() > 0);
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

		INVARIANT_CHECK;

		// This will corrupt the storage
		// use while debugging to find
		// states that cannot be scanned
		// by check_pieces.
//		m_storage.shuffle();

		m_piece_to_slot.resize(m_info.num_pieces(), has_no_slot);
		m_slot_to_piece.resize(m_info.num_pieces(), unallocated);
		m_free_slots.clear();
		m_unallocated_slots.clear();

		pieces.clear();
		pieces.resize(m_info.num_pieces(), false);

		// if we have fast-resume info
		// use it instead of doing the actual checking
		if (!data.piece_map.empty()
			&& data.piece_map.size() <= m_slot_to_piece.size())
		{
			for (int i = 0; i < (int)data.piece_map.size(); ++i)
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

			for (int i = (int)data.piece_map.size(); i < (int)pieces.size(); ++i)
			{
				m_unallocated_slots.push_back(i);
			}
			return;
		}

		// ------------------------
		//    DO THE FULL CHECK
		// ------------------------

		// first, create all missing directories
		for (torrent_info::file_iterator file_iter = m_info.begin_files(),
			end_iter = m_info.end_files();  file_iter != end_iter; ++file_iter)
		{
			path dir = m_save_path / get_filename(m_info, file_iter->path);
			if (!exists(dir.branch_path()))
				create_directories(dir.branch_path());
		}

		std::vector<char> piece_data(static_cast<int>(m_info.piece_length()));

		std::multimap<sha1_hash, int> hash_to_piece;
		// build the hash-map, that maps hashes to pieces
		for (int i = 0; i < m_info.num_pieces(); ++i)
		{
			hash_to_piece.insert(std::make_pair(m_info.hash_for_piece(i), i));
		}

		for (int current_slot = 0; current_slot <  m_info.num_pieces(); ++current_slot)
		{
			try
			{

				m_storage.read(
					&piece_data[0]
					, current_slot
					, 0
					, static_cast<int>(m_info.piece_size(current_slot)));

				int piece_index = identify_data(
					piece_data
					, current_slot
					, pieces
					, hash_to_piece);

				assert(piece_index == unassigned || piece_index >= 0);
				
				const bool this_should_move = piece_index >= 0 && m_slot_to_piece[piece_index] != unallocated;
				const bool other_should_move = m_piece_to_slot[current_slot] != has_no_slot;

				// check if this piece should be swapped with any other slot
				// this section will ensure that the storage is correctly sorted
				// libtorrent will never leave the storage in a state that
				// requires this sorting, but other clients may.

				// example of worst case:
				//                          | current_slot = 5
				//                          V
				//  +---+- - - +---+- - - +---+- -
				//  | x |      | 5 |      | 3 |     <- piece data in slots
				//  +---+- - - +---+- - - +---+- -
				//    3          y          5       <- slot index

				// in this example, the data in the current_slot (5)
				// is piece 3. It has to be moved into slot 3. The data
				// in slot y (piece 5) should be moved into the current_slot.
				// and the data in slot 3 (piece x) should be moved to slot y.

				// there are three possible cases.
				// 1. There's another piece that should be placed into this slot
				// 2. This piece should be placed into another slot.
				// 3. There's another piece that should be placed into this slot
				//    and this piece should be placed into another slot

				// swap piece_index with this slot

				// case 1
				if (this_should_move && !other_should_move)
				{
					assert(piece_index != current_slot);

					const int other_slot = piece_index;
					assert(other_slot >= 0);
					int other_piece = m_slot_to_piece[other_slot];

					m_slot_to_piece[other_slot] = piece_index;
					m_slot_to_piece[current_slot] = other_piece;
					m_piece_to_slot[piece_index] = piece_index;
					if (other_piece >= 0) m_piece_to_slot[other_piece] = current_slot;

					if (other_piece == unassigned)
					{
						std::vector<int>::iterator i =
							std::find(m_free_slots.begin(), m_free_slots.end(), other_slot);
						assert(i != m_free_slots.end());
						m_free_slots.erase(i);
						m_free_slots.push_back(current_slot);
					}

					const int slot1_size = static_cast<int>(m_info.piece_size(piece_index));
					const int slot2_size = other_piece >= 0 ? static_cast<int>(m_info.piece_size(other_piece)) : 0;
					std::vector<char> buf1(slot1_size);
					m_storage.read(&buf1[0], current_slot, 0, slot1_size);
					if (slot2_size > 0)
					{
						std::vector<char> buf2(slot2_size);
						m_storage.read(&buf2[0], piece_index, 0, slot2_size);
						m_storage.write(&buf2[0], current_slot, 0, slot2_size);
					}
					m_storage.write(&buf1[0], piece_index, 0, slot1_size);
					assert(m_slot_to_piece[current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[current_slot]] == current_slot);
				}
				// case 2
				else if (!this_should_move && other_should_move)
				{
					assert(piece_index != current_slot);

					const int other_piece = current_slot;
					const int other_slot = m_piece_to_slot[other_piece];
					assert(other_slot >= 0);
				
					m_slot_to_piece[current_slot] = other_piece;
					m_slot_to_piece[other_slot] = piece_index;
					m_piece_to_slot[other_piece] = current_slot;
					if (piece_index >= 0) m_piece_to_slot[piece_index] = other_slot;

					if (piece_index == unassigned)
					{
						m_free_slots.push_back(other_slot);
					}

					const int slot1_size = static_cast<int>(m_info.piece_size(other_piece));
					const int slot2_size = piece_index >= 0 ? static_cast<int>(m_info.piece_size(piece_index)) : 0;
					std::vector<char> buf1(slot1_size);
					m_storage.read(&buf1[0], other_slot, 0, slot1_size);
					if (slot2_size > 0)
					{
						std::vector<char> buf2(slot2_size);
						m_storage.read(&buf2[0], current_slot, 0, slot2_size);
						m_storage.write(&buf2[0], other_slot, 0, slot2_size);
					}
					m_storage.write(&buf1[0], current_slot, 0, slot1_size);
					assert(m_slot_to_piece[current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[current_slot]] == current_slot);
				}
				else if (this_should_move && other_should_move)
				{
					assert(piece_index != current_slot);
					assert(piece_index >= 0);

					const int piece1 = m_slot_to_piece[piece_index];
					const int piece2 = current_slot;
					const int slot1 = piece_index;
					const int slot2 = m_piece_to_slot[piece2];

					assert(slot1 >= 0);
					assert(slot2 >= 0);
					assert(piece2 >= 0);

					// movement diagram:
					// +---------------------------------------+
					// |                                       |
					// +--> slot1 --> slot2 --> current_slot --+

					m_slot_to_piece[slot1] = piece_index;
					m_slot_to_piece[slot2] = piece1;
					m_slot_to_piece[current_slot] = piece2;

					m_piece_to_slot[piece_index] = slot1;
					m_piece_to_slot[current_slot] = piece2;
					if (piece1 >= 0) m_piece_to_slot[piece1] = slot2;

					if (piece1 == unassigned)
					{
						std::vector<int>::iterator i =
							std::find(m_free_slots.begin(), m_free_slots.end(), slot1);
						assert(i != m_free_slots.end());
						m_free_slots.erase(i);
						m_free_slots.push_back(slot2);
					}

					const int slot1_size = piece1 >= 0 ? static_cast<int>(m_info.piece_size(piece1)) : 0;
					const int slot2_size = static_cast<int>(m_info.piece_size(piece2));
					const int slot3_size = static_cast<int>(m_info.piece_size(piece_index));

					std::vector<char> buf1(static_cast<int>(m_info.piece_length()));
					std::vector<char> buf2(static_cast<int>(m_info.piece_length()));

					m_storage.read(&buf2[0], current_slot, 0, slot3_size);
					m_storage.read(&buf1[0], slot2, 0, slot2_size);
					m_storage.write(&buf1[0], current_slot, 0, slot2_size);
					if (slot1_size > 0)
					{
						m_storage.read(&buf1[0], slot1, 0, slot1_size);
						m_storage.write(&buf1[0], slot2, 0, slot1_size);
					}
					m_storage.write(&buf2[0], slot1, 0, slot3_size);
					assert(m_slot_to_piece[current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[current_slot]] == current_slot);
				}
				else
				{
					assert(m_piece_to_slot[current_slot] == has_no_slot || piece_index != current_slot);
					assert(m_slot_to_piece[current_slot] == unallocated);
					assert(piece_index == unassigned || m_piece_to_slot[piece_index] == has_no_slot);

					// the slot was identified as piece 'piece_index'
					if (piece_index != unassigned)
						m_piece_to_slot[piece_index] = current_slot;
					else
						m_free_slots.push_back(current_slot);

					m_slot_to_piece[current_slot] = piece_index;

					assert(m_slot_to_piece[current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[current_slot]] == current_slot);
				}
			}
			catch (file_error&)
			{
				// find the file that failed, and skip all the blocks in that file
				size_type file_offset = 0;
				size_type current_offset = current_slot * m_info.piece_length();
				for (torrent_info::file_iterator i = m_info.begin_files();
					i != m_info.end_files(); ++i)
				{
					file_offset += i->size;
					if (file_offset > current_offset) break;
				}

				assert(file_offset > current_offset);
				int skip_blocks = static_cast<int>(
					(file_offset - current_offset + m_info.piece_length() - 1)
					/ m_info.piece_length());

				for (int i = current_slot; i < current_slot + skip_blocks; ++i)
				{
					assert(m_slot_to_piece[i] == unallocated);
					m_unallocated_slots.push_back(i);
				}

				// current slot will increase by one at the end of the for-loop too
				current_slot += skip_blocks - 1;
			}

			// Update progress meter and check if we've been requested to abort
			{
				boost::mutex::scoped_lock lock(mutex);
				data.progress = (float)current_slot / m_info.num_pieces();
				if (data.abort || (data.torrent_ptr && data.torrent_ptr->is_aborted()))
					return;
			}
		}
		// TODO: sort m_free_slots and m_unallocated_slots?
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

		INVARIANT_CHECK;

		assert(piece_index >= 0);
	  	assert(piece_index < (int)m_piece_to_slot.size());
		assert(m_piece_to_slot.size() == m_slot_to_piece.size());

		int slot_index = m_piece_to_slot[piece_index];

		if (slot_index != has_no_slot)
		{
			assert(slot_index >= 0);
			assert(slot_index < (int)m_slot_to_piece.size());
			return slot_index;
		}

		if (m_free_slots.empty())
		{
			allocate_slots(1);
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
			assert(!m_free_slots.empty());
			iter = m_free_slots.end() - 1;

			// special case to make sure we don't use the last slot
			// when we shouldn't, since it's smaller than ordinary slots
			if (*iter == m_info.num_pieces() - 1 && piece_index != *iter)
			{
				if (m_free_slots.size() == 1)
					allocate_slots(1);
				assert(m_free_slots.size() > 1);
				// assumes that all allocated slots
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

#if !defined(NDEBUG) && defined(TORRENT_STORAGE_DEBUG)
			std::stringstream s;

			s << "there is another piece at our slot, swapping..";

			s << "\n   piece_index: ";
			s << piece_index;
			s << "\n   slot_index: ";
			s << slot_index;
			s << "\n   piece at our slot: ";
			s << m_slot_to_piece[piece_index];
			s << "\n";

			print_to_log(s.str());
			debug_log();
#endif

			int piece_at_our_slot = m_slot_to_piece[piece_index];
			assert(m_piece_to_slot[piece_at_our_slot] == piece_index);

			std::swap(
				m_slot_to_piece[piece_index]
				, m_slot_to_piece[slot_index]);

			std::swap(
				m_piece_to_slot[piece_index]
				, m_piece_to_slot[piece_at_our_slot]);

			const int slot_size = static_cast<int>(m_info.piece_size(slot_index));
			std::vector<char> buf(slot_size);
			m_storage.read(&buf[0], piece_index, 0, slot_size);
			m_storage.write(&buf[0], slot_index, 0, slot_size);

			assert(m_slot_to_piece[piece_index] == piece_index);
			assert(m_piece_to_slot[piece_index] == piece_index);

			slot_index = piece_index;

#if !defined(NDEBUG) && defined(TORRENT_STORAGE_DEBUG)
			debug_log();
#endif
		}

		assert(slot_index >= 0);
	  	assert(slot_index < (int)m_slot_to_piece.size());
		return slot_index;
	}

	namespace
	{
		// this is used to notify potential other
		// threads that the allocation-function has exited
		struct allocation_syncronization
		{
			allocation_syncronization(
				bool& flag
				, boost::condition& cond
				, boost::mutex& monitor)
				: m_flag(flag)
				, m_cond(cond)
				, m_monitor(monitor)
			{
				boost::mutex::scoped_lock lock(m_monitor);

				while (m_flag)
					m_cond.wait(lock);

				m_flag = true;
			}

			~allocation_syncronization()
			{
				boost::mutex::scoped_lock lock(m_monitor);
				m_flag = false;
				m_cond.notify_one();
			}

			bool& m_flag;
			boost::condition& m_cond;
			boost::mutex& m_monitor;
		};
	
	}

	void piece_manager::impl::allocate_slots(int num_slots)
	{
		assert(num_slots > 0);

		// this object will syncronize the allocation with
		// potential other threads
		allocation_syncronization sync_obj(
			m_allocating
			, m_allocating_condition
			, m_allocating_monitor);

		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

		INVARIANT_CHECK;

		assert(!m_unallocated_slots.empty());
		
		const int piece_size = static_cast<int>(m_info.piece_length());

		std::vector<char> zeros(piece_size, 0);

		for (int i = 0;
			i < num_slots && !m_unallocated_slots.empty();
			++i)
		{
			int pos = m_unallocated_slots.front();
//			int piece_pos = pos;

			int new_free_slot = pos;
			if (m_piece_to_slot[pos] != has_no_slot)
			{
				assert(m_piece_to_slot[pos] >= 0);
				m_storage.read(&zeros[0], m_piece_to_slot[pos], 0, static_cast<int>(m_info.piece_size(pos)));
				new_free_slot = m_piece_to_slot[pos];
				m_slot_to_piece[pos] = pos;
				m_piece_to_slot[pos] = pos;
			}
			m_unallocated_slots.erase(m_unallocated_slots.begin());
			m_slot_to_piece[new_free_slot] = unassigned;
			m_free_slots.push_back(new_free_slot);

			m_storage.write(&zeros[0], pos, 0, static_cast<int>(m_info.piece_size(pos)));
		}

		assert(m_free_slots.size() > 0);
	}

	void piece_manager::allocate_slots(int num_slots)
	{
		m_pimpl->allocate_slots(num_slots);
	}

	path const& piece_manager::save_path() const
	{
		return m_pimpl->save_path();
	}

	bool piece_manager::move_storage(path const& save_path)
	{
		return m_pimpl->move_storage(save_path);
	}

#ifndef NDEBUG
	void piece_manager::impl::check_invariant() const
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------
		if (m_piece_to_slot.empty()) return;

		assert((int)m_piece_to_slot.size() == m_info.num_pieces());
		assert((int)m_slot_to_piece.size() == m_info.num_pieces());

		for (std::vector<int>::const_iterator i = m_free_slots.begin();
			i != m_free_slots.end();
			++i)
		{
			assert(*i < (int)m_slot_to_piece.size());
			assert(*i >= 0);
			assert(m_slot_to_piece[*i] == unassigned);
		}

		for (std::vector<int>::const_iterator i = m_unallocated_slots.begin();
			i != m_unallocated_slots.end();
			++i)
		{
			assert(*i < (int)m_slot_to_piece.size());
			assert(*i >= 0);
			assert(m_slot_to_piece[*i] == unallocated);
		}

		for (int i = 0; i < m_info.num_pieces(); ++i)
		{
			// Check domain of piece_to_slot's elements
			if (m_piece_to_slot[i] != has_no_slot)
			{
				assert(m_piece_to_slot[i] >= 0);
				assert(m_piece_to_slot[i] < (int)m_slot_to_piece.size());
			}

			// Check domain of slot_to_piece's elements
			if (m_slot_to_piece[i] != unallocated
				&& m_slot_to_piece[i] != unassigned)
			{
				assert(m_slot_to_piece[i] >= 0);
				assert(m_slot_to_piece[i] < (int)m_piece_to_slot.size());
			}

			// do more detailed checks on piece_to_slot
			if (m_piece_to_slot[i] >= 0)
			{
				assert(m_slot_to_piece[m_piece_to_slot[i]] == i);
				if (m_piece_to_slot[i] != i)
				{
					assert(m_slot_to_piece[i] == unallocated);
				}
			}
			else
			{
				assert(m_piece_to_slot[i] == has_no_slot);
			}

			// do more detailed checks on slot_to_piece

			if (m_slot_to_piece[i] >= 0)
			{
				assert(m_slot_to_piece[i] < (int)m_piece_to_slot.size());
				assert(m_piece_to_slot[m_slot_to_piece[i]] == i);
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
				assert(m_unallocated_slots.empty()
					|| (std::find(
						m_unallocated_slots.begin()
						, m_unallocated_slots.end()
						, i) != m_unallocated_slots.end())
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

