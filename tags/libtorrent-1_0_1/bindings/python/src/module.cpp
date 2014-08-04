// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifdef __GNUC__
#define BOOST_PYTHON_USE_GCC_SYMBOL_VISIBILITY 1
#endif

#include "libtorrent/config.hpp"
#include <boost/python/module.hpp>

void bind_utility();
void bind_fingerprint();
void bind_sha1_hash();
void bind_session();
void bind_entry();
void bind_torrent_info();
void bind_unicode_string_conversion();
void bind_torrent_handle();
void bind_torrent_status();
void bind_session_settings();
void bind_version();
void bind_alert();
void bind_datetime();
void bind_peer_info();
void bind_ip_filter();
void bind_magnet_uri();
void bind_converters();
void bind_create_torrent();
void bind_error_code();

BOOST_PYTHON_MODULE(libtorrent)
{
    Py_Initialize();
    PyEval_InitThreads();

    bind_error_code();
    bind_utility();
    bind_fingerprint();
    bind_sha1_hash();
    bind_entry();
    bind_session();
    bind_torrent_info();
#if TORRENT_USE_WSTRING
    bind_unicode_string_conversion();
#endif
    bind_torrent_handle();
    bind_torrent_status();
    bind_session_settings();
    bind_version();
    bind_alert();
    bind_datetime();
    bind_peer_info();
    bind_ip_filter();
    bind_magnet_uri();
    bind_converters();
    bind_create_torrent();
}
