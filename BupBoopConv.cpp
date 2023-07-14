// BupBoopConv.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <direct.h>

#include <iostream>
#include <vector>
#include <string>

#include "types.h"
#include "ADPCMEncode.h"

extern "C" {
#include "CoreTone/coretone.h"
#include "CoreTone/sample.h"
#include "CoreTone/channel.h"
#include "CoreTone/music.h"
}

#include <windows.h>

uint8_t* loadFile(const char* pFilename, const char* pType, bool bTestOnly = false)
{
	FILE* f;
	uint8_t* pData = 0;
	
	if (fopen_s(&f, pFilename, "rb") == 0)
	{
		fseek(f, 0, SEEK_END);
		uint32_t nSize = ftell(f);
		fseek(f, 0, SEEK_SET);

		// if a type has been given, check it
		if (pType)
		{
			uint32_t t;
			fread(&t, 1, 4, f);
			bool bFileValid = strncmp((const char*)&t, pType, 4) == 0;

			if (bTestOnly)
			{
				fclose(f);
				return (uint8_t*)1;
			}

			if (bFileValid)
			{
				pData = (uint8_t*)malloc(nSize);
			}
			fseek(f, 0, SEEK_SET);
		}

		// otherwise just load blind
		else
		{
			pData = (uint8_t*)malloc(nSize);
		}

		if (pData)
		{
			fread(pData, 1, nSize, f);
		}

		fclose(f);
	}

	return pData;
}

struct SFileParam
{
	const char* pName;
	void **ppPtr;
};

struct SDecoderState
{
	int16_t		smp[2];
	uint8_t		idx[2];
};

