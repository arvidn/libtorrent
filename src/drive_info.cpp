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
#include "libtorrent/aux_/drive_info.hpp"

#include <iostream>
#include <thread>
#include <string>

#ifdef TORRENT_LINUX
#include <sys/vfs.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/sysmacros.h> // for major()/minor()
#include <dirent.h>
#include <unistd.h>

#include "libtorrent/aux_/file_descriptor.hpp"

#include "libtorrent/optional.hpp"

namespace {

struct directory
{
	explicit directory(DIR* p) : ptr(p) {}
	~directory() { if (ptr != nullptr) ::closedir(ptr); }
	directory(directory const&) = delete;
	directory(directory&& f) = delete;
	directory& operator=(directory const&) = delete;
	directory& operator=(directory&& f) = delete;
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
	if (size <= 0 || size > static_cast<decltype(size)>(sizeof(p)))
		return boost::none;

	return std::string(p, std::size_t(size));
}
}

#elif defined TORRENT_WINDOWS && !defined TORRENT_WINRT

#include "libtorrent/aux_/windows.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/win_file_handle.hpp"

#include "libtorrent/optional.hpp"

#endif

namespace libtorrent {
namespace aux {

#ifdef TORRENT_LINUX
drive_info get_drive_info(std::string const& path)
{
	// make a conservative default assumption
	drive_info const def = drive_info::spinning;

	struct statfs stfs{};
	if (statfs(path.c_str(), &stfs) != 0)
	{
		if (stfs.f_type == TMPFS_MAGIC
			|| stfs.f_type == RAMFS_MAGIC)
			return drive_info::ssd_dax;

#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC      0x65735546
#endif

		// most fuse-based filesystems are probably not remote
		// but sshfs is and fuse appears to not like memory mapped files very
		// much. So this is a conservative assumption.
		if (stfs.f_type == FUSE_SUPER_MAGIC
			|| stfs.f_type == NFS_SUPER_MAGIC)
			return drive_info::remote;
	}

	struct stat st{};
	if (stat(path.c_str(), &st) != 0)
		return def;

	char device_id[50];
	std::snprintf(device_id, sizeof(device_id), "%d:%d\n", major(st.st_dev), minor(st.st_dev));

	directory dir(opendir("/sys/block"));
	if (dir.dir() == nullptr)
		return def;

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
			return drive_info::spinning;
		}
		else if (*content == "0\n")
		{
			content = read_file(de->d_name, "/queue/dax");
			if (content && *content == "1\n")
				return drive_info::ssd_dax;
			else
				return drive_info::ssd_disk;
		}
	}
	return def;
}

#elif defined TORRENT_WINDOWS && !defined TORRENT_WINRT

drive_info get_drive_info(std::string const& path)
{
	// make a conservative default assumption
	drive_info const def = drive_info::spinning;

	auto const native_path = convert_to_native_path_string(path);

	std::array<wchar_t, 300> volume_path;
	if (GetVolumePathNameW(native_path.c_str(), volume_path.data(), DWORD(volume_path.size())) == 0)
		return def;

	int const drive_type = GetDriveTypeW(volume_path.data());
	if (drive_type == DRIVE_REMOTE)
		return drive_info::remote;

	if (drive_type == DRIVE_RAMDISK)
		return drive_info::ssd_dax;

	DWORD fs_flags = 0;
	if (GetVolumeInformationW(volume_path.data()
		, nullptr, 0, nullptr, nullptr, &fs_flags, nullptr, 0))
	{
#ifndef FILE_DAX_VOLUME
#define FILE_DAX_VOLUME 0x20000000
#endif
		if (fs_flags & FILE_DAX_VOLUME)
			return drive_info::ssd_dax;
	}

	// these steps are documented here:
	// https://docs.microsoft.com/en-us/windows/win32/fileio/basic-and-dynamic-disks
	std::array<wchar_t, 300> volume_name;
	if (!GetVolumeNameForVolumeMountPointW(volume_path.data()
		, volume_name.data(), DWORD(volume_name.size())))
		return def;

	// strip trailing backslash
	auto const vol_name_len = ::wcslen(volume_name.data());
	if (vol_name_len > 0 && volume_name[vol_name_len-1] == L'\\')
		volume_name[vol_name_len - 1] = 0;

	aux::win_file_handle vol = CreateFileW(volume_name.data(), 0
		, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr
		, OPEN_EXISTING, {}, nullptr);
	if (vol.handle() == INVALID_HANDLE_VALUE)
		return def;

	struct extents_t
	{
		DWORD NumberOfDiskExtents;
		DISK_EXTENT Extents[4];
	};
	extents_t extents{};
	DWORD output_len = 0;
	if (!DeviceIoControl(vol.handle(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
		, nullptr, 0, &extents, sizeof(extents), &output_len, nullptr))
		return def;

	boost::optional<bool> seek_penalty;
	// a volume may span multiple physical disks, since we won't
	// know which physical disk we will access, make the
	// conservative assumption that we'll be on the worst one. If
	// one of the disks has seek-penalty, consider the whole volume
	// as a spinning disk and we should use a single hasher thread.
	for (int i = 0; i < int(extents.NumberOfDiskExtents); ++i)
	{
		std::array<wchar_t, 50> device_name{};
		::_snwprintf(device_name.data(), device_name.size()
			, L"\\\\?\\PhysicalDrive%d", extents.Extents[i].DiskNumber);

		aux::win_file_handle dev = CreateFileW(device_name.data(), 0
			, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr
			, OPEN_EXISTING, {}, nullptr);
		if (dev.handle() == INVALID_HANDLE_VALUE)
			continue;

#if _WIN32_WINNT >= 0x601
		STORAGE_PROPERTY_QUERY query{};
		query.PropertyId = StorageDeviceSeekPenaltyProperty;
		query.QueryType = PropertyExistsQuery;
		DEVICE_SEEK_PENALTY_DESCRIPTOR dev_seek{};

		output_len = 0;
		query.QueryType = PropertyStandardQuery;
		if (!DeviceIoControl(dev.handle(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &dev_seek, sizeof(dev_seek), &output_len, nullptr))
			continue;

		if (dev_seek.IncursSeekPenalty)
		{
			seek_penalty = true;
			break;
		}
		else if (!seek_penalty)
		{
			seek_penalty = false;
		}
#endif
	}
	if (seek_penalty && !*seek_penalty)
		return drive_info::ssd_disk;
	return def;
}
#else

drive_info get_drive_info(std::string const&)
{
	return drive_info::spinning;
}
#endif
}
}

