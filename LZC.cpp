#include "LZC.h"

//#define DBG2FILE
#ifdef DBG2FILE
	#define DBG( ... ) fprintf( dbgfh, __VA_ARGS__ )
#else
	#define DBG( ... )
#endif

#ifdef LZC_COMPRESS

	void LZC_SwapEndian( uint16_t* data ) { *data = ( (*data & 0xFF00) >> 8 ) | ( (*data & 0x00FF) << 8 ); }
	void LZC_SwapEndian( uint32_t* data ) { *data = ( (*data & 0xFF000000) >> 24 ) | ( (*data & 0x00FF0000) >> 8 ) | ( (*data & 0x0000FF00) << 8 ) | ( (*data & 0x000000FF) << 24 ); }

	LZC_SIZE_T LZC_GetMaxCompressedSize( const LZC_SIZE_T inSize )
	{
		return inSize * 3 + sizeof( LZC_Header );
		//return ( inSize / 256 + 1 ) * 257 + sizeof( LZC_Header );	// not perfect, slightly over-estimates
	}

	#define LZC_LEN_BITS	5
	#define LZC_MAX_DIST	0x7ff
	//#define LZC_MAX_DIST	0xfff
	#define LZC_MIN_LEN		3
	#define LZC_MAX_LEN		0x1f
	//#define LZC_MAX_LEN		0xf
	#define LZC_PACK_CMD( data, length, offset ) \
		data = ( matchLength - LZC_MIN_LEN ) | ( matchOffset << LZC_LEN_BITS );
	#define LZC_UNPACK_CMD( data, length, offset ) \
		{ \
			length = ( data & LZC_MAX_LEN ) + LZC_MIN_LEN; \
			offset = ( data >> LZC_LEN_BITS ) & LZC_MAX_DIST; \
		}

	LZC_SIZE_T LZC_Compress( uint8_t* in, const LZC_SIZE_T inSize, uint8_t* out, const bool targetIsBigEndian )
	{
#ifdef DBG2FILE
		FILE* dbgfh = fopen( "c:/dev/compress.txt", "wb" );
#endif
		uint8_t* outStart = out;

		uint8_t markerByte;
		{
			const uint8_t* input = in;
			uint32_t valueCounts[ 256 ];
			LZC_MEMSET( valueCounts, 0, sizeof( valueCounts ) );
			for( LZC_SIZE_T inPos = 0; inPos != inSize; ++inPos, ++input )
			{
				++valueCounts[ *input ];
			}
			markerByte = 0;
			for( uint32_t i = 1; i != 256; ++i )
			{
				if( valueCounts[ i ] < valueCounts[ markerByte ] )
				{
					markerByte = i;
				}
			}
		}
		//uint8_t markerByte = 122;

		LZC_Header* header = reinterpret_cast< LZC_Header* >( out );
		out += sizeof( LZC_Header );
		header->sig[0]           = 'L';
		header->sig[1]           = 'Z';
		header->sig[2]           = 'C';
		header->markerByte       = markerByte;
		header->decompressedSize = inSize;

		uint8_t* minWindow = in;
		uint8_t* maxWindow = in + inSize;
		uint8_t* window;
		LZC_SIZE_T inBytesLeft = inSize;
		for( LZC_SIZE_T inPos = 0; inPos != inSize; ++inPos )
		{
			// literal marker byte
			if( *in == markerByte )
			{
DBG( "literal marker\n" );
				out[0] = markerByte;
				out[1] = 0;
				out[2] = 0;
				out += 3;
				--inBytesLeft;
				++in;
				continue;
			}

			// not enough input left to consider compressing
			if( inBytesLeft <= LZC_MIN_LEN )
			{
DBG( "literal %02X\n", *in );
				*out = *in;
				++out;
				--inBytesLeft;
				++in;
				continue;
			}

			window = in - LZC_MAX_DIST;
			if( window < minWindow ) { window = minWindow; }	// LHS fail
			uint32_t matchOffset = 0;
			uint32_t matchLength = 0;
			for( ; window != in ; ++window )
			{
				uint32_t thisMatchLength = 0;
				uint8_t* cmpSrc = window;
				uint8_t* cmpDst = in;
				while( *cmpSrc == *cmpDst && thisMatchLength < LZC_MAX_LEN && cmpDst < maxWindow )	// LHS fail
				{
					++thisMatchLength;
					++cmpSrc;
					++cmpDst;
				}
				if( thisMatchLength >= LZC_MIN_LEN && thisMatchLength > matchLength )
				{
					matchLength = thisMatchLength;
					matchOffset = in - window;
					if( thisMatchLength == LZC_MAX_LEN ) { break; }	// this match is as good as any others we'll find
				}
			}

			// literal byte
			if( matchLength == 0 )
			{
DBG( "literal %02X\n", *in );
				*out = *in;
				++out;
				--inBytesLeft;
				++in;
			}
			else
			{
				out[0] = markerByte;
				uint16_t data;
				LZC_PACK_CMD( data, matchLength, matchOffset )
				if( targetIsBigEndian )
				{
					LZC_SwapEndian( &data );
				}
				//uint16_t data = ( matchLength - LZC_MIN_LEN ) | ( matchOffset << 4 );
				//uint16_t data = ( matchLength << 12 ) | matchOffset;
				{
					uint32_t length;
					uint32_t offset;
					LZC_UNPACK_CMD( data, length, offset )
					DBG( "cmd offset[%u] length[%u]\n", offset, length );
				}
//for( int i = 0; i != matchLength; ++i )
//{
//	DBG( "%02X ", in[ i ] );
//}
//DBG( "\n" );
				memcpy( &out[1], &data, sizeof( uint16_t ) );
				out += 3;
				in += matchLength;
				inPos += matchLength - 1;	// for loop adds 1
			}
		}

#ifdef DBG2FILE
		fclose( dbgfh );
#endif
		header->compressedSize = out - outStart;
		if( targetIsBigEndian )
		{
			LZC_SwapEndian( &header->decompressedSize );
			LZC_SwapEndian( &header->compressedSize );
		}
		return out - outStart;
	}