bool EncodeFiles(std::vector<std::string> &cFileList, const char* pSamplePakFilename, const char* pInstrPakFilename, bool bNumeralOutput = false, const char *pOutputDir = 0)
{
	// check the files passed are valid
	bool bFatalError = false;
	if (!loadFile(pSamplePakFilename, "CSMP", true))
	{
		printf("Sample pack file '%s' is invalid.\n", pSamplePakFilename);
		bFatalError = true;
	}
	if (!loadFile(pInstrPakFilename, "CINS", true))
	{
		printf("Sample pack file '%s' is invalid.\n", pInstrPakFilename);
		bFatalError = true;
	}

	if (bFatalError)
	{
		return false;
	}

	// load the sample and instument packs for use, we know they are valid
	uint8_t *pSamplePak = loadFile(pSamplePakFilename, "CSMP");
	uint8_t *pInstrPak = loadFile(pInstrPakFilename, "CINS");

	// buffer for 200x 16bit stereo samples 
	// 240hz tick (CORETONE_DECODE_RATE)
	// 48khz sample rate (CORETONE_RENDER_RATE)
	int16_t* pAudioBuffer = new int16_t[CORETONE_BUFFER_SAMPLES*2];
	uint8_t* pADPCMBuffer = new uint8_t[CORETONE_BUFFER_SAMPLES];

	// load up all files in turn and normalise accross the whole set
	printf("Calculating normalisation...\n");

	int16_t nMaxVolume = 0;

	for (std::vector<std::string>::iterator t = cFileList.begin(); t != cFileList.end(); ++t)
	{
		uint8_t* pMusic = loadFile(t->c_str(), "CMUS");
		if (pMusic)
		{
			printf("  %s...\n", t->c_str());

			// Initialise CoreTone with the data we've been given
			ct_init(pSamplePak, pInstrPak);
			ct_playMusic(pMusic);

			// work out peak volume for this track
			int32_t nLoopFlag = 0;
			do
			{
				int32_t nLoop = ct_update(pAudioBuffer);

				if (nLoop && !nLoopFlag)
				{
					nLoopFlag = 1;
				}
				else if (!nLoop && nLoopFlag)
				{
					nLoopFlag = 2;
					break;
				}

				// keep track of max volume to re-normalise
				for (uint32_t n = 0; n < CORETONE_BUFFER_SAMPLES * 2; n++)
				{
					int16_t v = pAudioBuffer[n];
					if (v < 0) v = -(v + 1);
					if (v > nMaxVolume) nMaxVolume = v;
				}

			} while (ct_checkMusic());

			free(pMusic);
		}
		else
		{
			printf("  %s... INVALID FILE\n", t->c_str());
		}
	}

	// now render all files and normalise volume

	printf("Rendering audio with x%.02f normalisation...\n", (float)32767 / nMaxVolume);
	int nFileIndex = 0;
	for (std::vector<std::string>::iterator t = cFileList.begin(); t != cFileList.end(); ++t)
	{
		uint8_t* pMusic = loadFile(t->c_str(), "CMUS");
		if (pMusic)
		{
			// Initialise CoreTone with the data we've been given
			ct_init(pSamplePak, pInstrPak);
			ct_playMusic(pMusic);

			// generate suitable output filename
			char szOutFile[MAX_PATH];
			if (bNumeralOutput)
			{
				// numeral output expects the output directory as well
				sprintf_s(szOutFile, "%s\\%d.bup", pOutputDir, nFileIndex++);
			}
			else
			{
				strcpy_s(szOutFile, t->c_str());
				char* pEnd = strrchr(szOutFile, '.');
				if (!pEnd)
				{
					pEnd = szOutFile + strlen(szOutFile);
				}
				strcpy_s(pEnd, (szOutFile + MAX_PATH) - pEnd, ".bup");
			}

			// open output file for writing
			FILE* pOutFile;
			if (fopen_s(&pOutFile, szOutFile, "wb") == 0)
			{
				printf("  %s...", t->c_str());

				// header
				fwrite("STR\0", 1, 4, pOutFile);

				// space for loop marker
				fseek(pOutFile, 14, SEEK_CUR);

				uint32_t trackLength = 0;
				uint32_t loopStart = 0;
				uint32_t loopEnd = 0;
				int32_t nLoopFlag = 0;

				std::vector<SDecoderState> cScrubTable;

				// encode to ADPCM stereo stream
				ADPCMEncodeStereo mADPCM;
				do
				{
					// rend
					int32_t nLoop = ct_update(pAudioBuffer);

					if (nLoop && !nLoopFlag)
					{
						nLoopFlag = 1;
						// add one second for the loop start incase of instrument decay
						// at the end of the loop
						loopStart = trackLength + 240;
					}
					else if (!nLoop && nLoopFlag)
					{
						loopEnd = trackLength + 240;
						nLoopFlag = 2;
						break;
					}

					// normalise the audio volume
					for (uint32_t n = 0; n < CORETONE_BUFFER_SAMPLES * 2; n++)
					{
						int32_t v = pAudioBuffer[n];
						v = (v * 32767) / nMaxVolume;
						pAudioBuffer[n] = v;
					}

					// store loop point information if needed
					if ((nLoopFlag == 1) && (trackLength == loopStart))
					{
						// get state of decoder at the loop point
						int16_t smp[2];
						uint8_t idx[2];
						mADPCM.GetState(&smp[0], &idx[0], &smp[1], &idx[1]);
						uint32_t pos = ftell(pOutFile);

						// seek back and write loop information
						fseek(pOutFile, 8, SEEK_SET);
						fwrite(&pos, 4, 1, pOutFile);		// file position for loop
						fwrite(&smp[0], 2, 1, pOutFile);	// adpcm decoder state for loop
						fwrite(&idx[0], 1, 1, pOutFile);
						fwrite(&smp[1], 2, 1, pOutFile);
						fwrite(&idx[1], 1, 1, pOutFile);

						// back to writing audio stream
						fseek(pOutFile, 0, SEEK_END);
					}

					// add decoder state for srubbing at 10 per second
					if ((trackLength % 24) == 0)
					{
						SDecoderState s;
						mADPCM.GetState(&s.smp[0], &s.idx[0], &s.smp[1], &s.idx[1]);
						cScrubTable.push_back(s);
					}

					// compress to 4bit stereo ADPCM
					mADPCM.EncodeBlock(pADPCMBuffer, pAudioBuffer, CORETONE_BUFFER_SAMPLES);

					// write out ADPCM stream
					fwrite(pADPCMBuffer, 1, CORETONE_BUFFER_SAMPLES, pOutFile);

					trackLength++;
				} while ((nLoopFlag != 2 && ct_checkMusic()) || (nLoopFlag == 2 && trackLength < loopEnd));
					
				// seek back and write scrub location
				uint32_t pos = ftell(pOutFile);
				fseek(pOutFile, 4, SEEK_SET);
				fwrite(&pos, 4, 1, pOutFile);		// file position for scrub table
				fseek(pOutFile, 0, SEEK_END);
					
				// write out the scrub table
				for (auto it = begin(cScrubTable); it != end(cScrubTable); ++it) 
				{
					fwrite(&it->smp[0], 2, 1, pOutFile);	// adpcm decoder state for loop
					fwrite(&it->idx[0], 1, 1, pOutFile);
					fwrite(&it->smp[1], 2, 1, pOutFile);
					fwrite(&it->idx[1], 1, 1, pOutFile);
				}
					
				fclose(pOutFile);

				printf(" %.02fs, %slooped.\n", ((float)trackLength) / CORETONE_DECODE_RATE, nLoopFlag == 2 ? "" : "un");
			}
		}
	}

	free(pSamplePak);
	free(pInstrPak);

	delete[] pAudioBuffer;
	delete[] pADPCMBuffer;

	return true;
}

