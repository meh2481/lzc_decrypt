#include "LZC.h"
#include <iostream>
#include <string>
#include <vector>
#include <stdlib.h>
#include <cstdio>
#include <sstream>
using namespace std;

typedef struct
{
	uint32_t unknown0;
	uint32_t width;
	uint32_t height;
	uint32_t unknown1[5];
	//uint8_t data[]
} texHeader;

//Save the given image to an external file
bool saveImage(uint8_t* data, uint32_t width, uint32_t height, string sFilename)
{
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
		system("mkdir output");
		
		int iNum = 0;
		bool bTexHeader = false;
		for(uint64_t i = 0; i < fileSize; i++)	//Definitely not the fastest way to do it... but I don't care
		{
			texHeader th;
			if(memcmp(&(dataIn[i]), "LZC", 3) == 0)	//Found another header
			{
				bTexHeader = !bTexHeader;
				if(bTexHeader)
				{
					uint64_t headerPos = i - sizeof(texHeader);
					memcpy(&th, &(dataIn[headerPos]), sizeof(texHeader));
					//cout << "Tex header size: " << th.width << "," << th.height << endl;
					continue;
				}
				
				if(th.width > 2048 || th.width < 16) continue; 
				//if(dataIn[i + 5] == 'W') continue;
				LZC_SIZE_T decomp_size = LZC_GetDecompressedSize(&(dataIn[i]));
				if(decomp_size)
				{
					//if(iNum <=1) continue;
					//cout << "Found image at " << i << ". Decompressed size: " << decomp_size << endl;
					//for (int j = i; j < i + 10; j++)
					//	cout << dataIn[j] << endl;
					uint8_t* dataOut = (uint8_t*)malloc(decomp_size + 16);
					LZC_Decompress(&(dataIn[i]), dataOut);
					
					ostringstream oss;
					oss << "./output/img_" << ++iNum << "(" << th.width << "x" << th.height << ")" << ".brimage";
					FILE* fout = fopen(oss.str().c_str(), "wb");
					if(fout == NULL)
					{
						cerr << "Unable to open output file " << oss.str() << endl;
						continue;	//Skip
					}
					fwrite(dataOut, decomp_size, 1, fout);
					
					fclose(fout);
					free(dataOut);
				}
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
