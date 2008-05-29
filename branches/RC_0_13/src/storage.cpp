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

#include "libtorrent/pch.hpp"

#include <ctime>
#include <iterator>
#include <algorithm>
#include <set>
#include <functional>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/version.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

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
#include "libtorrent/file_pool.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#ifndef NDEBUG
#include <ios>
#include <iostream>
#include <iomanip>
#include <cstdio>
#endif

#if defined(__APPLE__)
// for getattrlist()
#include <sys/attr.h>
#include <unistd.h>
// for statfs()
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if defined(__linux__)
#include <sys/statfs.h>
#endif

#if defined(__FreeBSD__)
// for statfs()
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if defined(_WIN32) && defined(UNICODE)

#include <windows.h>
#include <boost/filesystem/exception.hpp>
#include "libtorrent/utf8.hpp"
#include "libtorrent/buffer.hpp"

namespace libtorrent
{
	std::wstring safe_convert(std::string const& s)
	{
		try
		{
			return libtorrent::utf8_wchar(s);
		}
		catch (std::exception)
		{
			std::wstring ret;
			const char* end = &s[0] + s.size();
			for (const char* i = &s[0]; i < end;)
			{
				wchar_t c = '.';
				int result = std::mbtowc(&c, i, end - i);
				if (result > 0) i += result;
				else ++i;
				ret += c;
			}
			return ret;
		}
	}
}
#endif

#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION < 103400
namespace
{
	using libtorrent::safe_convert;
	using namespace boost::filesystem;
	
	// based on code from Boost.Fileystem
	bool create_directories_win(const fs::path& ph)
	{
		if (ph.empty() || exists(ph))
		{
			if ( !ph.empty() && !is_directory(ph) )
				boost::throw_exception( filesystem_error(
					"boost::filesystem::create_directories",
					ph, "path exists and is not a directory",
					not_directory_error ) );
			return false;
		}

		// First create branch, by calling ourself recursively
		create_directories_win(ph.branch_path());
		// Now that parent's path exists, create the directory
		std::wstring wph(safe_convert(ph.native_directory_string()));
		CreateDirectory(wph.c_str(), 0);
		return true;
	}

	bool exists_win( const fs::path & ph )
	{
		std::wstring wpath(safe_convert(ph.string()));
		if(::GetFileAttributes( wpath.c_str() ) == 0xFFFFFFFF)
		{
			UINT err = ::GetLastError();
			if((err == ERROR_FILE_NOT_FOUND)
				|| (err == ERROR_INVALID_PARAMETER)
				|| (err == ERROR_NOT_READY)
				|| (err == ERROR_PATH_NOT_FOUND)
				|| (err == ERROR_INVALID_NAME)
				|| (err == ERROR_BAD_NETPATH ))
			return false; // GetFileAttributes failed because the path does not exist
			// for any other error we assume the file does exist and fall through,
			// this may not be the best policy though...  (JM 20040330)
			return true;
		}
		return true;
	}

	boost::intmax_t file_size_win( const fs::path & ph )
	{
		std::wstring wpath(safe_convert(ph.string()));
		// by now, intmax_t is 64-bits on all Windows compilers
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if ( !::GetFileAttributesExW( wpath.c_str(),
					::GetFileExInfoStandard, &fad ) )
			boost::throw_exception( filesystem_error(
				"boost::filesystem::file_size",
				ph, detail::system_error_code() ) );
		if ( (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) !=0 )
			boost::throw_exception( filesystem_error(
				"boost::filesystem::file_size",
				ph, "invalid: is a directory",
				is_directory_error ) ); 
		return (static_cast<boost::intmax_t>(fad.nFileSizeHigh)
			<< (sizeof(fad.nFileSizeLow)*8))
			+ fad.nFileSizeLow;
	}
	
	std::time_t last_write_time_win( const fs::path & ph )
	{
		struct _stat path_stat;
		std::wstring wph(safe_convert(ph.native_file_string()));
		if ( ::_wstat( wph.c_str(), &path_stat ) != 0 )
			boost::throw_exception( filesystem_error(
			"boost::filesystem::last_write_time",
			ph, detail::system_error_code() ) );
		return path_stat.st_mtime;
	}

	void rename_win( const fs::path & old_path,
		const fs::path & new_path )
	{
		std::wstring wold_path(safe_convert(old_path.string()));
		std::wstring wnew_path(safe_convert(new_path.string()));
		if ( !::MoveFile( wold_path.c_str(), wnew_path.c_str() ) )
		boost::throw_exception( filesystem_error(
			"boost::filesystem::rename",
			old_path, new_path, detail::system_error_code() ) );
	}

} // anonymous namespace

#endif

#if BOOST_VERSION < 103200
bool operator<(fs::path const& lhs, fs::path const& rhs)
{
	return lhs.string() < rhs.string();
}
#endif

namespace fs = boost::filesystem;
using boost::bind;
using namespace ::boost::multi_index;
using boost::multi_index::multi_index_container;

#if !defined(NDEBUG) && defined(TORRENT_STORAGE_DEBUG)
namespace
{
	using namespace libtorrent;

	void print_to_log(const std::string& s)
	{
		static std::ofstream log("log.txt");
		log << s;
		log.flush();
	}
}
#endif

namespace libtorrent
{

