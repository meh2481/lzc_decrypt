#ifdef _WIN32
	#include <windows.h>
#endif
#include "LZC.h"
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <stdlib.h>
#include <cstdio>
#include <cmath>
#include <sstream>
#include "FreeImage.h"
using namespace std;

bool g_bPieceTogether;
bool g_bAdd;
int32_t g_iOffset;

typedef struct
{
	uint32_t type;
	uint32_t width;
	uint32_t height;
	uint32_t unknown1[5];
	//uint8_t data[]
} texHeader;

typedef struct
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} paletteEntry;

typedef struct
{
	uint8_t* data;
	uint32_t size;
} chunk;

typedef struct  
{
	int32_t	 texOffset; // offset from start of file to TexDesc
	int32_t  texDataSize;
	int32_t  pieceOffset; // offset from start of file to PiecesDesc
} FrameDesc;

typedef struct
{
	float x;
	float y;
} Vec2;

typedef struct 
{
	Vec2 topLeft;
	Vec2 topLeftUV;
	Vec2 bottomRight;
	Vec2 bottomRightUV;
} piece;

typedef struct
{
	int32_t numPieces;
	//piece[]
} PiecesDesc;

//Defined by the anb file format
#define TYPE_256_COLOR			4
#define TYPE_UNCOMPRESSED		2

#define NUM_PALETTE_ENTRIES		256
#define MAGIC_IMAGE_TOOWIDE		10000	//Yay! These numbers are magic!
#define MAGIC_IMAGE_TOONARROW	2
#define MAGIC_IMAGE_TOOSHORT	2
#define MAGIC_IMAGE_TOOTALL		10000
#define MAGIC_TEX_TOOBIG		6475888
#define	MAGIC_TOOMANYPIECES		512

FIBITMAP* imageFromPixels(uint8_t* imgData, uint32_t width, uint32_t height)
{
	//return FreeImage_ConvertFromRawBits(imgData, width, height, width*4, 32, 0xFF0000, 0x00FF00, 0x0000FF, true);	//Doesn't seem to work
	FIBITMAP* curImg = FreeImage_Allocate(width, height, 32);
	FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(curImg);
	if(image_type == FIT_BITMAP)
	{
		int curPos = 0;
		unsigned pitch = FreeImage_GetPitch(curImg);
		BYTE* bits = (BYTE*)FreeImage_GetBits(curImg);
		bits += pitch * height - pitch;
		for(int y = height-1; y >= 0; y--)
		{
			BYTE* pixel = (BYTE*)bits;
			for(int x = 0; x < width; x++)
			{
				pixel[FI_RGBA_BLUE] = imgData[curPos++];
				pixel[FI_RGBA_GREEN] = imgData[curPos++];
				pixel[FI_RGBA_RED] = imgData[curPos++];
				pixel[FI_RGBA_ALPHA] = imgData[curPos++];
				pixel += 4;
			}
			bits -= pitch;
		}
	}
	return curImg;
}

FIBITMAP* PieceImage(uint8_t* imgData, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texHeader th, bool bAdd)
{
	if(!imgData)
		return FreeImage_Allocate(0,0,32);

	Vec2 OutputSize;
	Vec2 CenterPos;
	OutputSize.x = -maxul.x + maxbr.x;
	OutputSize.y = maxul.y - maxbr.y;
	CenterPos.x = -maxul.x;
	CenterPos.y = maxul.y;
	OutputSize.x = (uint32_t)(OutputSize.x + 0.5f);
	OutputSize.y = (uint32_t)(OutputSize.y + 0.5f);

	FIBITMAP* result = FreeImage_Allocate(OutputSize.x, OutputSize.y, 32);

	//Create image from this set of pixels
	FIBITMAP* curImg = imageFromPixels(imgData, th.width, th.height);

	//Patch image together from pieces
	for(list<piece>::iterator lpi = pieces.begin(); lpi != pieces.end(); lpi++)
	{
		FIBITMAP* imgPiece = FreeImage_Copy(curImg, 
											(int)((lpi->topLeftUV.x) * th.width + 0.5f), (int)((lpi->topLeftUV.y) * th.height + 0.5f), 
											(int)((lpi->bottomRightUV.x) * th.width + 0.5f), (int)((lpi->bottomRightUV.y) * th.height + 0.5f));
		
		//Paste this into the pieced image
		Vec2 DestPos = CenterPos;
		DestPos.x += lpi->topLeft.x;
		DestPos.y -= lpi->topLeft.y;	//y is negative here
		DestPos.x = (uint32_t)(DestPos.x + 0.5f);
		DestPos.y = (uint32_t)(DestPos.y + 0.5f);
		
		FreeImage_Paste(result, imgPiece, DestPos.x, DestPos.y, 256);
		FreeImage_Unload(imgPiece);
	}
	FreeImage_Unload(curImg);
	
	return result;
}

