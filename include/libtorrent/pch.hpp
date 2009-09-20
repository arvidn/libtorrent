/*

Copyright (c) 2008, Arvid Norberg
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

#ifdef BOOST_BUILD_PCH_ENABLED

#include <algorithm>
#include <asio/ip/host_name.hpp>
#include <assert.h>
#include <bitset>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/config.hpp>
#include <boost/cstdint.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/detail/atomic_count.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/function.hpp>
#include <boost/integer_traits.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/iterator_adaptors.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/limits.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/next_prior.hpp>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>
#include <boost/ref.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/utility.hpp>
#include <boost/version.hpp>
#include <boost/weak_ptr.hpp>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>
#include <zlib.h>

#ifdef __OBJC__
#define Protocol Protocol_
#endif

#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/io_service.hpp>
#include <asio/deadline_timer.hpp>
#include <asio/write.hpp>

#ifdef __OBJC__ 
#undef Protocol
#endif

#endif