	std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		torrent_info const& t, fs::path p)
	{
		p = complete(p);
		std::vector<std::pair<size_type, std::time_t> > sizes;
		for (torrent_info::file_iterator i = t.begin_files(true);
			i != t.end_files(true); ++i)
		{
			size_type size = 0;
			std::time_t time = 0;
			try
			{
#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION < 103400
				fs::path f = p / i->path;
				size = file_size_win(f);
				time = last_write_time_win(f);
#elif defined(_WIN32) && defined(UNICODE)
				fs::wpath f = safe_convert((p / i->path).string());
				size = file_size(f);
				time = last_write_time(f);
#else
				fs::path f = p / i->path;
				size = file_size(f);
				time = last_write_time(f);
#endif
			}
			catch (std::exception&) {}
			sizes.push_back(std::make_pair(size, time));
		}
		return sizes;
	}

	// matches the sizes and timestamps of the files passed in
	// in non-compact mode, actual file sizes and timestamps
	// are allowed to be bigger and more recent than the fast
	// resume data. This is because full allocation will not move
	// pieces, so any older version of the resume data will
	// still be a correct subset of the actual data on disk.
	bool match_filesizes(
		torrent_info const& t
		, fs::path p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, bool compact_mode
		, std::string* error)
	{
		if ((int)sizes.size() != t.num_files(true))
		{
			if (error) *error = "mismatching number of files";
			return false;
		}
		p = complete(p);

		std::vector<std::pair<size_type, std::time_t> >::const_iterator s
			= sizes.begin();
		for (torrent_info::file_iterator i = t.begin_files(true);
			i != t.end_files(true); ++i, ++s)
		{
			size_type size = 0;
			std::time_t time = 0;
			try
			{
#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION < 103400
				fs::path f = p / i->path;
				size = file_size_win(f);
				time = last_write_time_win(f);
#elif defined(_WIN32) && defined(UNICODE)
				fs::wpath f = safe_convert((p / i->path).string());
				size = file_size(f);
				time = last_write_time(f);
#else
				fs::path f = p / i->path;
				size = file_size(f);
				time = last_write_time(f);
#endif
			}
			catch (std::exception&) {}
			if ((compact_mode && size != s->first)
				|| (!compact_mode && size < s->first))
			{
				if (error) *error = "filesize mismatch for file '"
					+ i->path.native_file_string()
					+ "', size: " + boost::lexical_cast<std::string>(size)
					+ ", expected to be " + boost::lexical_cast<std::string>(s->first)
					+ " bytes";
				return false;
			}
			if ((compact_mode && time != s->second)
				|| (!compact_mode && time < s->second))
			{
				if (error) *error = "timestamp mismatch for file '"
					+ i->path.native_file_string()
					+ "', modification date: " + boost::lexical_cast<std::string>(time)
					+ ", expected to have modification date "
					+ boost::lexical_cast<std::string>(s->second);
				return false;
			}
		}
		return true;
	}

	class storage : public storage_interface, boost::noncopyable
	{
	public:
		storage(boost::intrusive_ptr<torrent_info const> info, fs::path const& path, file_pool& fp)
			: m_info(info)
			, m_files(fp)
		{
			TORRENT_ASSERT(info->begin_files(true) != info->end_files(true));
			m_save_path = fs::complete(path);
			TORRENT_ASSERT(m_save_path.is_complete());
		}

		void release_files();
		void delete_files();
		void initialize(bool allocate_files);
		bool move_storage(fs::path save_path);
		size_type read(char* buf, int slot, int offset, int size);
		void write(const char* buf, int slot, int offset, int size);
		void move_slot(int src_slot, int dst_slot);
		void swap_slots(int slot1, int slot2);
		void swap_slots3(int slot1, int slot2, int slot3);
		bool verify_resume_data(entry& rd, std::string& error);
		void write_resume_data(entry& rd) const;
		sha1_hash hash_for_slot(int slot, partial_hash& ph, int piece_size);

		size_type read_impl(char* buf, int slot, int offset, int size, bool fill_zero);

		~storage()
		{ m_files.release(this); }

		boost::intrusive_ptr<torrent_info const> m_info;
		fs::path m_save_path;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_files;
		
		// temporary storage for moving pieces
		buffer m_scratch_buffer;
	};

	sha1_hash storage::hash_for_slot(int slot, partial_hash& ph, int piece_size)
	{
#ifndef NDEBUG
		hasher partial;
		hasher whole;
		int slot_size1 = piece_size;
		m_scratch_buffer.resize(slot_size1);
		read_impl(&m_scratch_buffer[0], slot, 0, slot_size1, true);
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
			read_impl(&m_scratch_buffer[0], slot, ph.offset, slot_size, true);
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

	void storage::initialize(bool allocate_files)
	{
		// first, create all missing directories
		fs::path last_path;
		for (torrent_info::file_iterator file_iter = m_info->begin_files(true),
			end_iter = m_info->end_files(true); file_iter != end_iter; ++file_iter)
		{
			fs::path dir = (m_save_path / file_iter->path).branch_path();

			if (dir != last_path)
			{

#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION < 103400
				last_path = dir;
				if (!exists_win(last_path))
					create_directories_win(last_path);
#elif defined(_WIN32) && defined(UNICODE)
				last_path = dir;
				fs::wpath wp = safe_convert(last_path.string());
				if (!exists(wp))
					create_directories(wp);
#else
				last_path = dir;
				if (!exists(last_path))
					create_directories(last_path);
#endif
			}

			// if the file is empty, just create it. But also make sure
			// the directory exists.
			if (file_iter->size == 0)
			{
				try {
					file(m_save_path / file_iter->path, file::out);
				} catch (std::exception&) {}
				continue;
			}

			try {
				if (allocate_files)
				{
					m_files.open_file(this, m_save_path / file_iter->path, file::in | file::out)
						->set_size(file_iter->size);
				}
			} catch (std::exception&) {}
		}
		// close files that were opened in write mode
		m_files.release(this);
	}

	void storage::release_files()
	{
		m_files.release(this);
		buffer().swap(m_scratch_buffer);
	}

	void storage::delete_files()
	{
		// make sure we don't have the files open
		m_files.release(this);
		buffer().swap(m_scratch_buffer);

		std::string error;

		// delete the files from disk
		std::set<std::string> directories;
		typedef std::set<std::string>::iterator iter_t;
		for (torrent_info::file_iterator i = m_info->begin_files(true)
			, end(m_info->end_files(true)); i != end; ++i)
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
#if defined(_WIN32) && defined(UNICODE)
			try
			{ fs::remove(safe_convert(p)); }
			catch (std::exception& e)
			{ error = e.what(); }
#else
			if (std::remove(p.c_str()) != 0 && errno != ENOENT)
				error = std::strerror(errno);
#endif
		}

		// remove the directories. Reverse order to delete
		// subdirectories first

		for (std::set<std::string>::reverse_iterator i = directories.rbegin()
			, end(directories.rend()); i != end; ++i)
		{
#if defined(_WIN32) && defined(UNICODE)
			try
			{ fs::remove(safe_convert(*i)); }
			catch (std::exception& e)
			{ error = e.what(); }
#else
			if (std::remove(i->c_str()) != 0 && errno != ENOENT)
				error = std::strerror(errno);
#endif
		}

		if (!error.empty()) throw std::runtime_error(error);
	}

	void storage::write_resume_data(entry& rd) const
	{
		std::vector<std::pair<size_type, std::time_t> > file_sizes
			= get_filesizes(*m_info, m_save_path);

		rd["file sizes"] = entry::list_type();
		entry::list_type& fl = rd["file sizes"].list();
		for (std::vector<std::pair<size_type, std::time_t> >::iterator i
			= file_sizes.begin(), end(file_sizes.end()); i != end; ++i)
		{
			entry::list_type p;
			p.push_back(entry(i->first));
			p.push_back(entry(i->second));
			fl.push_back(entry(p));
		}
	}

	bool storage::verify_resume_data(entry& rd, std::string& error)
	{
		std::vector<std::pair<size_type, std::time_t> > file_sizes;
		entry::list_type& l = rd["file sizes"].list();

		for (entry::list_type::iterator i = l.begin();
			i != l.end(); ++i)
		{
			file_sizes.push_back(std::make_pair(
				i->list().front().integer()
				, std::time_t(i->list().back().integer())));
		}

		if (file_sizes.empty())
		{
			error = "the number of files in resume data is 0";
			return false;
		}

		entry::list_type& slots = rd["slots"].list();
		bool seed = int(slots.size()) == m_info->num_pieces()
			&& std::find_if(slots.begin(), slots.end()
				, boost::bind<bool>(std::less<int>()
				, boost::bind((size_type const& (entry::*)() const)
					&entry::integer, _1), 0)) == slots.end();

		bool full_allocation_mode = false;
		try
		{
			full_allocation_mode = rd["allocation"].string() == "full";
		}
		catch (std::exception&) {}

		if (seed)
		{
			if (m_info->num_files(true) != (int)file_sizes.size())
			{
				error = "the number of files does not match the torrent (num: "
					+ boost::lexical_cast<std::string>(file_sizes.size()) + " actual: "
					+ boost::lexical_cast<std::string>(m_info->num_files(true)) + ")";
				return false;
			}

			std::vector<std::pair<size_type, std::time_t> >::iterator
				fs = file_sizes.begin();
			// the resume data says we have the entire torrent
			// make sure the file sizes are the right ones
			for (torrent_info::file_iterator i = m_info->begin_files(true)
				, end(m_info->end_files(true)); i != end; ++i, ++fs)
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

		return match_filesizes(*m_info, m_save_path, file_sizes
			, !full_allocation_mode, &error);
	}

	// returns true on success
	bool storage::move_storage(fs::path save_path)
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

		m_files.release(this);

#if defined(_WIN32) && defined(UNICODE) && BOOST_VERSION >= 103400
		old_path = safe_convert((m_save_path / m_info->name()).string());
		new_path = safe_convert((save_path / m_info->name()).string());
#else
		old_path = m_save_path / m_info->name();
		new_path = save_path / m_info->name();
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
		catch (std::exception&) {}
		return false;
	}