//Save the given image to an external file
bool saveImage(uint8_t* data, uint32_t dataSz, string sFilename, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texHeader th)
{
	vector<uint8_t> imgData;
	uint8_t* cur_data_ptr = data;
	if(th.type == TYPE_256_COLOR)
	{
		//Read in the image palette
		vector<paletteEntry> palEntries;
		for(uint32_t i = 0; i < NUM_PALETTE_ENTRIES; i++)
		{
			paletteEntry p;
			p.r = *cur_data_ptr++;
			p.g = *cur_data_ptr++;
			p.b = *cur_data_ptr++;
			p.a = *cur_data_ptr++;
			palEntries.push_back(p);
		}
		
		//Fill out our pixel data
		for(uint32_t i = 0; i < th.width * th.height; i++)
		{
			paletteEntry pixel = palEntries[*cur_data_ptr++];
			imgData.push_back(pixel.r);
			imgData.push_back(pixel.g);
			imgData.push_back(pixel.b);
			imgData.push_back(pixel.a);
		}
	}
	else if(th.type == TYPE_UNCOMPRESSED)
	{
		//Assume uncompressed
		for(uint32_t i = 0; i < th.width * th.height; i++)
		{
			imgData.push_back(*cur_data_ptr++);
			imgData.push_back(*cur_data_ptr++);
			imgData.push_back(*cur_data_ptr++);
			imgData.push_back(*cur_data_ptr++);
		}
	}
	else
		cout << "Warning: Unknown image type: " << th.type << endl;
	
	FIBITMAP* bmp;
	if(pieces.size())	//If we're patching pieces together (if g_bPieceTogether == true)
		bmp = PieceImage(imgData.data(), pieces, maxul, maxbr, th, g_bAdd);
	else
		bmp = FreeImage_ConvertFromRawBits(imgData.data(), th.width, th.height, th.width * 4, 32, 0x0000FF, 0x00FF00, 0xFF0000, true);
	
	if(!bmp) return false;
	//Save image as PNG
	cout << "Saving image " << sFilename << endl;
	bool bRet = FreeImage_Save(FIF_PNG, bmp, sFilename.c_str());
	FreeImage_Unload(bmp);
	return bRet;
}