bool ReadLine(char *pBuffer, int nMaxLen, FILE *f)
{
	if (fgets(pBuffer, nMaxLen, f))
	{
		// strip extra cr / lf
		pBuffer[strcspn(pBuffer, "\r\n")] = 0;
		return true;
	}
	return false;
}

// A78 V4 header suitable for SOUPER mapper games
const uint8_t A78HEADER[] =
{
	0x04,0x41,0x54,0x41,0x52,0x49,0x37,0x38,0x30,0x30,0x20,0x20,0x20,0x20,0x20,0x20,
	0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x08,0x00,0x00,0x10,0x00,0x01,0x01,0x04,0x00,0x00,0x00,0x00,0x00,0x00,
	0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x41,0x43,0x54,0x55,0x41,0x4C,0x20,0x43,0x41,0x52,0x54,0x20,
	0x44,0x41,0x54,0x41,0x20,0x53,0x54,0x41,0x52,0x54,0x53,0x20,0x48,0x45,0x52,0x45
};

bool AddHeader(const char *pOutfile, const char *pInfile, const char *pTitle)
{
	FILE *out, *in;

	// open the input and output files
	if (fopen_s(&in, pInfile, "rb") != 0)
	{
		printf("Failed to open '%s' for reading.\n", pInfile);
		return false;
	}

	if (fopen_s(&out, pOutfile, "wb") != 0)
	{
		fclose(in);
		printf("Failed to open '%s' for writing.\n", pOutfile);
		return false;
	}

	// add the game name into the header
	uint8_t header[sizeof(A78HEADER)];
	memcpy(header, A78HEADER, sizeof(A78HEADER));
	memcpy(header + 17, pTitle, strlen(pTitle));

	// write out the A78 header
	fwrite(header, 1, sizeof(A78HEADER), out);

	// now copy the actual ROM data
	int read;
	do
	{
		char buffer[16384];
		read = fread(buffer, 1, 16384, in);
		fwrite(buffer, 1, read, out);
	} while (read);

	fclose(in);
	fclose(out);

	return true;
}

