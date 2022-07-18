/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PREAD_DISK_JOB_HPP
#define TORRENT_PREAD_DISK_JOB_HPP

#include "libtorrent/aux_/disk_job.hpp"

namespace libtorrent::aux {

	struct pread_storage;

	struct TORRENT_EXTRA_EXPORT pread_disk_job : disk_job
	{
		// the disk storage this job applies to (if applicable)
		std::shared_ptr<pread_storage> storage;
	};

}

#endif // TORRENT_PREAD_DISK_JOB_HPP