#ifndef NDEBUG
/*
	void storage::shuffle()
	{
		int num_pieces = m_info->num_pieces();

		std::vector<int> pieces(num_pieces);
		for (std::vector<int>::iterator i = pieces.begin();
			i != pieces.end(); ++i)
		{
			*i = static_cast<int>(i - pieces.begin());
		}
		std::srand((unsigned int)std::time(0));
		std::vector<int> targets(pieces);
		std::random_shuffle(pieces.begin(), pieces.end());
		std::random_shuffle(targets.begin(), targets.end());

		for (int i = 0; i < (std::max)(num_pieces / 50, 1); ++i)
		{
			const int slot_index = targets[i];
			const int piece_index = pieces[i];
			const int slot_size =static_cast<int>(m_info->piece_size(slot_index));
			std::vector<char> buf(slot_size);
			read(&buf[0], piece_index, 0, slot_size);
			write(&buf[0], slot_index, 0, slot_size);
		}
	}
*/
#endif

	void storage::move_slot(int src_slot, int dst_slot)
	{
		int piece_size = m_info->piece_size(dst_slot);
		m_scratch_buffer.resize(piece_size);
		read_impl(&m_scratch_buffer[0], src_slot, 0, piece_size, true);
		write(&m_scratch_buffer[0], dst_slot, 0, piece_size);
	}

	void storage::swap_slots(int slot1, int slot2)
	{
		// the size of the target slot is the size of the piece
		int piece_size = m_info->piece_length();
		int piece1_size = m_info->piece_size(slot2);
		int piece2_size = m_info->piece_size(slot1);
		m_scratch_buffer.resize(piece_size * 2);
		read_impl(&m_scratch_buffer[0], slot1, 0, piece1_size, true);
		read_impl(&m_scratch_buffer[piece_size], slot2, 0, piece2_size, true);
		write(&m_scratch_buffer[0], slot2, 0, piece1_size);
		write(&m_scratch_buffer[piece_size], slot1, 0, piece2_size);
	}

	void storage::swap_slots3(int slot1, int slot2, int slot3)
	{
		// the size of the target slot is the size of the piece
		int piece_size = m_info->piece_length();
		int piece1_size = m_info->piece_size(slot2);
		int piece2_size = m_info->piece_size(slot3);
		int piece3_size = m_info->piece_size(slot1);
		m_scratch_buffer.resize(piece_size * 2);
		read_impl(&m_scratch_buffer[0], slot1, 0, piece1_size, true);
		read_impl(&m_scratch_buffer[piece_size], slot2, 0, piece2_size, true);
		write(&m_scratch_buffer[0], slot2, 0, piece1_size);
		read_impl(&m_scratch_buffer[0], slot3, 0, piece3_size, true);
		write(&m_scratch_buffer[piece_size], slot3, 0, piece2_size);
		write(&m_scratch_buffer[0], slot1, 0, piece3_size);
	}

	size_type storage::read(
		char* buf
		, int slot
		, int offset
		, int size)
	{
		return read_impl(buf, slot, offset, size, false);
	}

	size_type storage::read_impl(
		char* buf
		, int slot
		, int offset
		, int size
		, bool fill_zero)
	{
		TORRENT_ASSERT(buf != 0);
		TORRENT_ASSERT(slot >= 0 && slot < m_info->num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(offset < m_info->piece_size(slot));
		TORRENT_ASSERT(size > 0);

#ifndef NDEBUG
		std::vector<file_slice> slices
			= m_info->map_block(slot, offset, size, true);
		TORRENT_ASSERT(!slices.empty());
#endif

		size_type start = slot * (size_type)m_info->piece_length() + offset;
		TORRENT_ASSERT(start + size <= m_info->total_size());

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = m_info->begin_files(true);;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
		}

		int buf_pos = 0;
		boost::shared_ptr<file> in(m_files.open_file(
					this, m_save_path / file_iter->path, file::in));

		TORRENT_ASSERT(file_offset < file_iter->size);

		TORRENT_ASSERT(slices[0].offset == file_offset + file_iter->file_base);

		size_type new_pos = in->seek(file_offset + file_iter->file_base);
		if (new_pos != file_offset + file_iter->file_base)
		{
			// the file was not big enough
			if (!fill_zero)
				throw file_error("slot has no storage");
			std::memset(buf + buf_pos, 0, size - buf_pos);
			return size;
		}

#ifndef NDEBUG
		size_type in_tell = in->tell();
		TORRENT_ASSERT(in_tell == file_offset + file_iter->file_base);
#endif

		int left_to_read = size;
		int slot_size = static_cast<int>(m_info->piece_size(slot));

		if (offset + left_to_read > slot_size)
			left_to_read = slot_size - offset;

		TORRENT_ASSERT(left_to_read >= 0);

		size_type result = left_to_read;

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
				TORRENT_ASSERT(m_info->file_at(slices[counter].file_index, true).path
					== file_iter->path);
#endif

				int actual_read = int(in->read(buf + buf_pos, read_bytes));

				if (read_bytes != actual_read)
				{
					// the file was not big enough
					if (actual_read > 0) buf_pos += actual_read;
					if (!fill_zero)
						throw file_error("slot has no storage");
					std::memset(buf + buf_pos, 0, size - buf_pos);
					return size;
				}

				left_to_read -= read_bytes;
				buf_pos += read_bytes;
				TORRENT_ASSERT(buf_pos >= 0);
				file_offset += read_bytes;
			}

			if (left_to_read > 0)
			{
				++file_iter;
#ifndef NDEBUG
				// empty files are not returned by map_block, so if
				// this file was empty, don't increment the slice counter
				if (read_bytes > 0) ++counter;
#endif
				fs::path path = m_save_path / file_iter->path;

				file_offset = 0;
				in = m_files.open_file(
					this, path, file::in);
				in->seek(file_iter->file_base);
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
		TORRENT_ASSERT(buf != 0);
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_info->num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(size > 0);

#ifndef NDEBUG
		std::vector<file_slice> slices
			= m_info->map_block(slot, offset, size, true);
		TORRENT_ASSERT(!slices.empty());
#endif

		size_type start = slot * (size_type)m_info->piece_length() + offset;

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = m_info->begin_files(true);;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
			TORRENT_ASSERT(file_iter != m_info->end_files(true));
		}

		fs::path p(m_save_path / file_iter->path);
		boost::shared_ptr<file> out = m_files.open_file(
			this, p, file::out | file::in);

		TORRENT_ASSERT(file_offset < file_iter->size);
		TORRENT_ASSERT(slices[0].offset == file_offset + file_iter->file_base);

		size_type pos = out->seek(file_offset + file_iter->file_base);

		if (pos != file_offset + file_iter->file_base)
		{
			std::stringstream s;
			s << "no storage for slot " << slot;
			throw file_error(s.str());
		}

		int left_to_write = size;
		int slot_size = static_cast<int>(m_info->piece_size(slot));

		if (offset + left_to_write > slot_size)
			left_to_write = slot_size - offset;

		TORRENT_ASSERT(left_to_write >= 0);

		int buf_pos = 0;
#ifndef NDEBUG
		int counter = 0;