bool DecompressANB(string sFilename)
{
	cout << "Decompressing anb file " << sFilename << endl;
	//Read the whole file into memory up front
	FILE* f = fopen(sFilename.c_str(), "rb");
	if(f == NULL)
	{
		cerr << "Unable to open input file " << sFilename << endl;
		return false;
	}
	fseek(f, 0, SEEK_END);
	size_t fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* dataIn = (uint8_t*)malloc(fileSize);
	fread(dataIn, fileSize, 1, f);
	fclose(f);
	
	//Figure out what we'll be naming the images
	string sName = sFilename;
	//First off, strip off filename extension
	size_t namepos = sName.find(".anb");
	if(namepos != string::npos)
		sName.erase(namepos);
	//Next, strip off any file path before it
	namepos = sName.rfind('/');
	if(namepos == string::npos)
		namepos = sName.rfind('\\');
	if(namepos != string::npos)
		sName.erase(0, namepos+1);
	
	//Spin through the file and try to find LZC-compressed images
	int iNum = 0;
	bool bTexHeader = true;
	texHeader th;
	FrameDesc fd;
	int32_t headerPos;
	int32_t startPos = 0;
	vector<chunk> vMultiChunkData;	//If an image is compressed over multiple chunks, hang onto previous ones and attempt to reconstruct it
	Vec2 maxul;
	Vec2 maxbr;
	maxul.x = maxul.y = maxbr.x = maxbr.y = 0;
	list<piece> pieces;
	for(int32_t i = 0; i < fileSize; i++)	//Definitely not the fastest way to do it... but I don't care
	{
		if(memcmp(&(dataIn[i]), "LZC", 3) == 0)	//Found another LZC header
		{
			//Check and see if there's a texture header here. If so, skip the LZC stuff, cause that's multichunk-LZC stuff our LZC decompressor can't handle
			texHeader thTest;
			headerPos = i - sizeof(texHeader);
			memcpy(&thTest, &(dataIn[headerPos]), sizeof(texHeader));
			//cout << "Tex header: " << thTest.width << "," << thTest.height << endl;
			if(thTest.width < MAGIC_IMAGE_TOOWIDE && thTest.width > MAGIC_IMAGE_TOONARROW &&
				thTest.height < MAGIC_IMAGE_TOOTALL && thTest.height > MAGIC_IMAGE_TOOSHORT &&
				thTest.width * thTest.height < MAGIC_TEX_TOOBIG &&
				!vMultiChunkData.size())	//Sanity check to be sure this is a valid header
			{
				//cout << "Tex header type: " << thTest.type << endl;
				
				//Save this
				memcpy(&th, &thTest, sizeof(texHeader));
					
				//Search for frame header if we're going to be piecing these together
				if(g_bPieceTogether)
				{
					if(iNum == 0)	//First one tells us where to start searching backwards from
						startPos = i;
					for(int k = startPos - sizeof(texHeader) - sizeof(FrameDesc); k > 0; k --)
					{
						memcpy(&fd, &(dataIn[k]), sizeof(FrameDesc));
						if(fd.texOffset != headerPos) continue;
						//Sanity check
						if(fd.texDataSize > MAGIC_TEX_TOOBIG) continue;
						if(fd.texOffset == 0 || fd.pieceOffset + sizeof(PiecesDesc) > fileSize) continue;
						
						//Ok, found our header. Grab pieces
						pieces.clear();
						PiecesDesc pd;
						//fd.pieceOffset -= g_iOffset;
						memcpy(&pd, &(dataIn[fd.pieceOffset]), sizeof(PiecesDesc));
						//cout << "Numpieces: " << pd.numPieces << endl;
						if(pd.numPieces < 0 || pd.numPieces > MAGIC_TOOMANYPIECES) continue;
						maxul.x = maxul.y = maxbr.x = maxbr.y = 0;
						for(int32_t j = 0; j < pd.numPieces; j++)
						{
							piece p;
							memcpy(&p, &(dataIn[fd.pieceOffset+j*sizeof(piece)+sizeof(PiecesDesc)]), sizeof(piece));
							//Store our maximum values, so we know how large the image is
							if(p.topLeft.x < maxul.x)
								maxul.x = p.topLeft.x;
							if(p.topLeft.y > maxul.y)
								maxul.y = p.topLeft.y;
							if(p.bottomRight.x > maxbr.x)
								maxbr.x = p.bottomRight.x;
							if(p.bottomRight.y < maxbr.y)
								maxbr.y = p.bottomRight.y;
							pieces.push_back(p);
						}
						break;	//Got framedesc properly
					}
				}
				continue;	//Skip LZC chunked data info cause it causes these LZC functions to crash. We'll patch chunks together ourselves
			}
			
			//Alright, we've got another LZC chunk
			LZC_SIZE_T decomp_size = LZC_GetDecompressedSize(&(dataIn[i]));
			if(decomp_size)	//Sanity check; if this is zero; something's gone wrong
			{
				bool bChunk = false;	//If we need to read multiple chunks for this image
				
				LZC_SIZE_T size_needed = 0;
				if(th.type == TYPE_256_COLOR)
					size_needed = th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry);
				else if(th.type == TYPE_UNCOMPRESSED)
					size_needed = th.width * th.height * 4;
				else
					cout << "Warning: Unknown texture header type: " << th.type << endl;
				
				if(decomp_size < size_needed)	//Smaller than what we need; probably a chunk
					bChunk = true;	//Save this chunk for later
				
				//Larger than we'll possibly need; ignore
				if(decomp_size > size_needed) continue;
					
				//Decompress the data
				//cout << "Decomp size: " << decomp_size << endl;
				//cout << "Need: " << size_needed << endl;
				uint8_t* dataOut = (uint8_t*)malloc(decomp_size);
				LZC_Decompress(&(dataIn[i]), dataOut);
				
				if(!bChunk)	//One full image; go ahead and save it
				{
					ostringstream oss;
					oss << "./output/" << sName << '_' << ++iNum << ".png";
					saveImage(dataOut, decomp_size, oss.str(), pieces, maxul, maxbr, th);
					free(dataOut);
				}
				else	//A chunk; hang onto it
				{
					uint32_t totalSz = decomp_size;
					for(vector<chunk>::iterator it = vMultiChunkData.begin(); it != vMultiChunkData.end(); it++)
						totalSz += it->size;	//How far along are we?
					
					if(totalSz > size_needed)	//Too much; discard
						continue;
					
					chunk c;
					c.data = dataOut;
					c.size = decomp_size;
					vMultiChunkData.push_back(c);
					
					//cout << "Have: " << totalSz << endl;
					if(totalSz >= size_needed)	//If we've got all the chunks we need, patch it together!
					{
						uint8_t* finalimg = (uint8_t*)malloc(totalSz);
						uint32_t curCopyPos = 0;
						for(vector<chunk>::iterator it = vMultiChunkData.begin(); it != vMultiChunkData.end(); it++)
						{
							memcpy(finalimg + curCopyPos, it->data, it->size);
							curCopyPos += it->size;
							free(it->data);	//Done with this memory
						}
						vMultiChunkData.clear();	//Clear this all out cause we're done with it
						
						//If we've got too MUCH data now, spit out a warning and attempt to continue
						//if(totalSz != size_needed)
						//{
						//	cout << "Warning: LZC chunk size mismatch in file " << sFilename << " image " << iNum+1 << ". Got " 
						//		<< totalSz << " bytes, expected " << size_needed << endl;
							//continue;	//Skip dis
						//}
						
						//Done. We can save the image
						ostringstream oss;
						oss << "./output/" << sName << '_' << ++iNum << ".png";
						saveImage(finalimg, decomp_size, oss.str(), pieces, maxul, maxbr, th);
						free(finalimg);
					}
				}
			}
			else
				cerr << "ERR decompressing image " << iNum << endl;
		}
	}
	free(dataIn);
}

