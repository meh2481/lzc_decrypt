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
bool g_bNoSheet;

int offsetX = 1;
int offsetY = 2;

typedef struct
{
	uint32_t headerSz;
	uint32_t unk0;
	uint32_t numFrames;
	uint32_t numAnimations;
	uint32_t frameListPtr;	//point to listPtr of FrameDesc
	uint32_t animListPtr;	//point to listPtr of anim
	//... 					//Probably more stuff we don't care about
} anbHeader;

typedef struct
{
	uint32_t offset;	//generic pointer offset list
} listPtr;//[]

typedef struct
{
	uint32_t animIDHash;
	uint32_t numFrames;
	uint32_t unk0;
	uint32_t animListPtr;	//point to listPtr of animFrame
	uint32_t unk1;
} anim;

typedef struct
{
	uint32_t frameNo;
	uint32_t unk0[2];
} animFrame;

typedef struct
{
	uint32_t type;
	uint32_t width;
	uint32_t height;
	uint32_t unknown1[5];
	//uint8_t data[]	//Followed by image data
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
	float    minx;
	float 	 maxx;
	float 	 miny;
	float    maxy;
	int32_t	 texOffset; //point to texHeader
	int32_t  texDataSize;
	int32_t  pieceOffset; //point to PiecesDesc
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
	//piece[]	//followed by numPieces pieces
} PiecesDesc;


//For helping reconstruct data from LZC chunks
typedef struct
{
	uint8_t* data;
	uint32_t size;
} chunk;

//For helping piece animations together at the same size
typedef struct
{
	Vec2 maxul;
	Vec2 maxbr;
} extents;

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
#define FIRST_LZC_CHUNK_SZ      0x48

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

