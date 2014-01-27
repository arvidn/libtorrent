/*

Copyright (c) 2009, Arvid Norberg
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

#ifndef TORRENT_MAX_TYPE
#define TORRENT_MAX_TYPE

namespace libtorrent
{

	template<int v1, int v2>
	struct max { enum { value = v1>v2?v1:v2 }; };

	template<int v1, int v2, int v3>
	struct max3
	{
		enum
		{
			temp = max<v1,v2>::value,
			value = max<temp, v3>::value
		};
	};

	template<int v1, int v2, int v3, int v4>
	struct max4
	{
		enum
		{
			temp1 = max<v1,v2>::value,
			temp2 = max<v3,v4>::value,
			value = max<temp1, temp2>::value
		};
	};

	template<int v1, int v2, int v3, int v4, int v5>
	struct max5
	{
		enum
		{
			temp = max4<v1,v2, v3, v4>::value,
			value = max<temp, v5>::value
		};
	};

	template<int v1, int v2, int v3, int v4, int v5, int v6>
	struct max6
	{
		enum
		{
			temp1 = max<v1,v2>::value,
			temp2 = max<v3,v4>::value,
			temp3 = max<v5,v6>::value,
			value = max3<temp1, temp2, temp3>::value
		};
	};

	template<int v1, int v2, int v3, int v4, int v5, int v6, int v7>
	struct max7
	{
		enum
		{
			temp1 = max<v1,v2>::value,
			temp2 = max<v3,v4>::value,
			temp3 = max3<v5,v6,v7>::value,
			value = max3<temp1, temp2, temp3>::value
		};
	};

	template<int v1, int v2, int v3, int v4, int v5, int v6, int v7, int v8>
	struct max8
	{
		enum
		{
			temp1 = max<v1,v2>::value,
			temp2 = max3<v3,v4,v5>::value,
			temp3 = max3<v6,v7,v8>::value,
			value = max3<temp1, temp2, temp3>::value
		};
	};

	template<int v1, int v2, int v3, int v4, int v5, int v6, int v7, int v8, int v9>
	struct max9
	{
		enum
		{
			temp1 = max3<v1,v2, v3>::value,
			temp2 = max3<v4,v5,v6>::value,
			temp3 = max3<v7,v8,v9>::value,
			value = max3<temp1, temp2, temp3>::value
		};
	};
}

#endif