#endif
		while (left_to_write > 0)
		{
			int write_bytes = left_to_write;
			if (file_offset + write_bytes > file_iter->size)
			{
				TORRENT_ASSERT(file_iter->size >= file_offset);
				write_bytes = static_cast<int>(file_iter->size - file_offset);
			}

			if (write_bytes > 0)
			{
				TORRENT_ASSERT(int(slices.size()) > counter);
				TORRENT_ASSERT(slices[counter].size == write_bytes);
				TORRENT_ASSERT(m_info->file_at(slices[counter].file_index, true).path
					== file_iter->path);

				TORRENT_ASSERT(buf_pos >= 0);
				TORRENT_ASSERT(write_bytes >= 0);
				size_type written = out->write(buf + buf_pos, write_bytes);

				if (written != write_bytes)
				{
					std::stringstream s;
					s << "no storage for slot " << slot;
					throw file_error(s.str());
				}

				left_to_write -= write_bytes;
				buf_pos += write_bytes;
				TORRENT_ASSERT(buf_pos >= 0);
				file_offset += write_bytes;
				TORRENT_ASSERT(file_offset <= file_iter->size);
			}

			if (left_to_write > 0)
			{
#ifndef NDEBUG
				if (write_bytes > 0) ++counter;
#endif
				++file_iter;

				TORRENT_ASSERT(file_iter != m_info->end_files(true));
				fs::path p = m_save_path / file_iter->path;
				file_offset = 0;
				out = m_files.open_file(
					this, p, file::out | file::in);

				out->seek(file_iter->file_base);
			}
		}
	}

	storage_interface* default_storage_constructor(boost::intrusive_ptr<torrent_info const> ti
		, fs::path const& path, file_pool& fp)
	{
		return new storage(ti, path, fp);
	}

	// -- piece_manager -----------------------------------------------------

	piece_manager::piece_manager(
		boost::shared_ptr<void> const& torrent
		, boost::intrusive_ptr<torrent_info const> ti
		, fs::path const& save_path
		, file_pool& fp
		, disk_io_thread& io
		, storage_constructor_type sc)
		: m_storage(sc(ti, save_path, fp))
		, m_storage_mode(storage_mode_sparse)
		, m_info(ti)
		, m_save_path(complete(save_path))
		, m_state(state_none)
		, m_current_slot(0)
		, m_out_of_place(false)
		, m_scratch_piece(-1)
		, m_storage_constructor(sc)
		, m_io_thread(io)
		, m_torrent(torrent)
	{
#ifndef NDEBUG
		m_resume_data_verified = false;
#endif
	}

	piece_manager::~piece_manager()
	{
	}

	void piece_manager::write_resume_data(entry& rd) const
	{
		m_storage->write_resume_data(rd);
	}

	bool piece_manager::verify_resume_data(entry& rd, std::string& error)
	{
#ifndef NDEBUG
		m_resume_data_verified = true;
#endif
		return m_storage->verify_resume_data(rd, error);
	}

	void piece_manager::free_buffer(char* buf)
	{
		m_io_thread.free_buffer(buf);
	}

	void piece_manager::async_release_files(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::release_files;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_delete_files(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::delete_files;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_move_storage(fs::path const& p
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::move_storage;
		j.str = p.string();
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_read(
		peer_request const& r
		, boost::function<void(int, disk_io_job const&)> const& handler
		, char* buffer
		, int priority)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::read;
		j.piece = r.piece;
		j.offset = r.start;
		j.buffer_size = r.length;
		j.buffer = buffer;
		j.priority = priority;
		// if a buffer is not specified, only one block can be read
		// since that is the size of the pool allocator's buffers
		TORRENT_ASSERT(r.length <= 16 * 1024 || buffer != 0);
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_write(
		peer_request const& r
		, char const* buffer
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		TORRENT_ASSERT(r.length <= 16 * 1024);

		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::write;
		j.piece = r.piece;
		j.offset = r.start;
		j.buffer_size = r.length;
		j.buffer = m_io_thread.allocate_buffer();
		if (j.buffer == 0) throw file_error("out of memory");
		std::memcpy(j.buffer, buffer, j.buffer_size);
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_hash(int piece
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::hash;
		j.piece = piece;

		m_io_thread.add_job(j, handler);
	}

	fs::path piece_manager::save_path() const
	{
		boost::recursive_mutex::scoped_lock l(m_mutex);
		return m_save_path;
	}

	sha1_hash piece_manager::hash_for_piece_impl(int piece)
	{
		partial_hash ph;

		std::map<int, partial_hash>::iterator i = m_piece_hasher.find(piece);
		if (i != m_piece_hasher.end())
		{
			ph = i->second;
			m_piece_hasher.erase(i);
		}

		int slot = slot_for(piece);
		TORRENT_ASSERT(slot != has_no_slot);
		return m_storage->hash_for_slot(slot, ph, m_info->piece_size(piece));
	}

	bool piece_manager::move_storage_impl(fs::path const& save_path)
	{
		if (m_storage->move_storage(save_path))
		{
			m_save_path = fs::complete(save_path);
			return true;
		}
		return false;
	}

	void piece_manager::export_piece_map(
			std::vector<int>& p, std::vector<bool> const& have) const
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		INVARIANT_CHECK;

		if (m_storage_mode == storage_mode_compact)
		{
			p.clear();
			p.reserve(m_info->num_pieces());
			std::vector<int>::const_reverse_iterator last; 
			for (last = m_slot_to_piece.rbegin();
				last != m_slot_to_piece.rend(); ++last)
			{
				if (*last != unallocated) break;
			}

			for (std::vector<int>::const_iterator i =
				m_slot_to_piece.begin();
				i != last.base(); ++i)
			{
				p.push_back((*i >= 0) ? *i : unassigned);
			}
		}
		else
		{
			p.reserve(m_info->num_pieces());
			for (int i = 0; i < m_info->num_pieces(); ++i)
			{
				p.push_back(have[i] ? i : unassigned);
			}
		}
	}

	void piece_manager::mark_failed(int piece_index)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		INVARIANT_CHECK;

		if (m_storage_mode != storage_mode_compact) return;

		TORRENT_ASSERT(piece_index >= 0 && piece_index < (int)m_piece_to_slot.size());
		int slot_index = m_piece_to_slot[piece_index];
		TORRENT_ASSERT(slot_index >= 0);

		m_slot_to_piece[slot_index] = unassigned;
		m_piece_to_slot[piece_index] = has_no_slot;
		m_free_slots.push_back(slot_index);
	}

	unsigned long piece_manager::piece_crc(
		int slot_index
		, int block_size
		, piece_picker::block_info const* bi)
	try
	{
		TORRENT_ASSERT(slot_index >= 0);
		TORRENT_ASSERT(slot_index < m_info->num_pieces());
		TORRENT_ASSERT(block_size > 0);

		adler32_crc crc;
		std::vector<char> buf(block_size);
		int num_blocks = static_cast<int>(m_info->piece_size(slot_index)) / block_size;
		int last_block_size = static_cast<int>(m_info->piece_size(slot_index)) % block_size;
		if (last_block_size == 0) last_block_size = block_size;

		for (int i = 0; i < num_blocks-1; ++i)
		{
			if (bi[i].state != piece_picker::block_info::state_finished) continue;
			m_storage->read(
				&buf[0]
				, slot_index
				, i * block_size
				, block_size);
			crc.update(&buf[0], block_size);
		}
		if (num_blocks > 0 && bi[num_blocks - 1].state == piece_picker::block_info::state_finished)
		{
			m_storage->read(
				&buf[0]
				, slot_index
				, block_size * (num_blocks - 1)
				, last_block_size);
			crc.update(&buf[0], last_block_size);
		}
		return crc.final();
	}
	catch (std::exception&)
	{
		return 0;
	}

	size_type piece_manager::read_impl(
		char* buf
		, int piece_index
		, int offset
		, int size)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(size > 0);
		int slot = slot_for(piece_index);
		return m_storage->read(buf, slot, offset, size);
	}

	void piece_manager::write_impl(
		const char* buf
	  , int piece_index
	  , int offset
	  , int size)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(size > 0);
		TORRENT_ASSERT(piece_index >= 0 && piece_index < m_info->num_pieces());

		if (offset == 0)
		{
			partial_hash& ph = m_piece_hasher[piece_index];
			TORRENT_ASSERT(ph.offset == 0);
			ph.offset = size;
			ph.h.update(buf, size);
		}
		else
		{
			std::map<int, partial_hash>::iterator i = m_piece_hasher.find(piece_index);
			if (i != m_piece_hasher.end())
			{
				TORRENT_ASSERT(i->second.offset > 0);
				TORRENT_ASSERT(offset >= i->second.offset);
				if (offset == i->second.offset)
				{
					i->second.offset += size;
					i->second.h.update(buf, size);
				}
			}
		}
		
		int slot = allocate_slot_for_piece(piece_index);
		m_storage->write(buf, slot, offset, size);
	}

	int piece_manager::identify_data(
		const std::vector<char>& piece_data
		, int current_slot
		, std::vector<bool>& have_pieces
		, int& num_pieces
		, const std::multimap<sha1_hash, int>& hash_to_piece
		, boost::recursive_mutex& mutex)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT((int)have_pieces.size() == m_info->num_pieces());

		const int piece_size = static_cast<int>(m_info->piece_length());
		const int last_piece_size = static_cast<int>(m_info->piece_size(
			m_info->num_pieces() - 1));

		TORRENT_ASSERT((int)piece_data.size() >= last_piece_size);

		// calculate a small digest, with the same
		// size as the last piece. And a large digest
		// which has the same size as a normal piece
		hasher small_digest;
		small_digest.update(&piece_data[0], last_piece_size);
		hasher large_digest(small_digest);
		TORRENT_ASSERT(piece_size - last_piece_size >= 0);
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
			// the current slot is among the matching pieces, so
			// we will assume that the piece is in the right place
			const int piece_index = current_slot;

			// lock because we're writing to have_pieces
			boost::recursive_mutex::scoped_lock l(mutex);

			if (have_pieces[piece_index])
			{
				// we have already found a piece with
				// this index.
				int other_slot = m_piece_to_slot[piece_index];
				TORRENT_ASSERT(other_slot >= 0);

				// take one of the other matching pieces
				// that hasn't already been assigned
				int other_piece = -1;
				for (std::vector<int>::iterator i = matching_pieces.begin();
					i != matching_pieces.end(); ++i)
				{
					if (have_pieces[*i] || *i == piece_index) continue;
					other_piece = *i;
					break;
				}
				if (other_piece >= 0)
				{
					// replace the old slot with 'other_piece'
					TORRENT_ASSERT(have_pieces[other_piece] == false);
					have_pieces[other_piece] = true;
					m_slot_to_piece[other_slot] = other_piece;
					m_piece_to_slot[other_piece] = other_slot;
					++num_pieces;
				}
				else
				{
					// this index is the only piece with this
					// hash. The previous slot we found with
					// this hash must be the same piece. Mark
					// that piece as unassigned, since this slot
					// is the correct place for the piece.
					m_slot_to_piece[other_slot] = unassigned;
					if (m_storage_mode == storage_mode_compact)
						m_free_slots.push_back(other_slot);
				}
				TORRENT_ASSERT(m_piece_to_slot[piece_index] != current_slot);
				TORRENT_ASSERT(m_piece_to_slot[piece_index] >= 0);
				m_piece_to_slot[piece_index] = has_no_slot;
#ifndef NDEBUG
				// to make the assert happy, a few lines down
				have_pieces[piece_index] = false;
#endif
			}
			else
			{
				++num_pieces;
			}
			
			TORRENT_ASSERT(have_pieces[piece_index] == false);
			TORRENT_ASSERT(m_piece_to_slot[piece_index] == has_no_slot);
			have_pieces[piece_index] = true;

			return piece_index;
		}

		// find a matching piece that hasn't
		// already been assigned
		int free_piece = unassigned;
		for (std::vector<int>::iterator i = matching_pieces.begin();
			i != matching_pieces.end(); ++i)
		{
			if (have_pieces[*i]) continue;
			free_piece = *i;
			break;
		}

		if (free_piece >= 0)
		{
			// lock because we're writing to have_pieces
			boost::recursive_mutex::scoped_lock l(mutex);

			TORRENT_ASSERT(have_pieces[free_piece] == false);
			TORRENT_ASSERT(m_piece_to_slot[free_piece] == has_no_slot);
			have_pieces[free_piece] = true;
			++num_pieces;

			return free_piece;
		}
		else
		{
			TORRENT_ASSERT(free_piece == unassigned);
			return unassigned;
		}
	}

	// check if the fastresume data is up to date
	// if it is, use it and return true. If it 
	// isn't return false and the full check
	// will be run
	bool piece_manager::check_fastresume(
		aux::piece_checker_data& data
		, std::vector<bool>& pieces
		, int& num_pieces, storage_mode_t storage_mode
		, std::string& error_msg)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		INVARIANT_CHECK;

		TORRENT_ASSERT(m_info->piece_length() > 0);

		m_storage_mode = storage_mode;

		// This will corrupt the storage
		// use while debugging to find
		// states that cannot be scanned
		// by check_pieces.
