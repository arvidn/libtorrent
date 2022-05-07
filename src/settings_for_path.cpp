/*

Copyright (c) 2022, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/settings_for_path.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/string_view.hpp"

#include <iostream>
#include <thread>

#ifdef TORRENT_LINUX
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/sysmacros.h> // for major()/minor()
#include <dirent.h>
#include <unistd.h>

#include "libtorrent/aux_/file_descriptor.hpp"

#include <boost/optional.hpp>

namespace {

struct directory
{
	directory() : ptr(nullptr) {}
	explicit directory(DIR* p) : ptr(p) {}
	~directory() { if (ptr != nullptr) ::closedir(ptr); }
	directory(directory const&) = delete;
	directory(directory&& f) : ptr(f.ptr) { f.ptr = nullptr; }
	directory& operator=(directory const&) = delete;
	directory& operator=(directory&& f)
	{
		std::swap(ptr, f.ptr);
		return *this;
	}
	DIR* dir() const { return ptr; }
private:
	DIR* ptr;
};

boost::optional<std::string> read_file(char const* dev_name, char const* path)
{
	char p[300];
	std::snprintf(p, sizeof(p), "/sys/block/%s/%s", dev_name, path);

	lt::aux::file_descriptor f = ::open(p, 0);
	if (f.fd() == -1)
		return boost::none;


	auto size = ::read(f.fd(), p, sizeof(p));
	if (size <= 0 || size > sizeof(p))
		return boost::none;

	return std::string(p, size);
}
}

#elif defined TORRENT_WINDOWS

#include "libtorrent/aux_/windows.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/file_handle.hpp"

#include <boost/optional.hpp>

#endif

namespace {

int num_threads(int const scale = 1)
{
	return std::max(1, static_cast<int>(
		std::thread::hardware_concurrency() / scale));
}
}

namespace libtorrent {

void settings_for_path(settings_interface& settings, std::string const& path)
{
#ifdef TORRENT_LINUX
	struct stat st{};
	if (stat(path.c_str(), &st) == 0)
	{
		char device_id[50];
		std::snprintf(device_id, sizeof(device_id), "%d:%d\n", major(st.st_dev), minor(st.st_dev));

		directory dir(opendir("/sys/block"));
		if (dir.dir() != nullptr)
		{

			struct dirent* de;
			while ((de = readdir(dir.dir())))
			{
				if (de->d_name[0] == '.')
					continue;

				auto content = read_file(de->d_name, "/dev");
				if (!content)
					continue;

				if (*content != device_id)
					continue;

				content = read_file(de->d_name, "/queue/rotational");
				if (!content)
					continue;

				if (*content == "1\n")
				{
					settings.set_int(settings_pack::hashing_threads, 1);
				}
				else if (*content == "0\n")
				{
					content = read_file(de->d_name, "/queue/dax");
					int scale = 2;
					if (content && *content == "1\n")
					{
						// if we have DAX storage, max out the threads accessing
						// the disk
						scale = 1;
						settings.set_int(settings_pack::aio_threads, num_threads());
					}

					settings.set_int(settings_pack::hashing_threads, num_threads(scale));
				}
				return;
			}
		}
	}

#elif defined TORRENT_WINDOWS

	auto const native_path = convert_to_native_path_string(path);

	std::array<wchar_t, 300> volume_path;
	if (GetVolumePathNameW(native_path.c_str(), volume_path.data(), volume_path.size()) == 0)
		goto failed;

	int const drive_type = GetDriveTypeW(volume_path.data());
	if (drive_type == DRIVE_REMOTE)
	{
		settings.set_int(settings_pack::hashing_threads, 1);
		return;
	}
	if (drive_type == DRIVE_RAMDISK)
	{
		auto const n = num_threads();
		settings.set_int(settings_pack::aio_threads, n);
		settings.set_int(settings_pack::hashing_threads, n);
		return;
	}
	DWORD fs_flags = 0;
	if (GetVolumeInformationW(volume_path.data()
		, nullptr, 0, nullptr, nullptr, &fs_flags, nullptr, 0))
	{
		if (fs_flags & FILE_DAX_VOLUME)
		{
			// if we have DAX storage, max out the threads accessing
			// the disk
			auto const n = num_threads();
			settings.set_int(settings_pack::aio_threads, n);
			settings.set_int(settings_pack::hashing_threads, n);
			return;
		}
	}

	// these steps are documented here:
	// https://docs.microsoft.com/en-us/windows/win32/fileio/basic-and-dynamic-disks
	std::array<wchar_t, 300> volume_name;
	if (GetVolumeNameForVolumeMountPointW(volume_path.data()
		, volume_name.data(), volume_name.size()))
	{
		// strip trailing backslash
		auto const vol_name_len = ::wcslen(volume_name.data());
		if (vol_name_len > 0 && volume_name[vol_name_len-1] == L'\\')
			volume_name[vol_name_len - 1] = 0;

		aux::win_file_handle vol = CreateFileW(volume_name.data(), 0
			, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr
			, OPEN_EXISTING, {}, nullptr);
		if (vol.handle() != INVALID_HANDLE_VALUE)
		{
			struct extents_t
			{
				DWORD NumberOfDiskExtents;
				DISK_EXTENT Extents[4];
			};
			extents_t extents{};
			DWORD output_len = 0;
			if (DeviceIoControl(vol.handle(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
				, nullptr, 0, &extents, sizeof(extents), &output_len, nullptr))
			{
				boost::optional<bool> seek_penalty;
				// a volume may span multiple physical disks, since we won't
				// know which physical disk we will access, make the
				// conservative assumption that we'll be on the worst one. If
				// one of the disks has seek-penalty, consider the whole volume
				// as a spinning disk and we should use a single hasher thread.
				for (int i = 0; i < extents.NumberOfDiskExtents; ++i)
				{
					std::array<wchar_t, 50> device_name{};
					::_snwprintf(device_name.data(), device_name.size()
						, L"\\\\?\\PhysicalDrive%d", extents.Extents[i].DiskNumber);

					aux::win_file_handle dev = CreateFileW(device_name.data(), 0
						, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr
						, OPEN_EXISTING, {}, nullptr);
					if (dev.handle() != INVALID_HANDLE_VALUE)
					{
						STORAGE_PROPERTY_QUERY query{};
						query.PropertyId = StorageDeviceSeekPenaltyProperty;
						query.QueryType = PropertyExistsQuery;
						DEVICE_SEEK_PENALTY_DESCRIPTOR dev_seek{};

						DWORD output_len = 0;
						query.QueryType = PropertyStandardQuery;
						if (DeviceIoControl(dev.handle(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &dev_seek, sizeof(dev_seek), &output_len, nullptr))
						{
							if (dev_seek.IncursSeekPenalty)
							{
								seek_penalty = true;
								break;
							}
							else if (!seek_penalty)
							{
								seek_penalty = false;
							}
						}
					}
				}
				if (seek_penalty && !*seek_penalty)
				{
					settings.set_int(settings_pack::hashing_threads, num_threads(2));
					return;
				}
			}
		}
	}
failed:
#endif
	settings.set_int(settings_pack::hashing_threads, 1);
}
}

