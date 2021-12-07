/*

Copyright (c) 2016-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef PRINT_ALERTS_HPP
#define PRINT_ALERTS_HPP

#include "libtorrent/time.hpp"
#include "libtorrent/session.hpp"
#include "test.hpp" // for EXPORT

EXPORT void print_alerts(lt::session* ses, lt::time_point start_time);

#endif