int usage()
{
	cout << "Usage: lzc_decrypt [-noadd|-nopiece|-offset=x] file.anb" << endl;
	return 1;
}

int main(int argc, char** argv)
{
	g_bPieceTogether = true;
	g_bAdd = true;
	g_iOffset = 24;
	
	list<string> sFilenames;
	//Parse commandline
	for(int i = 1; i < argc; i++)
	{
		string s = argv[i];
		if(s == "-noadd")
			g_bAdd = false;
		else if(s == "-nopiece")
			g_bPieceTogether = false;
		else if(s.find(".anb") != string::npos)
			sFilenames.push_back(s);
		else if(s.find("-offset=") != string::npos)
		{
			size_t pos = s.find("-offset=") + 8;
			s.erase(0, pos);
			istringstream iss(s);
			if(!(iss >> g_iOffset))
				g_iOffset = 24;
		}
		else
			cout << "Unrecognized commandline argument " << s << endl;
	}
	
	if(!sFilenames.size())
		return usage();
	
	//Create the folder that we'll be outputting our images into
#ifdef _WIN32
	CreateDirectory(TEXT("output"), NULL);
#else
	system("mkdir -p output");
#endif
	
	//Decompress any .anb files we're given
	for(list<string>::iterator i = sFilenames.begin(); i != sFilenames.end(); i++)
		DecompressANB((*i));
	
	return 0;
}
























