/*

Copyright (c) 2015, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/version.hpp"

namespace lt {

char const* version()
{
	return version_str;
}

}
