#include "ADPCMEncode.h"

// Based on ADPCM code at --
// http://www.cs.columbia.edu/~gskc/Code/AdvancedInternetServices/SoundNoiseRatio/dvi_adpcm.c

/* Intel ADPCM step variation table */
const static int indexTable[8] = {
	-1, -1, -1, -1, 2, 4, 6, 8
};

const static int stepsizeTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
	19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

uint8_t ADPCMEncodeStereo::EncodeSample(int32_t& index, int32_t& valpred, int16_t val)
{
	// encoder step size for current index
	int32_t step = stepsizeTable[index];

	// difference between last sample generated (our predictor state) and this one
	int32_t diff = val - valpred;

	// separate sign from magnitude
	int32_t sign = (diff < 0) ? 8 : 0;
	if (sign) diff = -diff;

	// calculate delta
	// This code *approximately* computes:
	// delta = diff * 4 / step;
	// vpdiff = (delta + 0.5) * step / 4;
	// but in shift step bits are dropped. The net result of this is
	// that even if you have fast mul / div hardware you cannot put it to
	// good use since the fixup would be too expensive.
	
	int32_t delta = 0;
	int32_t vpdiff = (step >> 3);

	if (diff >= step)
	{
		delta |= 4;
		diff -= step;
		vpdiff += step;
	}
	step >>= 1;
	if (diff >= step)
	{
		delta |= 2;
		diff -= step;
		vpdiff += step;
	}
	step >>= 1;
	if (diff >= step)
	{
		delta |= 1;
		vpdiff += step;
	}

	// update our generated value based on nearest delta
	if (sign) valpred -= vpdiff;
	else valpred += vpdiff;

	// clamp it to valid range
	if (valpred > 32767) valpred = 32767;
	else if (valpred < -32768) valpred = -32768;

	// advance and clamp index
	index += indexTable[delta];
	if (index < 0) index = 0;
	else if (index > 88) index = 88;

	// add sign to calculated delta encoding
	delta |= sign;

	return delta;
}

void ADPCMEncodeStereo::GetState(int16_t* pSampleLeft, uint8_t* pIndexLeft, int16_t* pSampleRight, uint8_t* pIndexRight)
{
	*pIndexLeft = mnIndex[0];
	*pIndexRight = mnIndex[1];
	*pSampleLeft = mnSample[0];
	*pSampleRight = mnSample[1];
}

void ADPCMEncodeStereo::SetState(int16_t nSampleLeft, uint8_t nIndexLeft, int16_t nSampleRight, uint8_t nIndexRight)
{
	mnIndex[0] = nIndexLeft;
	mnIndex[1] = nIndexRight;
	mnSample[0] = nSampleLeft;
	mnSample[1] = nSampleRight;
}

void ADPCMEncodeStereo::EncodeBlock(uint8_t *pADPCMOut, const int16_t *pPCMIn, uint32_t nSamples)
{
	while (nSamples--)
	{
		uint8_t nEncoded;
		nEncoded = EncodeSample(mnIndex[0], mnSample[0], *pPCMIn++) << 4;
		nEncoded |= EncodeSample(mnIndex[1], mnSample[1], *pPCMIn++);

		*pADPCMOut++ = nEncoded;
	}
}

void ADPCMEncodeStereo::DecodeBlock(int16_t* pPCMOut, uint8_t* pADPCMIn, uint32_t nSamples)
{
	while (nSamples--)
	{
		for (uint32_t c = 0; c < 2; c++)
		{
			int32_t delta = (*pADPCMIn >> (c ? 0 : 4)) & 0xf;

			int32_t step = stepsizeTable[mnIndex[c]];

			// calculate value change
			uint32_t vpdiff = step >> 3;
			if (delta & 4) vpdiff += step;
			if (delta & 2) vpdiff += step >> 1;
			if (delta & 1) vpdiff += step >> 2;

			// update sample value
			if (delta & 8) mnSample[c] -= vpdiff;
			else mnSample[c] += vpdiff;

			// clamp sample
			if (mnSample[c] > 32767) mnSample[c] = 32767;
			else if (mnSample[c] < -32768) mnSample[c] = -32768;

			// advance index
			mnIndex[c] += indexTable[delta & 7];
			if (mnIndex[c] < 0) mnIndex[c] = 0;
			if (mnIndex[c] > 88) mnIndex[c] = 88;

			// write out PCM sample
			*pPCMOut++ = mnSample[c];
		}
		pADPCMIn++;
	}
}

