/*

Copyright (c) 2014-2022, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_MMAP_DISK_JOB_HPP
#define TORRENT_MMAP_DISK_JOB_HPP

#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/tailqueue.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/session_types.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/aux_/disk_job.hpp"

#include <variant>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace libtorrent::aux {

	struct mmap_storage;

	struct TORRENT_EXTRA_EXPORT mmap_disk_job : disk_job
	{
		// the disk storage this job applies to (if applicable)
		std::shared_ptr<mmap_storage> storage;
	};

}

#endif // TORRENT_MMAP_DISK_JOB_HPP
