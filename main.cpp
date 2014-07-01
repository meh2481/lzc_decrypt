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

#define PIXEL paletteEntry

#define NUM_PALETTE_ENTRIES	256

//Save the given image to an external file
bool saveImage(uint8_t* data, uint32_t dataSz, uint32_t width, uint32_t height, string sFilename)
{
	/*FILE* fout = fopen(sFilename.c_str(), "wb");
	if(fout == NULL)
	{
		cerr << "Unable to open output file " << sFilename << endl;
		return false;	//Skip
	}
	fwrite(data, dataSz, 1, fout);
	
	fclose(fout);*/
	
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
	
	if(sFilename.find(".anb") != string::npos)
	{
		system("mkdir -p output");
		
		int iNum = 0;
		bool bTexHeader = true;
		texHeader th;
		vector<uint8_t> vMultiChunkData;	//If an image is compressed over multiple chunks, hang onto previous ones and attempt to reconstruct it
		for(uint64_t i = 0; i < fileSize; i++)	//Definitely not the fastest way to do it... but I don't care
		{
			if(memcmp(&(dataIn[i]), "LZC", 3) == 0)	//Found another header
			{
				texHeader thTest;
				uint64_t headerPos = i - sizeof(texHeader);
				memcpy(&thTest, &(dataIn[headerPos]), sizeof(texHeader));
				if(thTest.width > 2048 || thTest.width < 16) 
				{
					//cout << "1 Tex header size: " << thTest.width << "," << thTest.height << endl;
					//continue;
				}
				else
				{
					th = thTest;
					memcpy(&th, &thTest, sizeof(texHeader));
					//cout << "2 Tex header size: " << thTest.width << "," << thTest.height << endl;
					continue;
				}
				//cout << "Tex header size: " << th.width << "," << th.height << endl;
				//if(bTexHeader)
				
				
				
				//if(dataIn[i + 5] == 'W') continue;
				LZC_SIZE_T decomp_size = LZC_GetDecompressedSize(&(dataIn[i]));
				if(decomp_size)
				{
					//cout << "Decompressed size: " << decomp_size << ", needs to be at least " << th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry) << endl;
					if(decomp_size < th.width * th.height + NUM_PALETTE_ENTRIES * sizeof(paletteEntry)) 
					{
						//cout << "Skipping" << endl;
						continue;	//Skip if this is obviously a bad size
					}
					//cout << "Not skipping" << endl;
					//if(iNum <=1) continue;
					//cout << "Found image at " << i << ". Decompressed size: " << decomp_size << endl;
					//for (int j = i; j < i + 10; j++)
					//	cout << dataIn[j] << endl;
					uint8_t* dataOut = (uint8_t*)malloc(decomp_size);
					LZC_Decompress(&(dataIn[i]), dataOut);
					
					ostringstream oss;
					oss << "./output/img_" << ++iNum << ".png";
					saveImage(dataOut, decomp_size, th.width, th.height, oss.str());
					free(dataOut);
				}
				else
					cerr << "ERR decompressing image " << iNum << endl;
			}
		}
	}
	else
	{
		
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
	
	DecompressANB(argv[1]);
	
	return 0;
}
