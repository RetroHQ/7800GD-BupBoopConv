#include <stdio.h>
#include <stdint.h>

// Based on ADPCM code at --
// http://www.cs.columbia.edu/~gskc/Code/AdvancedInternetServices/SoundNoiseRatio/dvi_adpcm.c

class ADPCMEncodeStereo
{
private:
	int32_t			mnSample[2];
	int32_t			mnIndex[2];

	uint8_t			EncodeSample(int32_t& index, int32_t& valpred, int16_t val);

public:
					ADPCMEncodeStereo() { ResetState(); }
	void			ResetState() { SetState(0, 0, 0, 0); }
	void			SetState(int16_t nSampleLeft, uint8_t nIndexLeft, int16_t nSampleRight, uint8_t nIndexRight);
	void			GetState(int16_t* pSampleLeft, uint8_t *pIndexLeft, int16_t *pSampleRight, uint8_t *pIndexRight);
	void			EncodeBlock(uint8_t* pADPCMOut, const int16_t* pPCMIn, uint32_t nSamples);
	void			DecodeBlock(int16_t* pPCMOut, uint8_t* pADPCMIn, uint32_t nSamples);
};
