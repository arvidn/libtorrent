/*
SHA-1 C++ conversion

original version:

SHA-1 in C
By Steve Reid <sreid@sea-to-sky.net>
100% Public Domain

changelog at the end of the file.
*/

#include <cstdio>
#include <cstring>

#include "libtorrent/sha1.hpp"

#include <boost/detail/endian.hpp> // for BIG_ENDIAN and LITTLE_ENDIAN macros

typedef boost::uint32_t u32;
typedef boost::uint8_t u8;

namespace libtorrent
{

namespace
{
	union CHAR64LONG16
	{
		u8 c[64];
		u32 l[16];
	};

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-member-function"
#endif

// blk0() and blk() perform the initial expand.
// I got the idea of expanding during the round function from SSLeay
	struct little_endian_blk0
	{
		static u32 apply(CHAR64LONG16* block, int i)
		{
			return block->l[i] = (rol(block->l[i],24)&0xFF00FF00)
				| (rol(block->l[i],8)&0x00FF00FF);
		}
	};

	struct big_endian_blk0
	{
		static u32 apply(CHAR64LONG16* block, int i)
		{
			return  block->l[i];
		}
	};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
	^block->l[(i+2)&15]^block->l[i&15],1))

// (R0+R1), R2, R3, R4 are the different operations used in SHA1
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+BlkFun::apply(block, i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

	// Hash a single 512-bit block. This is the core of the algorithm.
	template <class BlkFun>
	void SHA1transform(u32 state[5], u8 const buffer[64])
	{
		using namespace std;
		u32 a, b, c, d, e;

		CHAR64LONG16 workspace;
		CHAR64LONG16* block = &workspace;
		memcpy(block, buffer, 64);

		// Copy context->state[] to working vars
		a = state[0];
		b = state[1];
		c = state[2];
		d = state[3];
		e = state[4];
		// 4 rounds of 20 operations each. Loop unrolled.
		R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
		R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
		R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
		R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
		R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
		R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
		R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
		R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
		R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
		R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
		R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
		R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
		R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
		R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
		R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
		R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
		R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
		R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
		R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
		R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
		// Add the working vars back into context.state[]
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
	}

#ifdef VERBOSE
	void SHAPrintContext(sha_ctx *context, char *msg)
	{
		using namespace std;
		printf("%s (%d,%d) %x %x %x %x %x\n"
			, msg, (unsigned int)context->count[0]
			, (unsigned int)context->count[1]
			, (unsigned int)context->state[0]
			, (unsigned int)context->state[1]
			, (unsigned int)context->state[2]
			, (unsigned int)context->state[3]
			, (unsigned int)context->state[4]);
	}
#endif

	template <class BlkFun>
	void internal_update(sha_ctx* context, u8 const* data, u32 len)
	{
		using namespace std;
		u32 i, j;	// JHB

#ifdef VERBOSE
		SHAPrintContext(context, "before");
#endif
		j = (context->count[0] >> 3) & 63;
		if ((context->count[0] += len << 3) < (len << 3)) context->count[1]++;
		context->count[1] += (len >> 29);
		if ((j + len) > 63)
		{
			memcpy(&context->buffer[j], data, (i = 64-j));
			SHA1transform<BlkFun>(context->state, context->buffer);
			for ( ; i + 63 < len; i += 64)
			{
				SHA1transform<BlkFun>(context->state, &data[i]);
			}
			j = 0;
		}
		else
		{
			i = 0;
		}
		memcpy(&context->buffer[j], &data[i], len - i);
#ifdef VERBOSE
		SHAPrintContext(context, "after ");
#endif
	}

#if !defined BOOST_BIG_ENDIAN && !defined BOOST_LITTLE_ENDIAN
	bool is_big_endian()
	{
		u32 test = 1;
		return *reinterpret_cast<u8*>(&test) == 0;
	}
#endif
}

// SHA1Init - Initialize new context

