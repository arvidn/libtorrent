// Copyright Andrew Resch 2008. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt

#include <boost/python.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace {
    
    torrent_handle _add_magnet_uri(session& s, std::string uri, dict params)
    {
        add_torrent_params p;

        std::string url;
        if (params.has_key("tracker_url"))
        {
            url = extract<std::string>(params["tracker_url"]);
            p.tracker_url = url.c_str();
        }
        std::string name;
        if (params.has_key("name"))
        {
            name = extract<std::string>(params["name"]);
            p.name = name.c_str();
        }
        p.save_path = fs::path(extract<std::string>(params["save_path"]));

        std::vector<char> resume_buf;
        if (params.has_key("resume_data"))
        {
            std::string resume = extract<std::string>(params["resume_data"]);
            resume_buf.resize(resume.size());
            std::memcpy(&resume_buf[0], &resume[0], resume.size());
            p.resume_data = &resume_buf;
        }
        p.storage_mode = extract<storage_mode_t>(params["storage_mode"]);
        p.paused = params["paused"];
        p.auto_managed = params["auto_managed"];
        p.duplicate_is_error = params["duplicate_is_error"];
        
        return add_magnet_uri(s, uri, p);
    }
}

void bind_magnet_uri()
{
    def("add_magnet_uri", &_add_magnet_uri);
}
