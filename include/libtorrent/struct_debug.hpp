/*

Copyright (c) 2011, Arvid Norberg, Magnus Jonsson
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

#ifndef TORRENT_STRUCT_DEBUG
#define TORRENT_STRUCT_DEBUG

#define PRINT_SIZEOF(x) snprintf(tmp, sizeof(tmp), "\nsizeof(" #x ") = %d\n", int(sizeof(x))); \
	l << tmp; \
	temp = 0; \
	prev_size = 0;

#define PRINT_OFFSETOF(x, y) if (offsetof(x, y) > 0) { \
		snprintf(tmp, sizeof(tmp), "\tsize: %-3d\tpadding: %-3d\n" \
		, prev_size \
		, int((offsetof(x, y) - temp)) - prev_size); \
		l << tmp; \
	} \
	snprintf(tmp, sizeof(tmp), "%-50s: %-3d" \
		, #x "::" #y \
		, int(offsetof(x, y))); \
	temp = offsetof(x, y); \
	prev_size = sizeof(reinterpret_cast<x*>(0)->y); \
	l << tmp;

#define PRINT_OFFSETOF_END(x) snprintf(tmp, sizeof(tmp), "\tsize: %-3d\tpadding: %-3d\n" \
	, prev_size, int((sizeof(x) - temp) - prev_size)); \
	l << tmp;

#endif // TORRENT_STRUCT_DEBUG

