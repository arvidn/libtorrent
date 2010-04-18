// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/identify_client.hpp>
#include <libtorrent/bencode.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

object client_fingerprint_(peer_id const& id)
{
    boost::optional<fingerprint> result = client_fingerprint(id);
    return result ? object(*result) : object();
}

entry bdecode_(std::string const& data)
{
    return bdecode(data.begin(), data.end());
}

std::string bencode_(entry const& e)
{
    std::string result;
    bencode(std::back_inserter(result), e);
    return result;
}

void bind_utility()
{
    def("identify_client", &libtorrent::identify_client);
    def("client_fingerprint", &client_fingerprint_);
    def("bdecode", &bdecode_);
    def("bencode", &bencode_);
}

