/*

Copyright (c) 2007, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/pch.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/file.hpp"
#include <set>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/utility.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#if BOOST_VERSION >= 103500
#include <boost/system/error_code.hpp>
#endif

#include <boost/version.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

using boost::iostreams::mapped_file;
using boost::iostreams::mapped_file_params;

namespace libtorrent
{
	namespace fs = boost::filesystem;
#if BOOST_VERSION >= 103500
	typedef boost::system::error_code ec_t;
#else
	typedef error_code ec_t;
#endif

	struct mapped_file_pool
	{
		mapped_file_pool(int size = 40): m_size(size) {}

	private:
	
		enum { view_size = 100 * 1024 * 1024 };
		int m_size;

		struct file_entry
		{
			file_entry() : key(0), references(0) {}
			bool open(fs::path const& path, std::ios::openmode openmode
				, size_type start, size_type size, void* key_, size_type file_size = 0)
			{
#ifndef NDEBUG
				if (file_size > 0)
				{
					ec_t ec;
					fs::file_status st = fs::status(path, ec);
					TORRENT_ASSERT(!fs::exists(st));
				}
#endif
				key = key_;
				last_use = time_now();
				params.path = path.string();
				params.mode = openmode;
				params.offset = start;
				params.length = size;
				params.new_file_size = file_size;
				file.open(params);
				return file.is_open();
			}
			mapped_file_params params;
			mapped_file file;
			void* key;
			ptime last_use;
			int references;
		};

		typedef std::list<file_entry> files_t;
		files_t m_files;

	public:

		struct file_view
		{
			explicit file_view(file_entry* e): m_entry(e) { ++m_entry->references; }
			file_view(): m_entry(0) {}
			file_view(file_view const& f): m_entry(f.m_entry)
			{ if (m_entry) ++m_entry->references; }
			~file_view()
			{
				TORRENT_ASSERT(m_entry == 0 || m_entry->references > 0);
				if (m_entry) --m_entry->references;
			}
			file_view& operator=(file_view const& v)
			{
				TORRENT_ASSERT(m_entry == 0 || m_entry->references > 0);
				if (m_entry) --m_entry->references;
				m_entry = v.m_entry;
				if (m_entry) ++m_entry->references;
				return *this;
			}

			bool valid() const { return m_entry && m_entry->file.const_data(); }

			char* addr() const
			{
				TORRENT_ASSERT(m_entry);
				return m_entry->file.data();
			}

			char const* const_addr() const
			{
				TORRENT_ASSERT(m_entry);
				return m_entry->file.const_data();
			}

			size_type offset() const
			{
				TORRENT_ASSERT(m_entry);
				return m_entry->params.offset;
			}

			size_type size() const
			{
				TORRENT_ASSERT(m_entry);
				return m_entry->params.length;
			}

			private:
				file_entry* m_entry;
		};

		file_view open_file(fs::path const& p, std::ios::openmode mode
			, size_type offset, size_type length, void* key
			, size_type file_size)
		{
			TORRENT_ASSERT(file_size > 0);
			files_t::iterator min = m_files.end();
			for (std::list<file_entry>::iterator i = m_files.begin()
				, end(m_files.end()); i != end; ++i)
			{
				if (i->params.path == p.string()
					&& i->params.offset <= offset
					&& i->params.offset + i->params.length >= offset + length)
				{
					if (i->key != key) return file_view();
					if ((mode & std::ios::out) && (i->params.mode & std::ios::out) == 0)
					{
						TORRENT_ASSERT(i->references == 0);
						i->file.close();
						m_files.erase(i);
						min = m_files.end();
						break;
					}
					i->last_use = time_now();
					return file_view(&(*i));
				}
				if ((min == m_files.end() || i->last_use < min->last_use)
					&& i->references == 0)
				{
					min = i;
				}
			}

			if (int(m_files.size()) >= m_size && min != m_files.end())
			{
				TORRENT_ASSERT(min->references == 0);
				min->file.close();
				m_files.erase(min);
			}

			size_type start = (offset / view_size) * view_size;
			TORRENT_ASSERT(start + view_size >= offset + length);

#if BOOST_VERSION < 103500
			fs::system_error_type ec;
#else
			ec_t ec;
#endif
			fs::file_status st = fs::status(p, ec);
			
			m_files.push_back(file_entry());
			bool ret = false;
			if (!exists(st))
			{
				ret = m_files.back().open(p, mode | std::ios::out, start, view_size, key, file_size);
			}
			else
			{
				if (is_directory(st)) return file_view();
				size_type s = fs::file_size(p);
#ifdef WIN32
				// TODO: SetFileSize()
				if (s < file_size) {}
#else
				if (s < file_size) truncate(p.string().c_str(), file_size);
#endif
				ret = m_files.back().open(p, mode, start, view_size, key);
			}
			
			
			if (!ret)
			{
				m_files.erase(boost::prior(m_files.end()));
				return file_view();
			}
			return file_view(&m_files.back());
		}

		void release(void* key)
		{
			for (std::list<file_entry>::iterator i = m_files.begin();
				!m_files.empty() && i != m_files.end();)
			{
				if (i->key == key)
				{
					TORRENT_ASSERT(i->references == 0);
					i->file.close();
					m_files.erase(i++);
					continue;
				}
				++i;
			}
		}
	
	};

	
	struct mapped_storage: storage_interface
	{
		mapped_storage(file_storage const& fs, fs::path save_path)
			: m_files(fs)
			, m_save_path(save_path)
		{}

		bool initialize(bool allocate_files) { return false; }

		int read(char* buf, int slot, int offset, int size)
		{
			TORRENT_ASSERT(buf != 0);
			TORRENT_ASSERT(slot >= 0 && slot < m_files.num_pieces());
			TORRENT_ASSERT(offset >= 0);
			TORRENT_ASSERT(offset < m_files.piece_size(slot));
			TORRENT_ASSERT(size > 0);

			size_type result = -1;
			try
			{

#ifndef NDEBUG
			std::vector<file_slice> slices
				= files().map_block(slot, offset, size);
			TORRENT_ASSERT(!slices.empty());
#endif
			size_type start = slot * (size_type)m_files.piece_length() + offset;
			TORRENT_ASSERT(start + size <= m_files.total_size());

			// find the file iterator and file offset
			size_type file_offset = start;
			std::vector<file_entry>::const_iterator file_iter;

			for (file_iter = files().begin();;)
			{
				if (file_offset < file_iter->size)
					break;

				file_offset -= file_iter->size;
				++file_iter;
			}

			TORRENT_ASSERT(file_iter->size > 0);
			mapped_file_pool::file_view view = m_pool.open_file(
				m_save_path / file_iter->path, std::ios::in
				, file_offset + file_iter->file_base, size, this
				, file_iter->size + file_iter->file_base);

			if (!view.valid())
			{
				set_error((m_save_path / file_iter->path).string(), "failed to open file for reading");
				return -1;
			}
			TORRENT_ASSERT(view.const_addr() != 0);

			int left_to_read = size;
			int buf_pos = 0;
			result = left_to_read;
#ifndef NDEBUG
			int counter = 0;
#endif
			while (left_to_read > 0)
			{
				int read_bytes = left_to_read;
				if (file_offset + read_bytes > file_iter->size)
					read_bytes = static_cast<int>(file_iter->size - file_offset);

				if (read_bytes > 0)
				{
#ifndef NDEBUG
					TORRENT_ASSERT(int(slices.size()) > counter);
					size_type slice_size = slices[counter].size;
					TORRENT_ASSERT(slice_size == read_bytes);
					TORRENT_ASSERT(files().at(slices[counter].file_index).path
						== file_iter->path);
#endif

					TORRENT_ASSERT(file_offset + file_iter->file_base >= view.offset());
					TORRENT_ASSERT(view.const_addr() != 0);
					std::memcpy(buf + buf_pos
						, view.const_addr() + (file_offset + file_iter->file_base - view.offset())
						, read_bytes);

					left_to_read -= read_bytes;
					buf_pos += read_bytes;
					TORRENT_ASSERT(buf_pos >= 0);
					file_offset += read_bytes;
				}

				if (left_to_read > 0)
				{
					++file_iter;
					// skip empty files
					while (file_iter != files().end() && file_iter->size == 0)
						++file_iter;

#ifndef NDEBUG
					// empty files are not returned by map_block, so if
					// this file was empty, don't increment the slice counter
					if (read_bytes > 0) ++counter;
#endif
					fs::path path = m_save_path / file_iter->path;

					file_offset = 0;

					view = m_pool.open_file(path, std::ios::in, file_offset + file_iter->file_base
						, left_to_read, this
						, file_iter->size + file_iter->file_base);

					if (!view.valid())
					{
						set_error((m_save_path / file_iter->path).string(), "failed to open for reading");
						return -1;
					}
					TORRENT_ASSERT(view.const_addr() != 0);
				}
			}
			}
			catch (std::exception& e)
			{
				set_error("", e.what());
				return -1;
			}
	
			return result;
		}

		int write(const char* buf, int slot, int offset, int size)
		{
			TORRENT_ASSERT(buf != 0);
			TORRENT_ASSERT(slot >= 0 && slot < m_files.num_pieces());
			TORRENT_ASSERT(offset >= 0);
			TORRENT_ASSERT(offset < m_files.piece_size(slot));
			TORRENT_ASSERT(size > 0);

#ifndef NDEBUG
			std::vector<file_slice> slices
				= files().map_block(slot, offset, size);
			TORRENT_ASSERT(!slices.empty());
#endif
			size_type start = slot * (size_type)m_files.piece_length() + offset;
			TORRENT_ASSERT(start + size <= m_files.total_size());

			// find the file iterator and file offset
			size_type file_offset = start;
			std::vector<file_entry>::const_iterator file_iter;

			for (file_iter = files().begin();;)
			{
				if (file_offset < file_iter->size)
					break;

				file_offset -= file_iter->size;
				++file_iter;
			}

			TORRENT_ASSERT(file_iter->size > 0);
			try
			{

			mapped_file_pool::file_view view = m_pool.open_file(
				m_save_path / file_iter->path, std::ios::in | std::ios::out
				, file_offset + file_iter->file_base, size, this
				, file_iter->size + file_iter->file_base);
		
			if (!view.valid())
			{
				set_error((m_save_path / file_iter->path).string(), "failed to open file for writing");
				return -1;
			}
			TORRENT_ASSERT(view.addr() != 0);

			int left_to_write = size;
			int buf_pos = 0;
#ifndef NDEBUG
			int counter = 0;
#endif
			while (left_to_write > 0)
			{
				int write_bytes = left_to_write;
				if (file_offset + write_bytes > file_iter->size)
					write_bytes = static_cast<int>(file_iter->size - file_offset);

				if (write_bytes > 0)
				{
#ifndef NDEBUG
					TORRENT_ASSERT(int(slices.size()) > counter);
					size_type slice_size = slices[counter].size;
					TORRENT_ASSERT(slice_size == write_bytes);
					TORRENT_ASSERT(files().at(slices[counter].file_index).path
						== file_iter->path);
#endif

					TORRENT_ASSERT(file_offset + file_iter->file_base >= view.offset());
					TORRENT_ASSERT(view.addr() != 0);
					std::memcpy(view.addr() + (file_offset + file_iter->file_base - view.offset())
						, buf + buf_pos
						, write_bytes);

					left_to_write -= write_bytes;
					buf_pos += write_bytes;
					TORRENT_ASSERT(buf_pos >= 0);
					file_offset += write_bytes;
				}

				if (left_to_write > 0)
				{
					++file_iter;
					while (file_iter != files().end() && file_iter->size == 0)
						++file_iter;
#ifndef NDEBUG
					// empty files are not returned by map_block, so if
					// this file was empty, don't increment the slice counter
					if (write_bytes > 0) ++counter;
#endif
					fs::path path = m_save_path / file_iter->path;

					file_offset = 0;
					view = m_pool.open_file(path, std::ios::in | std::ios::out
						, file_offset + file_iter->file_base, left_to_write, this
						, file_iter->size + file_iter->file_base);

					if (!view.valid())
					{
						set_error((m_save_path / file_iter->path).string(), "failed to open file for reading");
						return -1;
					}
					TORRENT_ASSERT(view.addr() != 0);
				}
			}
			}
			catch (std::exception& e)
			{
				set_error((m_save_path / file_iter->path).string(), e.what());
				return -1;
			}
			return size;
		}

		bool move_storage(fs::path save_path)
		{
#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION >= 103400
			fs::wpath old_path;
			fs::wpath new_path;
#else
			fs::path old_path;
			fs::path new_path;
#endif

			save_path = complete(save_path);

#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION < 103400
			std::wstring wsave_path(safe_convert(save_path.native_file_string()));
			if (!exists_win(save_path))
				CreateDirectory(wsave_path.c_str(), 0);
			else if ((GetFileAttributes(wsave_path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) == 0)
				return false;
#elif defined(_WIN32) && defined(UNICODE)
			fs::wpath wp = safe_convert(save_path.string());
			if (!exists(wp))
				create_directory(wp);
			else if (!is_directory(wp))
				return false;
#else
			if (!exists(save_path))
				create_directory(save_path);
			else if (!is_directory(save_path))
				return false;
#endif

			m_pool.release(this);

#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION >= 103400
			old_path = safe_convert((m_save_path / files().name()).string());
			new_path = safe_convert((save_path / files().name()).string());
#else
			old_path = m_save_path / files().name();
			new_path = save_path / files().name();
#endif

			try
			{
#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION < 103400
				rename_win(old_path, new_path);
				rename(old_path, new_path);
#else
				rename(old_path, new_path);
#endif
				m_save_path = save_path;
				return true;
			}
			catch (std::exception& e)
			{
#ifndef NDEBUG
				std::cerr << "ERROR: " << e.what() << std::endl;
#endif
			}
			return false;
		}

		bool verify_resume_data(lazy_entry const& rd, std::string& error)
		{
			if (rd.type() != lazy_entry::dict_t)
			{
				error = "invalid fastresume file (not a dictionary)";
				return true;
			}

			std::vector<std::pair<size_type, std::time_t> > file_sizes;
			lazy_entry const* file_sizes_ent = rd.dict_find_list("file sizes");
			if (file_sizes_ent == 0)
			{
				error = "missing or invalid 'file sizes' entry in resume data";
				return false;
			}

			for (int i = 0; i < file_sizes_ent->list_size(); ++i)
			{
				lazy_entry const* e = file_sizes_ent->list_at(i);
				if (e->type() != lazy_entry::list_t
					|| e->list_size() != 2
					|| e->list_at(0)->type() != lazy_entry::int_t
					|| e->list_at(1)->type() != lazy_entry::int_t)
					continue;
				file_sizes.push_back(std::pair<size_type, std::time_t>(
					e->list_int_value_at(0), std::time_t(e->list_int_value_at(1))));
			}

			if (file_sizes.empty())
			{
				error = "the number of files in resume data is 0";
				return false;
			}

			lazy_entry const* slots = rd.dict_find_list("slots");
			if (slots == 0)
			{
				error = "missing or invalid 'slots' entry in resume data";
				return false;
			}

			bool seed = false;

			if (int(slots->list_size()) == m_files.num_pieces())
			{
				bool seed = true;
				for (int i = 0; i < slots->list_size(); ++i)
				{
					lazy_entry const* e = slots->list_at(i);
					if (e->list_int_value_at(i, -1) >= 0) continue;
					seed = false;
					break;
				}
			}

			bool full_allocation_mode = false;
			if (rd.dict_find_string_value("allocation") == "full")
				full_allocation_mode = true;

			if (seed)
			{
				if (files().num_files() != (int)file_sizes.size())
				{
					error = "the number of files does not match the torrent (num: "
						+ boost::lexical_cast<std::string>(file_sizes.size()) + " actual: "
						+ boost::lexical_cast<std::string>(files().num_files()) + ")";
					return false;
				}

				std::vector<std::pair<size_type, std::time_t> >::iterator
					fs = file_sizes.begin();
				// the resume data says we have the entire torrent
				// make sure the file sizes are the right ones
				for (file_storage::iterator i = files().begin()
					, end(files().end()); i != end; ++i, ++fs)
				{
					if (i->size != fs->first)
					{
						error = "file size for '" + i->path.native_file_string()
							+ "' was expected to be "
							+ boost::lexical_cast<std::string>(i->size) + " bytes";
						return false;
					}
				}
			}

			return match_filesizes(files(), m_save_path, file_sizes
				, !full_allocation_mode, &error);
		}

		bool write_resume_data(entry& rd) const
		{
			if (rd.type() != entry::dictionary_t)
			{
				set_error("", "invalid fastresume file");
				return true;
			}
			std::vector<std::pair<size_type, std::time_t> > file_sizes
				= get_filesizes(m_files, m_save_path);

			entry::list_type& fl = rd["file sizes"].list();
			for (std::vector<std::pair<size_type, std::time_t> >::iterator i
				= file_sizes.begin(), end(file_sizes.end()); i != end; ++i)
			{
				entry::list_type p;
				p.push_back(entry(i->first));
				p.push_back(entry(i->second));
				fl.push_back(entry(p));
			}
			return false;
		}

		bool move_slot(int src_slot, int dst_slot)
		{
			// TODO: this can be optimized by mapping both slots and do a straight memcpy
			int piece_size = m_files.piece_size(dst_slot);
			m_scratch_buffer.resize(piece_size);
			size_type ret1 = read(&m_scratch_buffer[0], src_slot, 0, piece_size);
			size_type ret2 = write(&m_scratch_buffer[0], dst_slot, 0, piece_size);
			return ret1 != piece_size || ret2 != piece_size;
		}

		bool swap_slots(int slot1, int slot2)
		{
			// TODO: this can be optimized by mapping both slots and do a straight memcpy
			// the size of the target slot is the size of the piece
			int piece_size = m_files.piece_length();
			int piece1_size = m_files.piece_size(slot2);
			int piece2_size = m_files.piece_size(slot1);
			m_scratch_buffer.resize(piece_size * 2);
			size_type ret1 = read(&m_scratch_buffer[0], slot1, 0, piece1_size);
			size_type ret2 = read(&m_scratch_buffer[piece_size], slot2, 0, piece2_size);
			size_type ret3 = write(&m_scratch_buffer[0], slot2, 0, piece1_size);
			size_type ret4 = write(&m_scratch_buffer[piece_size], slot1, 0, piece2_size);
			return ret1 != piece1_size || ret2 != piece2_size
				|| ret3 != piece1_size || ret4 != piece2_size;
		}

		bool swap_slots3(int slot1, int slot2, int slot3)
		{
			// TODO: this can be optimized by mapping both slots and do a straight memcpy
			// the size of the target slot is the size of the piece
			int piece_size = m_files.piece_length();
			int piece1_size = m_files.piece_size(slot2);
			int piece2_size = m_files.piece_size(slot3);
			int piece3_size = m_files.piece_size(slot1);
			m_scratch_buffer.resize(piece_size * 2);
			size_type ret1 = read(&m_scratch_buffer[0], slot1, 0, piece1_size);
			size_type ret2 = read(&m_scratch_buffer[piece_size], slot2, 0, piece2_size);
			size_type ret3 = write(&m_scratch_buffer[0], slot2, 0, piece1_size);
			size_type ret4 = read(&m_scratch_buffer[0], slot3, 0, piece3_size);
			size_type ret5 = write(&m_scratch_buffer[piece_size], slot3, 0, piece2_size);
			size_type ret6 = write(&m_scratch_buffer[0], slot1, 0, piece3_size);
			return ret1 != piece1_size || ret2 != piece2_size
				|| ret3 != piece1_size || ret4 != piece3_size
				|| ret5 != piece2_size || ret6 != piece3_size;
		}

		sha1_hash hash_for_slot(int slot, partial_hash& ph, int piece_size)
		{
#ifndef NDEBUG
			hasher partial;
			hasher whole;
			int slot_size1 = piece_size;
			m_scratch_buffer.resize(slot_size1);
			read(&m_scratch_buffer[0], slot, 0, slot_size1);
			if (ph.offset > 0)
				partial.update(&m_scratch_buffer[0], ph.offset);
			whole.update(&m_scratch_buffer[0], slot_size1);
			hasher partial_copy = ph.h;
			TORRENT_ASSERT(ph.offset == 0 || partial_copy.final() == partial.final());
#endif
			int slot_size = piece_size - ph.offset;
			if (slot_size > 0)
			{
				m_scratch_buffer.resize(slot_size);
				read(&m_scratch_buffer[0], slot, ph.offset, slot_size);
				ph.h.update(&m_scratch_buffer[0], slot_size);
			}
#ifndef NDEBUG
			sha1_hash ret = ph.h.final();
			TORRENT_ASSERT(ret == whole.final());
			return ret;
#else
			return ph.h.final();
#endif
		}

		bool rename_file(int index, std::string const& new_filename)
		{
			if (index < 0 || index >= m_files.num_files()) return true;
			fs::path old_name = m_save_path / files().at(index).path;
			m_pool.release(this);

#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION >= 103400
			fs::wpath old_path = safe_convert(old_name.string());
			fs::wpath new_path = safe_convert((m_save_path / new_filename).string());
#else
			fs::path const& old_path = old_name;
			fs::path new_path = m_save_path / new_filename;
#endif

#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
				rename(old_path, new_path);
				if (!m_mapped_files)
				{ m_mapped_files.reset(new file_storage(m_files)); }
				m_mapped_files->rename_file(index, new_filename);
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e)
			{
				set_error(old_name.string(), e.what());
				return true;
			}
#endif
			return false;
		}

		bool release_files()
		{
			m_pool.release(this);
			return false;
		}

		bool delete_files()
		{
			// make sure we don't have the files open
			m_pool.release(this);
			buffer().swap(m_scratch_buffer);

			int result = 0;
			std::string error;
			std::string error_file;

			// delete the files from disk
			std::set<std::string> directories;
			typedef std::set<std::string>::iterator iter_t;
			for (file_storage::iterator i = m_files.begin()
				, end(m_files.end()); i != end; ++i)
			{
				std::string p = (m_save_path / i->path).string();
				fs::path bp = i->path.branch_path();
				std::pair<iter_t, bool> ret;
				ret.second = true;
				while (ret.second && !bp.empty())
				{
					std::pair<iter_t, bool> ret = directories.insert((m_save_path / bp).string());
					bp = bp.branch_path();
				}
				if (std::remove(p.c_str()) != 0 && errno != ENOENT)
				{
					error = std::strerror(errno);
					error_file = p;
					result = errno;
				}
			}

			// remove the directories. Reverse order to delete
			// subdirectories first

			for (std::set<std::string>::reverse_iterator i = directories.rbegin()
				, end(directories.rend()); i != end; ++i)
			{
				if (std::remove(i->c_str()) != 0 && errno != ENOENT)
				{
					error = std::strerror(errno);
					error_file = *i;
					result = errno;
				}
			}

			if (!error.empty()) set_error(error_file, error);
			return result != 0;
		}

	private:

		file_storage const& files() const { return m_mapped_files?*m_mapped_files:m_files; }

		boost::scoped_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;
		fs::path m_save_path;

		// temporary storage for moving pieces
		buffer m_scratch_buffer;

		static mapped_file_pool m_pool;
	};

	storage_interface* mapped_storage_constructor(file_storage const& fs
		, fs::path const& path, file_pool& fp)
	{
		return new mapped_storage(fs, path);
	}

	mapped_file_pool mapped_storage::m_pool;

}