//		m_storage->shuffle();

		m_piece_to_slot.resize(m_info->num_pieces(), has_no_slot);
		m_slot_to_piece.resize(m_info->num_pieces(), unallocated);
		TORRENT_ASSERT(m_free_slots.empty());
		TORRENT_ASSERT(m_unallocated_slots.empty());

		// assume no piece is out of place (i.e. in a slot
		// other than the one it should be in)
		bool out_of_place = false;
		
		pieces.clear();
		pieces.resize(m_info->num_pieces(), false);
		num_pieces = 0;

		// if we have fast-resume info
		// use it instead of doing the actual checking
		if (!data.piece_map.empty()
			&& int(data.piece_map.size()) <= m_info->num_pieces())
		{
			TORRENT_ASSERT(m_resume_data_verified);
			for (int i = 0; i < (int)data.piece_map.size(); ++i)
			{
				m_slot_to_piece[i] = data.piece_map[i];
				if (data.piece_map[i] >= 0)
				{
					if (data.piece_map[i] != i) out_of_place = true;
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
						++num_pieces;
						pieces[found_piece] = true;
					}
				}
				else if (data.piece_map[i] == unassigned)
				{
					if (m_storage_mode == storage_mode_compact)
						m_free_slots.push_back(i);
				}
				else
				{
					TORRENT_ASSERT(data.piece_map[i] == unallocated);
					if (m_storage_mode == storage_mode_compact)
						m_unallocated_slots.push_back(i);
				}
			}

			if (m_storage_mode == storage_mode_compact)
			{
				m_unallocated_slots.reserve(int(m_info->num_pieces() - data.piece_map.size()));
				for (int i = (int)data.piece_map.size(); i < (int)m_info->num_pieces(); ++i)
				{
					m_unallocated_slots.push_back(i);
				}
				if (m_unallocated_slots.empty())
				{
					switch_to_full_mode();
				}
			}
			else
			{
				if (!out_of_place)
				{
					// if no piece is out of place
					// since we're in full allocation mode, we can
					// forget the piece allocation tables

					std::vector<int>().swap(m_piece_to_slot);
					std::vector<int>().swap(m_slot_to_piece);
					m_state = state_create_files;
					return false;
				}
				else
				{
					// in this case we're in full allocation mode, but
					// we're resuming a compact allocated storage
					m_state = state_expand_pieces;
					m_current_slot = 0;
					error_msg = "pieces needs to be reordered";
					return false;
				}
			}

			m_state = state_create_files;
			return false;
		}

		m_state = state_full_check;
		return false;
	}