bool ParseCDF(const char *pFilename)
{
	FILE *f;
	if (fopen_s(&f, pFilename, "rt") == 0)
	{
		char szLine[256];

		// check it's a valid prosystem cdf
		if (!ReadLine(szLine, 256, f) || _stricmp(szLine, "ProSystem") != 0)
		{
			return false;
		}
		
		// needs to be a souper mapper
		if (!ReadLine(szLine, 256, f) || _stricmp(szLine, "SOUPER") != 0)
		{
			return false;
		}

		// game title
		char szGameTitle[256];
		ReadLine(szGameTitle, 256, f);

		// binary filename
		char szBinaryFilename[256];
		ReadLine(szBinaryFilename, 256, f);

		// look for CORETONE
		while (ReadLine(szLine, 256, f) && _stricmp(szLine, "CORETONE") != 0);

		if (_stricmp(szLine, "CORETONE") != 0)
		{
			return false;
		}

		// first entry is sample pak
		char szSamplePakFilename[256];
		ReadLine(szSamplePakFilename, 256, f);

		// second entry is instrument pak
		char szInstrumentPakFilename[256];
		ReadLine(szInstrumentPakFilename, 256, f);

		// the rest of the lines are music file entries
		std::vector<std::string> cFileList;
		while (ReadLine(szLine, 256, f))
		{
			// ignore blank lines
			if (szLine[0])
			{
				cFileList.push_back(szLine);
			}
		}

		// strip any spaces from the game name for the folder name
		char szStippedName[256] = "7800GD\\";
		char *pIn = szGameTitle, *pOut = szStippedName + 7;
		while (*pIn)
		{
			if (*pIn != ' ')
			{
				*pOut++ = *pIn++;
			}
			else
			{
				pIn++;
			}
		}
		*pOut = 0;

		// we'll make a folder for the game and audio
		_mkdir("7800GD");
		_mkdir(szStippedName);

		// process our audio file list
		printf("Converting audio from '%s'...\n", szGameTitle);
		EncodeFiles(cFileList, szSamplePakFilename, szInstrumentPakFilename, true, szStippedName);

		// add an A78 header to the binary file
		printf("Generating A78 file from '%s'...\n", szBinaryFilename);
		strcat_s(szStippedName, ".A78");
		AddHeader(szStippedName, szBinaryFilename, szGameTitle);

		printf("\nDone!\n\n");
	}
	return false;
}

int main(int argc, const char **argv)
{
	char* pSamplePakFilename = 0;
	char* pInstrPakFilename = 0;
	char* pMusicFilename = 0;
	char *pCDFFilename = 0;

	const SFileParam aParam[] =
	{
		{ "-s", (void**) &pSamplePakFilename },
		{ "-i", (void**) &pInstrPakFilename },
		{ "-m", (void**) &pMusicFilename },
		{ "-c", (void**) &pCDFFilename }
	};
	const uint32_t PARAMS = sizeof(aParam) / sizeof(aParam[0]);

	for (int n = 1; n < argc-1; n++)
	{
		const char* arg = argv[n];
		const char* arg2 = argv[n + 1];
		bool bParamOk = false;

		for (int p = 0; p < PARAMS; p++)
		{
			const SFileParam* pParam = &aParam[p];
			if (_stricmp(pParam->pName, arg) == 0)
			{
				bParamOk = true;
				*pParam->ppPtr = _strdup(arg2);
				n++;
			}
		}
		if (!bParamOk)
		{
			printf("Unknown parameter: '%s'\n", arg);
		}
	}

	if (pCDFFilename)
	{
		ParseCDF(pCDFFilename);
	}
	else if (pSamplePakFilename && pInstrPakFilename && pMusicFilename)
	{
		std::vector<std::string> cFileList;

		// search for all music files matching the pattern
		WIN32_FIND_DATA fd;
		HANDLE hFind = ::FindFirstFile(pMusicFilename, &fd);
		if (hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					cFileList.push_back(fd.cFileName);
				}
			} while (::FindNextFile(hFind, &fd));
			::FindClose(hFind);
		}

		EncodeFiles(cFileList, pSamplePakFilename, pInstrPakFilename);
	}
	else
	{
		printf(	"This tool renders a BupBoop music file set into a format usable by the 7800GD.\n\n"
				"Usage: %s -s filename.smp -i filename.ins -m filename.mus\n"
				"   or: %s -c filename.cdf\n\n", argv[0], argv[0]);
	}

	free(pMusicFilename);
	free(pCDFFilename);
}
