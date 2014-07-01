#include "LZC.h"
#include <iostream>
#include <string>
#include <vector>
#include <stdlib.h>
#include <cstdio>
#include <sstream>
#include "FreeImage.h"
using namespace std;

typedef struct
{
	uint32_t unknown0;
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

#define NUM_PALETTE_ENTRIES		256
#define MAGIC_IMAGE_TOOWIDE		2048	//Yay! These numbers are magic!
#define MAGIC_IMAGE_TOONARROW	16

//Save the given image to an external file
bool saveImage(uint8_t* data, uint32_t dataSz, uint32_t width, uint32_t height, string sFilename)
{
	//Read in the image palette
	vector<paletteEntry> palEntries;
	uint8_t* cur_data_ptr = data;
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
	vector<uint8_t> imgData;
	for(uint32_t i = 0; i < width * height; i++)
	{
		paletteEntry pixel = palEntries[*cur_data_ptr++];
		imgData.push_back(pixel.r);
		imgData.push_back(pixel.g);
		imgData.push_back(pixel.b);
		imgData.push_back(pixel.a);
	}
	
	//Save image as PNG
	FIBITMAP* bmp = FreeImage_ConvertFromRawBits(imgData.data(), width, height, width * 4, 32, 0x0000FF, 0x00FF00, 0xFF0000, true);
	if(!bmp) return false;
	bool bRet = FreeImage_Save(FIF_PNG, bmp, sFilename.c_str());
	FreeImage_Unload(bmp);
	return bRet;
}

bool DecompressANB(string sFilename)
{
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
	
	//Create the folder that we'll be outputting our images into (TODO: More cross-platform way of doing this)
	system("mkdir -p output");
	
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
	uint64_t headerPos;
	vector<chunk> vMultiChunkData;	//If an image is compressed over multiple chunks, hang onto previous ones and attempt to reconstruct it
	for(uint64_t i = 0; i < fileSize; i++)	//Definitely not the fastest way to do it... but I don't care
	{
		if(memcmp(&(dataIn[i]), "LZC", 3) == 0)	//Found another LZC header
		{
			//Check and see if there's a texture header here. If so, skip the LZC stuff, cause that's multichunk-LZC stuff our LZC decompressor can't handle
			texHeader thTest;
			headerPos = i - sizeof(texHeader);
			memcpy(&thTest, &(dataIn[headerPos]), sizeof(texHeader));
			if(thTest.width < MAGIC_IMAGE_TOOWIDE && thTest.width > MAGIC_IMAGE_TOONARROW)	//Sanity check to be sure this is a valid header
			{
				th = thTest;
				memcpy(&th, &thTest, sizeof(texHeader));
				continue;	//Skip LZC chunked data info cause it causes these LZC functions to crash. We'll patch chunks together ourselves
			}
			
			//Alright, we've got another LZC chunk
			LZC_SIZE_T decomp_size = LZC_GetDecompressedSize(&(dataIn[i]));
			if(decomp_size)	//Sanity check; if this is zero; something's gone wrong
			{
				bool bChunk = false;	//If we need to read multiple chunks for this image
				
				if(decomp_size < th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry))	//Smaller than what we need; probably a chunk
					bChunk = true;	//Save this chunk for later
					
				//Decompress the data
				uint8_t* dataOut = (uint8_t*)malloc(decomp_size);
				LZC_Decompress(&(dataIn[i]), dataOut);
				
				if(!bChunk)	//One full image; go ahead and save it
				{
					ostringstream oss;
					oss << "./output/" << sName << '_' << ++iNum << ".png";
					saveImage(dataOut, decomp_size, th.width, th.height, oss.str());
					free(dataOut);
				}
				else	//A chunk; hang onto it
				{
					chunk c;
					c.data = dataOut;
					c.size = decomp_size;
					vMultiChunkData.push_back(c);
					uint32_t totalSz = 0;
					for(vector<chunk>::iterator it = vMultiChunkData.begin(); it != vMultiChunkData.end(); it++)
						totalSz += it->size;	//How far along are we?
					if(totalSz >= th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry))	//If we've got all the chunks we need, patch it together!
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
						if(totalSz != th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry))
							cout << "Warning: LZC chunk size mismatch in file " << sFilename << " image " << iNum+1 << ". Got " 
								<< totalSz << " bytes, expected " << th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry) << endl;
						
						//Done. We can save the image
						ostringstream oss;
						oss << "./output/" << sName << '_' << ++iNum << ".png";
						saveImage(finalimg, decomp_size, th.width, th.height, oss.str());
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

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		cout << "Usage: lzc_decrypt [file.anb]" << endl;
		return 1;
	}
	
	//Decompress any .anb files we're given
	for(int i = 1; i < argc; i++)
		DecompressANB(argv[i]);
	
	return 0;
}