/*
   state chart:

   check_fastresume()

      |        |
      |        v
      |  +------------+   +---------------+
      |  | full_check |-->| expand_pieses |
      |  +------------+   +---------------+
      |        |                 |
      |        v                 |
      |  +--------------+        |
      +->| create_files | <------+
         +--------------+
               |
               v
         +----------+
         | finished |
         +----------+
*/


	// performs the full check and full allocation
	// (if necessary). returns true if finished and
	// false if it should be called again
	// the second return value is the progress the
	// file check is at. 0 is nothing done, and 1
	// is finished
	std::pair<bool, float> piece_manager::check_files(
		std::vector<bool>& pieces, int& num_pieces, boost::recursive_mutex& mutex)
	{
#ifndef NDEBUG
		boost::recursive_mutex::scoped_lock l_(mutex);
		TORRENT_ASSERT(num_pieces == std::count(pieces.begin(), pieces.end(), true));
		l_.unlock();
#endif

		if (m_state == state_create_files)
		{
			m_storage->initialize(m_storage_mode == storage_mode_allocate);
			m_state = state_finished;
			return std::make_pair(true, 1.f);
		}

		if (m_state == state_expand_pieces)
		{
			INVARIANT_CHECK;

			if (m_scratch_piece >= 0)
			{
				int piece = m_scratch_piece;
				int other_piece = m_slot_to_piece[piece];
				m_scratch_piece = -1;

				if (other_piece >= 0)
				{
					if (m_scratch_buffer2.empty())
						m_scratch_buffer2.resize(m_info->piece_length());

					m_storage->read(&m_scratch_buffer2[0], piece, 0, m_info->piece_size(other_piece));
					m_scratch_piece = other_piece;
					m_piece_to_slot[other_piece] = unassigned;
				}
				
				// the slot where this piece belongs is
				// free. Just move the piece there.
				m_storage->write(&m_scratch_buffer[0], piece, 0, m_info->piece_size(piece));
				m_piece_to_slot[piece] = piece;
				m_slot_to_piece[piece] = piece;

				if (other_piece >= 0)
					m_scratch_buffer.swap(m_scratch_buffer2);
		
				return std::make_pair(false, (float)m_current_slot / m_info->num_pieces());
			}

			while (m_current_slot < m_info->num_pieces()
				&& (m_slot_to_piece[m_current_slot] == m_current_slot
				|| m_slot_to_piece[m_current_slot] < 0))
			{
				++m_current_slot;
			}

			if (m_current_slot == m_info->num_pieces())
			{
				m_state = state_create_files;
				buffer().swap(m_scratch_buffer);
				buffer().swap(m_scratch_buffer2);
				if (m_storage_mode != storage_mode_compact)
				{
					std::vector<int>().swap(m_piece_to_slot);
					std::vector<int>().swap(m_slot_to_piece);
				}
				return std::make_pair(false, 1.f);
			}

			int piece = m_slot_to_piece[m_current_slot];
			TORRENT_ASSERT(piece >= 0);
			int other_piece = m_slot_to_piece[piece];
			if (other_piece >= 0)
			{
				// there is another piece in the slot
				// where this one goes. Store it in the scratch
				// buffer until next iteration.
				if (m_scratch_buffer.empty())
					m_scratch_buffer.resize(m_info->piece_length());
			
				m_storage->read(&m_scratch_buffer[0], piece, 0, m_info->piece_size(other_piece));
				m_scratch_piece = other_piece;
				m_piece_to_slot[other_piece] = unassigned;
			}

			// the slot where this piece belongs is
			// free. Just move the piece there.
			m_storage->move_slot(m_current_slot, piece);
			m_piece_to_slot[piece] = piece;
			m_slot_to_piece[m_current_slot] = unassigned;
			m_slot_to_piece[piece] = piece;
		
			return std::make_pair(false, (float)m_current_slot / m_info->num_pieces());
		}

		TORRENT_ASSERT(m_state == state_full_check);

		// ------------------------
		//    DO THE FULL CHECK
		// ------------------------

		try
		{
			if (m_info->num_pieces() == 0)
			{
				m_state = state_create_files;
				return std::make_pair(false, 1.f);
			}

			// initialization for the full check
			if (m_hash_to_piece.empty())
			{
				for (int i = 0; i < m_info->num_pieces(); ++i)
				{
					m_hash_to_piece.insert(std::make_pair(m_info->hash_for_piece(i), i));
				}
				boost::recursive_mutex::scoped_lock l(mutex);
				std::fill(pieces.begin(), pieces.end(), false);
				num_pieces = 0;
			}

			m_piece_data.resize(int(m_info->piece_length()));
			int piece_size = int(m_info->piece_size(m_current_slot));
			int num_read = int(m_storage->read(&m_piece_data[0]
				, m_current_slot, 0, piece_size));

			// if the file is incomplete, skip the rest of it
			if (num_read != piece_size)
				throw file_error("");

			int piece_index = identify_data(m_piece_data, m_current_slot
				, pieces, num_pieces, m_hash_to_piece, mutex);

			if (piece_index != m_current_slot
				&& piece_index >= 0)
				m_out_of_place = true;

			TORRENT_ASSERT(num_pieces == std::count(pieces.begin(), pieces.end(), true));
			TORRENT_ASSERT(piece_index == unassigned || piece_index >= 0);

			const bool this_should_move = piece_index >= 0 && m_slot_to_piece[piece_index] != unallocated;
			const bool other_should_move = m_piece_to_slot[m_current_slot] != has_no_slot;

			// check if this piece should be swapped with any other slot
			// this section will ensure that the storage is correctly sorted
			// libtorrent will never leave the storage in a state that
			// requires this sorting, but other clients may.

			// example of worst case:
			//                          | m_current_slot = 5
			//                          V
			//  +---+- - - +---+- - - +---+- -
			//  | x |      | 5 |      | 3 |     <- piece data in slots
			//  +---+- - - +---+- - - +---+- -
			//    3          y          5       <- slot index

			// in this example, the data in the m_current_slot (5)
			// is piece 3. It has to be moved into slot 3. The data
			// in slot y (piece 5) should be moved into the m_current_slot.
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
				TORRENT_ASSERT(piece_index != m_current_slot);

				const int other_slot = piece_index;
				TORRENT_ASSERT(other_slot >= 0);
				int other_piece = m_slot_to_piece[other_slot];

				m_slot_to_piece[other_slot] = piece_index;
				m_slot_to_piece[m_current_slot] = other_piece;
				m_piece_to_slot[piece_index] = piece_index;
				if (other_piece >= 0) m_piece_to_slot[other_piece] = m_current_slot;

				if (other_piece == unassigned)
				{
					std::vector<int>::iterator i =
						std::find(m_free_slots.begin(), m_free_slots.end(), other_slot);
					TORRENT_ASSERT(i != m_free_slots.end());
					if (m_storage_mode == storage_mode_compact)
					{
						m_free_slots.erase(i);
						m_free_slots.push_back(m_current_slot);
					}
				}

				if (other_piece >= 0)
					m_storage->swap_slots(other_slot, m_current_slot);
				else
					m_storage->move_slot(m_current_slot, other_slot);

				TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
			}
			// case 2
			else if (!this_should_move && other_should_move)
			{
				TORRENT_ASSERT(piece_index != m_current_slot);

				const int other_piece = m_current_slot;
				const int other_slot = m_piece_to_slot[other_piece];
				TORRENT_ASSERT(other_slot >= 0);

				m_slot_to_piece[m_current_slot] = other_piece;
				m_slot_to_piece[other_slot] = piece_index;
				m_piece_to_slot[other_piece] = m_current_slot;

				if (piece_index == unassigned
					&& m_storage_mode == storage_mode_compact)
					m_free_slots.push_back(other_slot);

				if (piece_index >= 0)
				{
					m_piece_to_slot[piece_index] = other_slot;
					m_storage->swap_slots(other_slot, m_current_slot);
				}
				else
				{
					m_storage->move_slot(other_slot, m_current_slot);
				}
				TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
			}
			else if (this_should_move && other_should_move)
			{
				TORRENT_ASSERT(piece_index != m_current_slot);
				TORRENT_ASSERT(piece_index >= 0);

				const int piece1 = m_slot_to_piece[piece_index];
				const int piece2 = m_current_slot;
				const int slot1 = piece_index;
				const int slot2 = m_piece_to_slot[piece2];

				TORRENT_ASSERT(slot1 >= 0);
				TORRENT_ASSERT(slot2 >= 0);
				TORRENT_ASSERT(piece2 >= 0);

				if (slot1 == slot2)
				{
					// this means there are only two pieces involved in the swap
					TORRENT_ASSERT(piece1 >= 0);

					// movement diagram:
					// +-------------------------------+
					// |                               |
					// +--> slot1 --> m_current_slot --+

					m_slot_to_piece[slot1] = piece_index;
					m_slot_to_piece[m_current_slot] = piece1;

					m_piece_to_slot[piece_index] = slot1;
					m_piece_to_slot[piece1] = m_current_slot;

					TORRENT_ASSERT(piece1 == m_current_slot);
					TORRENT_ASSERT(piece_index == slot1);

					m_storage->swap_slots(m_current_slot, slot1);

					TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
							|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
				}
				else
				{
					TORRENT_ASSERT(slot1 != slot2);
					TORRENT_ASSERT(piece1 != piece2);

					// movement diagram:
					// +-----------------------------------------+
					// |                                         |
					// +--> slot1 --> slot2 --> m_current_slot --+

					m_slot_to_piece[slot1] = piece_index;
					m_slot_to_piece[slot2] = piece1;
					m_slot_to_piece[m_current_slot] = piece2;

					m_piece_to_slot[piece_index] = slot1;
					m_piece_to_slot[m_current_slot] = piece2;

					if (piece1 == unassigned)
					{
						std::vector<int>::iterator i =
							std::find(m_free_slots.begin(), m_free_slots.end(), slot1);
						TORRENT_ASSERT(i != m_free_slots.end());
						if (m_storage_mode == storage_mode_compact)
						{
							m_free_slots.erase(i);
							m_free_slots.push_back(slot2);
						}
					}

					if (piece1 >= 0)
					{
						m_piece_to_slot[piece1] = slot2;
						m_storage->swap_slots3(m_current_slot, slot1, slot2);
					}
					else
					{
						m_storage->move_slot(m_current_slot, slot1);
						m_storage->move_slot(slot2, m_current_slot);
					}

					TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
				}
			}
			else
			{
				TORRENT_ASSERT(m_piece_to_slot[m_current_slot] == has_no_slot || piece_index != m_current_slot);
				TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unallocated);
				TORRENT_ASSERT(piece_index == unassigned || m_piece_to_slot[piece_index] == has_no_slot);

				// the slot was identified as piece 'piece_index'
				if (piece_index != unassigned)
					m_piece_to_slot[piece_index] = m_current_slot;
				else if (m_storage_mode == storage_mode_compact)
					m_free_slots.push_back(m_current_slot);

				m_slot_to_piece[m_current_slot] = piece_index;

				TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
						|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
			}
		}
		catch (file_error&)
		{
			// find the file that failed, and skip all the blocks in that file
			size_type file_offset = 0;
			size_type current_offset = size_type(m_current_slot) * m_info->piece_length();
			for (torrent_info::file_iterator i = m_info->begin_files(true);
					i != m_info->end_files(true); ++i)
			{
				file_offset += i->size;
				if (file_offset > current_offset) break;
			}

			TORRENT_ASSERT(file_offset > current_offset);
			int skip_blocks = static_cast<int>(
					(file_offset - current_offset + m_info->piece_length() - 1)
					/ m_info->piece_length());

			if (m_storage_mode == storage_mode_compact)
			{
				for (int i = m_current_slot; i < m_current_slot + skip_blocks; ++i)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] == unallocated);
					m_unallocated_slots.push_back(i);
				}
			}

			// current slot will increase by one at the end of the for-loop too
			m_current_slot += skip_blocks - 1;
		}
		++m_current_slot;

		if (m_current_slot >= m_info->num_pieces())
		{
			TORRENT_ASSERT(m_current_slot == m_info->num_pieces());

			// clear the memory we've been using
			std::vector<char>().swap(m_piece_data);
			std::multimap<sha1_hash, int>().swap(m_hash_to_piece);

			if (m_storage_mode != storage_mode_compact)
			{
				if (!m_out_of_place)
				{
					// if no piece is out of place
					// since we're in full allocation mode, we can
					// forget the piece allocation tables

					std::vector<int>().swap(m_piece_to_slot);
					std::vector<int>().swap(m_slot_to_piece);
					m_state = state_create_files;
					return std::make_pair(false, 1.f);
				}
				else
				{
					// in this case we're in full allocation mode, but
					// we're resuming a compact allocated storage
					m_state = state_expand_pieces;
					m_current_slot = 0;
					return std::make_pair(false, 0.f);
				}
			}
			else if (m_unallocated_slots.empty())
			{
				switch_to_full_mode();
			}
			m_state = state_create_files;

#ifndef NDEBUG
			boost::recursive_mutex::scoped_lock l(mutex);
			TORRENT_ASSERT(num_pieces == std::count(pieces.begin(), pieces.end(), true));
#endif
			return std::make_pair(false, 1.f);
		}

		TORRENT_ASSERT(num_pieces == std::count(pieces.begin(), pieces.end(), true));

		return std::make_pair(false, (float)m_current_slot / m_info->num_pieces());
	}

	void piece_manager::switch_to_full_mode()
	{
		TORRENT_ASSERT(m_storage_mode == storage_mode_compact);	
		TORRENT_ASSERT(m_unallocated_slots.empty());	
		// we have allocated all slots, switch to
		// full allocation mode in order to free
		// some unnecessary memory.
		m_storage_mode = storage_mode_sparse;
		std::vector<int>().swap(m_unallocated_slots);
		std::vector<int>().swap(m_free_slots);
		std::vector<int>().swap(m_piece_to_slot);
		std::vector<int>().swap(m_slot_to_piece);
	}

	int piece_manager::allocate_slot_for_piece(int piece_index)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		if (m_storage_mode != storage_mode_compact) return piece_index;