FIBITMAP* PieceImage(uint8_t* imgData, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texHeader th)
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
FIBITMAP* createImage(uint8_t* data, uint32_t dataSz, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texHeader th)
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
	if(pieces.size())	//If we're patching pieces together here...
		bmp = PieceImage(imgData.data(), pieces, maxul, maxbr, th);
	else
		bmp = FreeImage_ConvertFromRawBits(imgData.data(), th.width, th.height, th.width * 4, 32, 0x0000FF, 0x00FF00, 0xFF0000, true);
	
	return bmp;
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
		
	//Read in header
	anbHeader ah;
	memcpy(&ah, dataIn, sizeof(anbHeader));
	
	//Save FrameDescs
	vector<FrameDesc> vFrames;
	vFrames.reserve(ah.numFrames);
	for(int i = 0; i < ah.numFrames; i++)
	{
		FrameDesc fd;
		listPtr lp;
		memcpy(&lp, &dataIn[ah.frameListPtr+i*sizeof(listPtr)], sizeof(listPtr));
		memcpy(&fd, &dataIn[lp.offset], sizeof(FrameDesc));
		
		vFrames.push_back(fd);
	}
	
	//Save pieces
	vector<list<piece> > pieces;
	vector<extents> frameExtents;
	pieces.reserve(ah.numFrames);
	frameExtents.reserve(ah.numFrames);
	for(int i = 0; i < ah.numFrames; i++)
	{
		PiecesDesc pd;
		memcpy(&pd, &dataIn[vFrames[i].pieceOffset], sizeof(PiecesDesc));
		
		list<piece> lp;
		
		if(g_bPieceTogether)
		{
			extents e;
			e.maxul.x = e.maxul.y = e.maxbr.x = e.maxbr.y = 0;
			for(int j = 0; j < pd.numPieces; j++)
			{
				piece p;
				memcpy(&p, &dataIn[vFrames[i].pieceOffset + sizeof(PiecesDesc) + j * sizeof(piece)], sizeof(piece));
				
				//Store maximum extents for this piece (rather than trusting the fields in FrameDesc, because those seem to sometimes be offsets)
				if(p.topLeft.x < e.maxul.x)
					e.maxul.x = p.topLeft.x;
				if(p.topLeft.y > e.maxul.y)
					e.maxul.y = p.topLeft.y;
				if(p.bottomRight.x > e.maxbr.x)
					e.maxbr.x = p.bottomRight.x;
				if(p.bottomRight.y < e.maxbr.y)
					e.maxbr.y = p.bottomRight.y;
				
				lp.push_back(p);
			}
			frameExtents.push_back(e);
		}
		pieces.push_back(lp);
	}
	
	//Save animation frames
	vector<vector<uint32_t> > vAnims;
	vAnims.reserve(ah.numAnimations);
	for(int i = 0; i < ah.numAnimations; i++)
	{
		anim a;
		listPtr lp1;
		memcpy(&lp1, &dataIn[ah.animListPtr+i*sizeof(listPtr)], sizeof(listPtr));
		memcpy(&a, &dataIn[lp1.offset], sizeof(anim));
		
		vector<uint32_t> vAnimFrames;
		vAnimFrames.reserve(a.numFrames);
		for(int j = 0; j < a.numFrames; j++)
		{
			animFrame af;
			listPtr lp2;
			memcpy(&lp2, &dataIn[a.animListPtr+j*sizeof(listPtr)], sizeof(listPtr));
			memcpy(&af, &dataIn[lp2.offset], sizeof(animFrame));
			
			vAnimFrames.push_back(af.frameNo);
		}
		vAnims.push_back(vAnimFrames);
	}
	
	//Save animation extents (maximum of each frame extent)
	vector<extents> animExtents;
	animExtents.reserve(ah.numAnimations);
	for(int i = 0; i < ah.numAnimations; i++)
	{
		extents e;
		e.maxul.x = e.maxul.y = e.maxbr.x = e.maxbr.y = 0;
		
		for(vector<uint32_t>::iterator j = vAnims[i].begin(); j != vAnims[i].end(); j++)
		{
			if(frameExtents[*j].maxul.x < e.maxul.x)
				e.maxul.x = frameExtents[*j].maxul.x;
			if(frameExtents[*j].maxul.y > e.maxul.y)
				e.maxul.y = frameExtents[*j].maxul.y;
			if(frameExtents[*j].maxbr.x > e.maxbr.x)
				e.maxbr.x = frameExtents[*j].maxbr.x;
			if(frameExtents[*j].maxbr.y < e.maxbr.y)
				e.maxbr.y = frameExtents[*j].maxbr.y;
		}
		animExtents.push_back(e);
	}
	
	//Decompress image data
	vector<chunk> imageData;
	vector<texHeader> texHead;
	imageData.reserve(ah.numFrames);
	texHead.reserve(ah.numFrames);
	for(int i = 0; i < ah.numFrames; i++)
	{
		uint32_t curPtr = vFrames[i].texOffset;
		
		texHeader th;
		memcpy(&th, &dataIn[curPtr], sizeof(texHeader));
		curPtr += sizeof(texHeader);
		texHead.push_back(th);
		
		//Skip over first LZC header; patch chunks together ourselves (probably set up similarly to WFLZ, but I don't care much)
		LZC_Header lzch;
		memcpy(&lzch, &dataIn[curPtr], sizeof(LZC_Header));
		curPtr += lzch.compressedSize;	//Skip this header
		
		uint32_t curDecompSize = 0;
		vector<uint8_t*> pixelData;
		vector<LZC_SIZE_T> dataSz;
		//Decompress loop
		LZC_SIZE_T size_needed = lzch.decompressedSize;
		while(curDecompSize < size_needed)
		{
			while(true)
			{
				//Read forward until we find another LZC header...
				if(memcmp(&(dataIn[curPtr]), "LZC", 3) == 0)
					break;
				curPtr++; //Search for next header
			}
			LZC_SIZE_T decomp_size = LZC_GetDecompressedSize(&(dataIn[curPtr]));
			if(decomp_size)
			{
				uint8_t* dataOut = (uint8_t*)malloc(decomp_size);
				uint32_t readAmt = LZC_Decompress(&(dataIn[curPtr]), dataOut);
				curPtr += readAmt;	//Skip forward to about where the next header should be 
				curDecompSize += decomp_size;
				dataSz.push_back(decomp_size);
				pixelData.push_back(dataOut);
			}
			else
			{
				cout << "ERR Size = 0" << endl;
				curPtr++;	//Skip past this header so we don't hit it over and over...
			}
		}
		
		//Figure out the total size we need to allocate
		LZC_SIZE_T totalSz = 0;
		for(vector<LZC_SIZE_T>::iterator j = dataSz.begin(); j != dataSz.end(); j++)
			totalSz += *j;
			
		chunk c;
		c.size = totalSz;
		c.data = (uint8_t*)malloc(totalSz);
		
		//Copy all the LZC chunks into this image
		uint32_t curImgDataPos = 0;
		for(int j = 0; j < dataSz.size(); j++)
		{
			memcpy(c.data + curImgDataPos, pixelData[j], dataSz[j]);
			curImgDataPos += dataSz[j];
			free(pixelData[j]);	//Free data while we're at it...
		}
		
		imageData.push_back(c);
	}
	
	//If we shouldn't sheet this, stop here and output
	if(g_bNoSheet)
	{
		for(int i = 0; i < vAnims.size(); i++)
		{
			int iCurImg = 0;
			for(vector<uint32_t>::iterator j = vAnims[i].begin(); j != vAnims[i].end(); j++)
			{
				ostringstream oss;
				oss << "./output/" << sName << i << '_' << ++iCurImg << ".png";
				cout << "Saving image " << oss.str().c_str() << endl;
				FIBITMAP* bmp = createImage(imageData[*j].data, imageData[*j].size, pieces[*j], animExtents[i].maxul, animExtents[i].maxbr, texHead[*j]);
				FreeImage_Save(FIF_PNG, bmp, oss.str().c_str());
			}
		}
	}
	else	//Generate spritesheet out of this
	{
		//Figure out dimensions of final image
		int finalX = offsetX;
		int finalY = offsetY/2;
		int totalWidthAvg = 0;
		for(int i = 0; i < animExtents.size(); i++)
		{
			int animExtentX = (int)(animExtents[i].maxbr.x - animExtents[i].maxul.x + offsetX + 0.5) * vAnims[i].size() + offsetX;
			if(animExtentX > finalX)
				finalX = animExtentX;
			totalWidthAvg += animExtentX;
			finalY += (int)(animExtents[i].maxul.y - animExtents[i].maxbr.y + offsetY + 0.5);
		}
		
		//Don't care if there's only a few
		if(animExtents.size() > 5)
			totalWidthAvg /= animExtents.size();
		totalWidthAvg *= 1.5f;
		
		//If an animation is longer than double the size of the average, cut it up vertically
		finalX = offsetX;
		finalY = offsetY/2;
		for(int i = 0; i < animExtents.size(); i++)
		{
			int animMaxX = (int)(animExtents[i].maxbr.x - animExtents[i].maxul.x + 0.5);
			int animMaxY = (int)(animExtents[i].maxul.y - animExtents[i].maxbr.y + 0.5);
			
			animMaxX += offsetX;
			int curAnimMaxX = 0;
			for(int j = 0; j < vAnims[i].size(); j++)
			{
				if(curAnimMaxX + animMaxX > totalWidthAvg)
				{
					if(finalX < curAnimMaxX)				//Save final width
						finalX = curAnimMaxX + offsetX;
					
					finalY += offsetY/2 + animMaxY;			//Offset vertically, spacing of 1 pixel instead of 2
					curAnimMaxX = 0;						//Start next row
				}
				curAnimMaxX += animMaxX;
			}
			
			if(finalX < curAnimMaxX)
				finalX = curAnimMaxX + offsetX;
			
			finalY += offsetY + animMaxY;
		}
		
		//Allocate final image, and piece
		FIBITMAP* finalSheet = FreeImage_Allocate(finalX, finalY, 32);
		RGBQUAD q = {128,128,0,255};
		FreeImage_FillBackground(finalSheet, (const void *)&q);
		
		int curX = offsetX;
		int curY = offsetY/2;
		for(int i = 0; i < vAnims.size(); i++)
		{
			int yAdd = 0;
			for(vector<uint32_t>::iterator j = vAnims[i].begin(); j != vAnims[i].end(); j++)
			{
				FIBITMAP* bmp = createImage(imageData[*j].data, imageData[*j].size, pieces[*j], animExtents[i].maxul, animExtents[i].maxbr, texHead[*j]);
				
				yAdd = offsetY + FreeImage_GetHeight(bmp);
			
				//See if we should start next row in sprite sheet (if this anim is too long)
				if(curX + FreeImage_GetWidth(bmp) + offsetX > totalWidthAvg)
				{
					curX = offsetX;
					curY += FreeImage_GetHeight(bmp) + offsetY/2;
				}
				
				FreeImage_Paste(finalSheet, bmp, curX, curY, 300);
				curX += offsetX + FreeImage_GetWidth(bmp);
				FreeImage_Unload(bmp);
			}
			curY += yAdd;
			curX = offsetX;
		}
		
		ostringstream oss;
		oss << "./output/" << sName << ".png";
		cout << "Saving image " << oss.str().c_str() << endl;
		FreeImage_Save(FIF_PNG, finalSheet, oss.str().c_str());
		
		FreeImage_Unload(finalSheet);
	}
	
	//Clean up memory
	for(int i = 0; i < ah.numFrames; i++)
		free(imageData[i].data);
	
	free(dataIn);
}

int usage()
{
	cout << "Usage: lzc_decrypt [-nopiece|-nosheet] file.anb" << endl;
	return 1;
}

int main(int argc, char** argv)
{
	g_bNoSheet = false;
	g_bPieceTogether = true;
	
	list<string> sFilenames;
	//Parse commandline
	for(int i = 1; i < argc; i++)
	{
		string s = argv[i];
		if(s == "-nosheet")
			g_bNoSheet = true;
		else if(s == "-nopiece")
			g_bPieceTogether = false;
		else if(s.find(".anb") != string::npos)
			sFilenames.push_back(s);
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
























