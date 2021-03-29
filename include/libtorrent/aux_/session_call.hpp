/*

Copyright (c) 2014, Arvid Norberg, Steven Siloti
Copyright (c) 2014-2016, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SESSION_CALL_HPP_INCLUDED
#define TORRENT_SESSION_CALL_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include <functional>

namespace lt::aux {

void blocking_call();
void dump_call_profile();

void torrent_wait(bool& done, aux::session_impl& ses);

} // namespace lt::aux

#endif // TORRENT_SESSION_CALL_HPP_INCLUDED