//		INVARIANT_CHECK;

		TORRENT_ASSERT(piece_index >= 0);
		TORRENT_ASSERT(piece_index < (int)m_piece_to_slot.size());
		TORRENT_ASSERT(m_piece_to_slot.size() == m_slot_to_piece.size());

		int slot_index = m_piece_to_slot[piece_index];

		if (slot_index != has_no_slot)
		{
			TORRENT_ASSERT(slot_index >= 0);
			TORRENT_ASSERT(slot_index < (int)m_slot_to_piece.size());
			return slot_index;
		}

		if (m_free_slots.empty())
		{
			allocate_slots(1);
			TORRENT_ASSERT(!m_free_slots.empty());
		}

		std::vector<int>::iterator iter(
			std::find(
				m_free_slots.begin()
				, m_free_slots.end()
				, piece_index));

		if (iter == m_free_slots.end())
		{
			TORRENT_ASSERT(m_slot_to_piece[piece_index] != unassigned);
			TORRENT_ASSERT(!m_free_slots.empty());
			iter = m_free_slots.end() - 1;

			// special case to make sure we don't use the last slot
			// when we shouldn't, since it's smaller than ordinary slots
			if (*iter == m_info->num_pieces() - 1 && piece_index != *iter)
			{
				if (m_free_slots.size() == 1)
					allocate_slots(1);
				TORRENT_ASSERT(m_free_slots.size() > 1);
				// assumes that all allocated slots
				// are put at the end of the free_slots vector
				iter = m_free_slots.end() - 1;
			}
		}

		slot_index = *iter;
		m_free_slots.erase(iter);

		TORRENT_ASSERT(m_slot_to_piece[slot_index] == unassigned);

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
			TORRENT_ASSERT(m_piece_to_slot[piece_at_our_slot] == piece_index);

			std::swap(
				m_slot_to_piece[piece_index]
				, m_slot_to_piece[slot_index]);

			std::swap(
				m_piece_to_slot[piece_index]
				, m_piece_to_slot[piece_at_our_slot]);

			m_storage->move_slot(piece_index, slot_index);

			TORRENT_ASSERT(m_slot_to_piece[piece_index] == piece_index);
			TORRENT_ASSERT(m_piece_to_slot[piece_index] == piece_index);

			slot_index = piece_index;