#endif // LZC_COMPRESS

#ifdef LZC_DECOMPRESS

	LZC_SIZE_T LZC_GetDecompressedSize( const uint8_t* const in )
	{
		const LZC_Header* const header = reinterpret_cast< const LZC_Header* const >( in );
		if( header->sig[0] == 'L' && header->sig[1] == 'Z' && header->sig[2] == 'C' )
		{
			return static_cast< LZC_SIZE_T >( header->decompressedSize );
		}
		return 0;
	}

	//MEH: Return how many compressed bytes were read
	uint32_t LZC_Decompress( uint8_t* in, uint8_t* out )
	{
#ifdef DBG2FILE
		FILE* dbgfh = fopen( "c:/dev/decompress.txt", "wb" );
#endif
		LZC_Header* header = reinterpret_cast< LZC_Header* >( in );
		in += sizeof( LZC_Header );
		uint8_t markerByte = header->markerByte;
		uint8_t* end = in + header->compressedSize - sizeof( LZC_Header );
		while( in != end )
		{
			uint8_t curByte = *in;
			++in;
			if( curByte == markerByte )
			{
				if( in[0] == 0 && in[1] == 0 )
				{
DBG( "literal marker\n" );
					in += 2;
					*out = markerByte;
					++out;
				}
				else
				{
					uint16_t data;
					memcpy( &data, in, sizeof( uint16_t ) );
					uint32_t length;
					uint32_t offset;
					LZC_UNPACK_CMD( data, length, offset )
					//uint32_t length = ( data & 0xf ) + LZC_MIN_LEN;
					//uint32_t offset = ( data >> 4 ) & 0xfff;
DBG( "cmd offset[%u] length[%u]\n", offset, length );
					LZC_SAFEMEMCPY( out, out - offset, length );
//for( int i = 0; i != length; ++i )
//{
//	DBG( "%02X ", out[ i ] );
//}
//DBG( "\n" );
					out += length;
					in += 2;
				}
			}
			else
			{
DBG( "literal %02X\n", curByte );
				*out = curByte;
				++out;
			}
		}
#ifdef DBG2FILE
		fclose( dbgfh );
#endif
		return header->compressedSize;
	}

#endif // LZC_DECOMPRESS
