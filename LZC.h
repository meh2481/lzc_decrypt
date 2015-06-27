#pragma once
#ifndef LZC_H

/* LZC Compression Library
* By Shane@Calimlim.com

Notes:
* I've tried to make this C-compatible, but I'm a C++ programmer, so I may have missed a few things. (ok, probably lots of problems)
* I've also tried to make this platform indepenent -- it should be pretty good there, but it DOES expect a byte to be 8 bits

TODO:
* Endian-safeness
* ensure uses of LZC_SIZE_T vs u32 are correct
*/

//
// Options
//
// whether to compile in the compress/decompress routines

#define LZC_COMPRESS
#define LZC_DECOMPRESS

//
// Memory Management
//

#include <stdio.h>   // memcpy/memset
#include <string.h>
#define LZC_MEMCPY( dst, src, size ) memcpy( dst, src, size )
#define LZC_MEMSET( dst, val, size ) memset( dst, val, size )
#define LZC_SAFEMEMCPY( dst, src, size ) \
	for( int i = 0; i != size; ++i ) \
	{ \
		*( dst + i ) = *( src + i ); \
	}

//
// MSVC Compatibility
//

// MSVC
#ifdef _MSC_VER
	#if _MSC_VER < 1300
	   typedef signed   char  int8_t;
	   typedef unsigned char  uint8_t;
	   typedef signed   short int16_t;
	   typedef unsigned short uint16_t;
	   typedef signed   int   int32_t;
	   typedef unsigned int   uint32_t;
	#else
	   typedef signed   __int8  int8_t;
	   typedef unsigned __int8  uint8_t;
	   typedef signed   __int16 int16_t;
	   typedef unsigned __int16 uint16_t;
	   typedef signed   __int32 int32_t;
	   typedef unsigned __int32 uint32_t;
	#endif
	typedef signed   __int64 int64_t;
	typedef unsigned __int64 uint64_t;
// any C99 compliant compiler
#else
	#include <stdint.h>
#endif

//
// C Compatibility
//

#ifdef __cplusplus
	#define LZC_SIZE_T size_t
#else
	#define LZC_SIZE_T uint32_t
#endif

typedef struct _LZC_Header
{
	uint8_t		sig[3];
	uint8_t		markerByte;
	uint32_t	decompressedSize;
	uint32_t	compressedSize;
} LZC_Header;

#ifdef LZC_COMPRESS

	//! LZC_GetMaxCompressedSize()
	LZC_SIZE_T LZC_GetMaxCompressedSize( const LZC_SIZE_T inSize );

	//! LZC_Compress()
	LZC_SIZE_T LZC_Compress( uint8_t* in, const LZC_SIZE_T inSize, uint8_t* out, const bool targetIsBigEndian = false );

#endif // LZC_COMPRESS

#ifdef LZC_DECOMPRESS

	//! LZC_GetDecompressedSize()
	/*!
	* Returns 0 if data appears invalid
	*/
	LZC_SIZE_T LZC_GetDecompressedSize( const uint8_t* const in );

	//! LZC_Decompress()
	uint32_t LZC_Decompress( uint8_t* in, uint8_t* out );

#endif // LZC_DECOMPRESS

#endif // LZC_H