#if !defined(NDEBUG) && defined(TORRENT_STORAGE_DEBUG)
			debug_log();
#endif
		}
		TORRENT_ASSERT(slot_index >= 0);
		TORRENT_ASSERT(slot_index < (int)m_slot_to_piece.size());

		if (m_unallocated_slots.empty())
		{
			switch_to_full_mode();
		}
		
		return slot_index;
	}

	bool piece_manager::allocate_slots(int num_slots, bool abort_on_disk)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		TORRENT_ASSERT(num_slots > 0);

//		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_unallocated_slots.empty());
		TORRENT_ASSERT(m_storage_mode == storage_mode_compact);

		bool written = false;

		for (int i = 0; i < num_slots && !m_unallocated_slots.empty(); ++i)
		{
//			INVARIANT_CHECK;

			int pos = m_unallocated_slots.front();
			TORRENT_ASSERT(m_slot_to_piece[pos] == unallocated);
			TORRENT_ASSERT(m_piece_to_slot[pos] != pos);

			int new_free_slot = pos;
			if (m_piece_to_slot[pos] != has_no_slot)
			{
				new_free_slot = m_piece_to_slot[pos];
				m_storage->move_slot(new_free_slot, pos);
				m_slot_to_piece[pos] = pos;
				m_piece_to_slot[pos] = pos;
				written = true;
			}
			m_unallocated_slots.erase(m_unallocated_slots.begin());
			m_slot_to_piece[new_free_slot] = unassigned;
			m_free_slots.push_back(new_free_slot);
			if (abort_on_disk && written) break;
		}

		TORRENT_ASSERT(m_free_slots.size() > 0);
		return written;
	}

	int piece_manager::slot_for(int piece) const
	{
		if (m_storage_mode != storage_mode_compact) return piece;
		TORRENT_ASSERT(piece < int(m_piece_to_slot.size()));
		TORRENT_ASSERT(piece >= 0);
		return m_piece_to_slot[piece];
	}

	int piece_manager::piece_for(int slot) const
	{
		if (m_storage_mode != storage_mode_compact) return slot;
		TORRENT_ASSERT(slot < int(m_slot_to_piece.size()));
		TORRENT_ASSERT(slot >= 0);
		return m_slot_to_piece[slot];
	}
		
#ifndef NDEBUG
	void piece_manager::check_invariant() const
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		if (m_unallocated_slots.empty() && m_state == state_finished)
		{
			TORRENT_ASSERT(m_storage_mode != storage_mode_compact
				|| m_info->num_pieces() == 0);
		}
		
		if (m_storage_mode != storage_mode_compact)
		{
			TORRENT_ASSERT(m_unallocated_slots.empty());
			TORRENT_ASSERT(m_free_slots.empty());
		}
		
		if (m_storage_mode != storage_mode_compact
			&& m_state != state_expand_pieces
			&& m_state != state_full_check)
		{
			TORRENT_ASSERT(m_piece_to_slot.empty());
			TORRENT_ASSERT(m_slot_to_piece.empty());
		}
		else
		{
			if (m_piece_to_slot.empty()) return;

			TORRENT_ASSERT((int)m_piece_to_slot.size() == m_info->num_pieces());
			TORRENT_ASSERT((int)m_slot_to_piece.size() == m_info->num_pieces());

			for (std::vector<int>::const_iterator i = m_free_slots.begin();
					i != m_free_slots.end(); ++i)
			{
				TORRENT_ASSERT(*i < (int)m_slot_to_piece.size());
				TORRENT_ASSERT(*i >= 0);
				TORRENT_ASSERT(m_slot_to_piece[*i] == unassigned);
				TORRENT_ASSERT(std::find(i+1, m_free_slots.end(), *i)
						== m_free_slots.end());
			}

			for (std::vector<int>::const_iterator i = m_unallocated_slots.begin();
					i != m_unallocated_slots.end(); ++i)
			{
				TORRENT_ASSERT(*i < (int)m_slot_to_piece.size());
				TORRENT_ASSERT(*i >= 0);
				TORRENT_ASSERT(m_slot_to_piece[*i] == unallocated);
				TORRENT_ASSERT(std::find(i+1, m_unallocated_slots.end(), *i)
						== m_unallocated_slots.end());
			}

			for (int i = 0; i < m_info->num_pieces(); ++i)
			{
				// Check domain of piece_to_slot's elements
				if (m_piece_to_slot[i] != has_no_slot)
				{
					TORRENT_ASSERT(m_piece_to_slot[i] >= 0);
					TORRENT_ASSERT(m_piece_to_slot[i] < (int)m_slot_to_piece.size());
				}

				// Check domain of slot_to_piece's elements
				if (m_slot_to_piece[i] != unallocated
						&& m_slot_to_piece[i] != unassigned)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] >= 0);
					TORRENT_ASSERT(m_slot_to_piece[i] < (int)m_piece_to_slot.size());
				}

				// do more detailed checks on piece_to_slot
				if (m_piece_to_slot[i] >= 0)
				{
					TORRENT_ASSERT(m_slot_to_piece[m_piece_to_slot[i]] == i);
					if (m_piece_to_slot[i] != i)
					{
						TORRENT_ASSERT(m_slot_to_piece[i] == unallocated);
					}
				}
				else
				{
					TORRENT_ASSERT(m_piece_to_slot[i] == has_no_slot);
				}

				// do more detailed checks on slot_to_piece

				if (m_slot_to_piece[i] >= 0)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] < (int)m_piece_to_slot.size());
					TORRENT_ASSERT(m_piece_to_slot[m_slot_to_piece[i]] == i);
#ifdef TORRENT_STORAGE_DEBUG
					TORRENT_ASSERT(
							std::find(
								m_unallocated_slots.begin()
								, m_unallocated_slots.end()
								, i) == m_unallocated_slots.end()
							);
					TORRENT_ASSERT(
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
					TORRENT_ASSERT(m_unallocated_slots.empty()
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
					TORRENT_ASSERT(
							std::find(
								m_free_slots.begin()
								, m_free_slots.end()
								, i) != m_free_slots.end()
							);
#endif
				}
				else
				{
					TORRENT_ASSERT(false && "m_slot_to_piece[i] is invalid");
				}
			}
		}
	}

#ifdef TORRENT_STORAGE_DEBUG
	void piece_manager::debug_log() const
	{
		std::stringstream s;

		s << "index\tslot\tpiece\n";

		for (int i = 0; i < m_info->num_pieces(); ++i)
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