void SHA1_init(sha_ctx* context)
{
    // SHA1 initialization constants
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}


// Run your data through this.

void SHA1_update(sha_ctx* context, u8 const* data, u32 len)
{
	// GCC standard defines for endianness
	// test with: cpp -dM /dev/null
#if defined BOOST_BIG_ENDIAN
	internal_update<big_endian_blk0>(context, data, len);
#elif defined BOOST_LITTLE_ENDIAN
	internal_update<little_endian_blk0>(context, data, len);
#else
	// select different functions depending on endianess
	// and figure out the endianess runtime
	if (is_big_endian())
		internal_update<big_endian_blk0>(context, data, len);
	else
		internal_update<little_endian_blk0>(context, data, len);
#endif
}


// Add padding and return the message digest.

void SHA1_final(u8* digest, sha_ctx* context)
{
	u8 finalcount[8];

	for (u32 i = 0; i < 8; ++i)
	{
		// Endian independent
		finalcount[i] = static_cast<u8>(
			(context->count[(i >= 4 ? 0 : 1)]
			>> ((3-(i & 3)) * 8) ) & 255);
	}

	SHA1_update(context, reinterpret_cast<u8 const*>("\200"), 1);
	while ((context->count[0] & 504) != 448)
		SHA1_update(context, reinterpret_cast<u8 const*>("\0"), 1);
	SHA1_update(context, finalcount, 8);  // Should cause a SHA1transform()

	for (u32 i = 0; i < 20; ++i)
	{
		digest[i] = static_cast<unsigned char>(
			(context->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
	}
}

} // libtorrent namespace

/************************************************************

-----------------
Modified 7/98 
By James H. Brown <jbrown@burgoyne.com>
Still 100% Public Domain

Corrected a problem which generated improper hash values on 16 bit machines
Routine SHA1Update changed from
	void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned int
len)
to
	void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned
long len)

The 'len' parameter was declared an int which works fine on 32 bit machines.
However, on 16 bit machines an int is too small for the shifts being done
against
it.  This caused the hash function to generate incorrect values if len was
greater than 8191 (8K - 1) due to the 'len << 3' on line 3 of SHA1Update().

Since the file IO in main() reads 16K at a time, any file 8K or larger would
be guaranteed to generate the wrong hash (e.g. Test Vector #3, a million
"a"s).

I also changed the declaration of variables i & j in SHA1Update to 
unsigned long from unsigned int for the same reason.

These changes should make no difference to any 32 bit implementations since
an
int and a long are the same size in those environments.

--
I also corrected a few compiler warnings generated by Borland C.
1. Added #include <process.h> for exit() prototype
2. Removed unused variable 'j' in SHA1Final
3. Changed exit(0) to return(0) at end of main.

ALL changes I made can be located by searching for comments containing 'JHB'
-----------------
Modified 8/98
By Steve Reid <sreid@sea-to-sky.net>
Still 100% public domain

1- Removed #include <process.h> and used return() instead of exit()
2- Fixed overwriting of finalcount in SHA1Final() (discovered by Chris Hall)
3- Changed email address from steve@edmweb.com to sreid@sea-to-sky.net

-----------------
Modified 4/01
By Saul Kravitz <Saul.Kravitz@celera.com>
Still 100% PD
Modified to run on Compaq Alpha hardware.  

-----------------
Converted to C++ 6/04
By Arvid Norberg <arvidn@sourceforge.net>
1- made the input buffer const, and made the
   previous SHA1HANDSOFF implicit
2- uses C99 types with size guarantees
   from boost
3- if none of BOOST_BIG_ENDIAN or BOOST_LITTLE_ENDIAN
   are defined, endianess is determined
   at runtime. templates are used to duplicate
   the transform function for each endianess
4- using anonymous namespace to avoid external
   linkage on internal functions
5- using standard C++ includes
6- made API compatible with openssl

still 100% PD
*/

/*
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/
