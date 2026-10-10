/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Based on ffmpeg's QDMC decoder
// Copyright (c) 2017 Paul B Mahol

#include "common/scummsys.h"

#ifdef USE_QDMC

#include "audio/decoders/qdmc.h"
#include "audio/audiostream.h"
#include "audio/decoders/codec.h"
#include "audio/decoders/raw.h"

#include "common/array.h"
#include "common/bitstream.h"
#include "common/endian.h"
#include "common/intrinsics.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "math/fft.h"
#include "math/utils.h"

namespace Audio {

static const uint32 kCodePrefix[] = {
	0x0, 0x1, 0x2, 0x3, 0x4, 0x6, 0x8, 0xA,
	0xC, 0x10, 0x14, 0x18, 0x1C, 0x24, 0x2C, 0x34,
	0x3C, 0x4C, 0x5C, 0x6C, 0x7C, 0x9C, 0xBC, 0xDC,
	0xFC, 0x13C, 0x17C, 0x1BC, 0x1FC, 0x27C, 0x2FC, 0x37C,
	0x3FC, 0x4FC, 0x5FC, 0x6FC, 0x7FC, 0x9FC, 0xBFC, 0xDFC,
	0xFFC, 0x13FC, 0x17FC, 0x1BFC, 0x1FFC, 0x27FC, 0x2FFC, 0x37FC,
	0x3FFC, 0x4FFC, 0x5FFC, 0x6FFC, 0x7FFC, 0x9FFC, 0xBFFC, 0xDFFC,
	0xFFFC, 0x13FFC, 0x17FFC, 0x1BFFC, 0x1FFFC, 0x27FFC, 0x2FFFC, 0x37FFC,
	0x3FFFC
};

static const float kAmplitudeTable[64] = {
	1.18750000f, 1.68359380f, 2.37500000f, 3.36718750f, 4.75000000f,
	6.73437500f, 9.50000000f, 13.4687500f, 19.0000000f, 26.9375000f,
	38.0000000f, 53.8750000f, 76.0000000f, 107.750000f, 152.000000f,
	215.500000f, 304.000000f, 431.000000f, 608.000000f, 862.000000f,
	1216.00000f, 1724.00000f, 2432.00000f, 3448.00000f, 4864.00000f,
	6896.00000f, 9728.00000f, 13792.0000f, 19456.0000f, 27584.0000f,
	38912.0000f, 55168.0000f, 77824.0000f, 110336.000f, 155648.000f,
	220672.000f, 311296.000f, 441344.000f, 622592.000f, 882688.000f,
	1245184.00f, 1765376.00f, 2490368.00f, 3530752.00f, 4980736.00f,
	7061504.00f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16 kNodes[112] = {
	0, 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 56, 64,
	80, 96, 120, 144, 176, 208, 240, 256,
	0, 2, 4, 8, 16, 24, 32, 48, 56, 64, 80, 104,
	128, 160, 208, 256, 0, 0, 0, 0, 0,
	0, 2, 4, 8, 16, 32, 48, 64, 80, 112, 160, 208,
	256, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 4, 8, 16, 32, 48, 64, 96, 144, 208, 256,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 4, 16, 32, 64, 256, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8 kNoiseBandsSize[] = {
	19, 14, 11, 9, 4, 2, 0
};

static const uint8 kNoiseBandsSelector[] = {
	4, 3, 2, 1, 0, 0, 0
};

// Huffman tables as { symbol + 1, code length }, in code order. Symbol 0 is the escape code.
static const uint8 kHuffmanTables[][2] = {
	// Noise value - 27 entries
	{  1,  2 }, { 10,  7 }, { 26,  9 }, { 22,  9 }, { 24,  9 }, { 14,  9 },
	{  8,  6 }, {  6,  5 }, {  7,  5 }, {  9,  7 }, { 30,  9 }, { 32, 10 },
	{ 13, 10 }, { 20,  9 }, { 28,  9 }, { 12,  7 }, { 15, 11 }, { 36, 12 },
	{  0, 12 }, { 34, 10 }, { 18,  9 }, { 11,  9 }, { 16,  9 }, {  5,  3 },
	{  2,  3 }, {  4,  3 }, {  3,  2 },
	// Noise segment length - 12 entries
	{  1,  1 }, {  2,  2 }, {  3,  4 }, {  8,  9 }, {  9, 10 }, {  0, 10 },
	{ 13,  8 }, {  7,  7 }, {  6,  6 }, { 17,  5 }, {  4,  4 }, {  5,  4 },
	// Amplitude - 28 entries
	{ 18,  3 }, { 16,  3 }, { 22,  7 }, {  8, 10 }, {  4, 10 }, {  3,  9 },
	{  2,  8 }, { 23,  8 }, { 10,  8 }, { 11,  7 }, { 21,  5 }, { 20,  4 },
	{  1,  7 }, {  7, 10 }, {  5, 10 }, {  9,  9 }, {  6, 10 }, { 25, 11 },
	{ 26, 12 }, { 27, 13 }, {  0, 13 }, { 24,  9 }, { 12,  6 }, { 13,  5 },
	{ 14,  4 }, { 19,  3 }, { 15,  3 }, { 17,  2 },
	// Frequency differences - 47 entries
	{  2,  4 }, { 14,  6 }, { 26,  7 }, { 31,  8 }, { 32,  9 }, { 35,  9 },
	{  7,  5 }, { 10,  5 }, { 22,  7 }, { 27,  7 }, { 19,  7 }, { 20,  7 },
	{  4,  5 }, { 13,  5 }, { 17,  6 }, { 15,  6 }, {  8,  5 }, {  5,  4 },
	{ 28,  7 }, { 33,  9 }, { 36, 11 }, { 38, 12 }, { 42, 14 }, { 45, 16 },
	{ 44, 18 }, {  0, 18 }, { 46, 17 }, { 43, 15 }, { 40, 13 }, { 37, 11 },
	{ 39, 12 }, { 41, 12 }, { 34,  8 }, { 16,  6 }, { 11,  5 }, {  9,  4 },
	{  1,  2 }, {  3,  4 }, { 30,  7 }, { 29,  7 }, { 23,  6 }, { 24,  6 },
	{ 18,  6 }, {  6,  4 }, { 12,  5 }, { 21,  6 }, { 25,  6 },
	// Amplitude differences - 9 entries
	{  1,  2 }, {  3,  3 }, {  4,  4 }, {  5,  5 }, {  6,  6 }, {  7,  7 },
	{  8,  8 }, {  0,  8 }, {  2,  1 },
	// Phase differences - 9 entries
	{  2,  2 }, {  1,  2 }, {  3,  4 }, {  7,  4 }, {  6,  5 }, {  5,  6 },
	{  0,  6 }, {  4,  4 }, {  8,  2 }
};

static const uint8 kHuffmanSizes[] = {
	27, 12, 28, 47, 9, 9
};

static const uint8 kHuffmanLookupBits[] = {
	12, 10, 12, 12, 8, 6
};

typedef Common::BitStreamMemory8LSB QDMCBitStream;

/**
 * A Huffman table for a least significant bit first stream, with the codes
 * assigned in table order. Codes up to the lookup size are found in one step,
 * longer ones by searching.
 */
class QDMCHuffman {
public:
	void init(const uint8 (*table)[2], uint size, uint lookupBits);
	int getSymbol(QDMCBitStream &bits) const;

private:
	struct Entry {
		uint32 code; // Bit-reversed: the first bit read is bit 0
		uint8 length;
		int16 symbol;
	};

	uint _lookupBits;
	Common::Array<Entry> _lookup;
	Common::Array<Entry> _long;
};

void QDMCHuffman::init(const uint8 (*table)[2], uint size, uint lookupBits) {
	Entry empty;
	empty.code = 0;
	empty.length = 0;
	empty.symbol = -1;

	_lookupBits = lookupBits;
	_lookup.clear();
	_lookup.resize(1 << lookupBits, empty);
	_long.clear();

	uint32 code = 0; // Left-aligned
	for (uint i = 0; i < size; i++) {
		Entry entry;
		entry.length = table[i][1];
		entry.symbol = table[i][0] - 1;
		entry.code = 0;
		for (uint b = 0; b < entry.length; b++)
			entry.code |= ((code >> (31 - b)) & 1) << b;
		code += 1u << (32 - entry.length);

		if (entry.length <= lookupBits) {
			for (uint32 j = entry.code; j < _lookup.size(); j += 1 << entry.length)
				_lookup[j] = entry;
		} else {
			_long.push_back(entry);
		}
	}
}

int QDMCHuffman::getSymbol(QDMCBitStream &bits) const {
	const Entry &entry = _lookup[bits.peekBits(_lookupBits)];
	if (entry.length) {
		bits.skip(entry.length);
		return entry.symbol;
	}

	for (uint i = 0; i < _long.size(); i++) {
		if (bits.peekBits(_long[i].length) == _long[i].code) {
			bits.skip(_long[i].length);
			return _long[i].symbol;
		}
	}

	return -1;
}

class QDMCStream : public Codec {
public:
	QDMCStream(uint channels, uint sampleRate, uint bitRate, uint fftSize, uint32 packetSize);
	~QDMCStream() override;

	AudioStream *decodeFrame(Common::SeekableReadStream &stream) override;

private:
	struct Tone {
		uint8 mode;
		uint8 phase;
		uint8 offset;
		int16 freq;
		int16 amplitude;
	};

	int getVLC(QDMCBitStream &bits, int table, bool flag) const;
	bool checkLabel(const byte *data) const;
	bool readNoiseData(QDMCBitStream &bits);
	void addTone(int group, int offset, int freq, int stereoMode, int amplitude, int phase);
	bool readWaveData(QDMCBitStream &bits);
	void linCalc(float amplitude, int node1, int node2, int index);
	void addNoise(int ch, int currentSubframe);
	void addWave(int offset, int freqs, int group, int stereoMode, int amp, int phase);
	void addWave0(int offset, int freqs, int stereoMode, int amp, int phase);
	void addWaves(int currentSubframe);
	void makeNoises();
	bool decodePacket(const byte *data, int16 *out);
	void flush();

	uint _channels;
	uint _sampleRate;
	uint32 _packetSize;

	uint8 _frameBits;
	int _bandIndex;
	int _frameSize;
	int _subframeSize;
	int _fftOffset;
	int _bufferOffset;

	uint8 _noise[2][19][17];
	Tone _tones[5][8192];
	int _nbTones[5];
	int _curTone[5];
	float _altSin[5][31];
	float _fftBuffer[4][8192 * 2];
	float _noise2Buffer[4096 * 2];
	float _noiseBuffer[4096 * 2];
	float _buffer[2 * 32768];
	float *_bufferPtr;
	uint32 _rndval;

	float _sinTable[512];
	QDMCHuffman _huffman[6];

	Math::FFT *_fft;
	Math::Complex *_fftData[2];
};

QDMCStream::QDMCStream(uint channels, uint sampleRate, uint bitRate, uint fftSize, uint32 packetSize) {
	_channels = channels;
	_sampleRate = sampleRate;
	_packetSize = packetSize;

	memset(_noise, 0, sizeof(_noise));
	memset(_nbTones, 0, sizeof(_nbTones));
	memset(_curTone, 0, sizeof(_curTone));
	memset(_noise2Buffer, 0, sizeof(_noise2Buffer));
	memset(_noiseBuffer, 0, sizeof(_noiseBuffer));
	_bufferPtr = _buffer;
	_rndval = 0;
	flush();

	const uint8 (*table)[2] = kHuffmanTables;
	for (int i = 0; i < ARRAYSIZE(_huffman); i++) {
		_huffman[i].init(table, kHuffmanSizes[i], kHuffmanLookupBits[i]);
		table += kHuffmanSizes[i];
	}

	for (int i = 0; i < 512; i++)
		_sinTable[i] = sin(2.0f * i * M_PI * 0.001953125f);

	int x;
	if (_sampleRate >= 32000) {
		x = 28000;
		_frameBits = 13;
	} else if (_sampleRate >= 16000) {
		x = 20000;
		_frameBits = 12;
	} else {
		x = 16000;
		_frameBits = 11;
	}
	_frameSize = 1 << _frameBits;
	_subframeSize = _frameSize >> 5;

	if (_channels == 2)
		x = 3 * x / 2;
	_bandIndex = kNoiseBandsSelector[MIN(6, (int)floor(bitRate * 3.0 / (double)x + 0.5))];

	// The inverse FFT covers twice the subframe size
	int fftBits = Common::intLog2(fftSize) + 1;
	int fftLength = MAX(1 << fftBits, 2 * _subframeSize);
	_fft = new Math::FFT(fftBits, 1);
	for (int ch = 0; ch < 2; ch++) {
		_fftData[ch] = new Math::Complex[fftLength];
		memset(_fftData[ch], 0, sizeof(Math::Complex) * fftLength);
	}

	for (int g = 5; g > 0; g--) {
		for (int j = 0; j < (1 << g) - 1; j++)
			_altSin[5 - g][j] = _sinTable[((j + 1) << (8 - g)) & 0x1FF];
	}

	makeNoises();
}

QDMCStream::~QDMCStream() {
	delete _fft;
	delete[] _fftData[0];
	delete[] _fftData[1];
}

void QDMCStream::flush() {
	memset(_buffer, 0, sizeof(_buffer));
	memset(_fftBuffer, 0, sizeof(_fftBuffer));
	_fftOffset = 0;
	_bufferOffset = 0;
}

void QDMCStream::makeNoises() {
	for (int j = 0; j < kNoiseBandsSize[_bandIndex]; j++) {
		int n0 = kNodes[j + 21 * _bandIndex];
		int n1 = kNodes[j + 21 * _bandIndex + 1];
		int n2 = kNodes[j + 21 * _bandIndex + 2];
		float *nptr = _noiseBuffer + 256 * j;

		for (int i = 0; i + n0 < n1; i++, nptr++)
			nptr[0] = i / (float)(n1 - n0);

		int diff = n2 - n1;
		nptr = _noiseBuffer + (j << 8) + n1 - n0;

		for (int i = n1; i < n2; i++, nptr++, diff--)
			nptr[0] = diff / (float)(n2 - n1);
	}
}

int QDMCStream::getVLC(QDMCBitStream &bits, int table, bool flag) const {
	if (bits.pos() >= bits.size())
		return -1;

	int v = _huffman[table].getSymbol(bits);
	if (v < 0)
		v = bits.getBits(bits.getBits(3) + 1);

	if (flag) {
		if (v >= ARRAYSIZE(kCodePrefix))
			return -1;

		v = kCodePrefix[v] + bits.getBits(v >> 2);
	}

	return v;
}

bool QDMCStream::checkLabel(const byte *data) const {
	// A packet starts with the label "QMC\1" and a checksum of the bytes that follow
	if (READ_BE_UINT32(data) != MKTAG('Q', 'M', 'C', 1))
		return false;

	uint16 sum = 226;
	for (uint32 i = 6; i < _packetSize; i++)
		sum += data[i];

	return sum == READ_LE_UINT16(data + 4);
}

bool QDMCStream::readNoiseData(QDMCBitStream &bits) {
	for (uint ch = 0; ch < _channels; ch++) {
		for (int band = 0; band < kNoiseBandsSize[_bandIndex]; band++) {
			int v = getVLC(bits, 0, false);
			if (v < 0)
				return false;

			if (v & 1)
				v = v + 1;
			else
				v = -v;

			int lastVal = v / 2;
			_noise[ch][band][0] = lastVal - 1;
			for (int j = 0; j < 15;) {
				int len = getVLC(bits, 1, true);
				if (len < 0)
					return false;
				len += 1;

				v = getVLC(bits, 0, false);
				if (v < 0)
					return false;

				int newVal;
				if (v & 1)
					newVal = lastVal + (v + 1) / 2;
				else
					newVal = lastVal - v / 2;

				int idx = j + 1;
				if (len + idx > 16)
					return false;

				for (int k = 1; idx <= j + len; k++, idx++)
					_noise[ch][band][idx] = lastVal + k * (newVal - lastVal) / len - 1;

				lastVal = newVal;
				j += len;
			}
		}
	}

	return true;
}

void QDMCStream::addTone(int group, int offset, int freq, int stereoMode, int amplitude, int phase) {
	const int index = _nbTones[group];

	if (index >= ARRAYSIZE(_tones[group])) {
		warning("QDMCStream: Too many tones, ignoring tone");
		return;
	}

	_tones[group][index].offset = offset;
	_tones[group][index].freq = freq;
	_tones[group][index].mode = stereoMode;
	_tones[group][index].amplitude = amplitude;
	_tones[group][index].phase = phase;
	_nbTones[group]++;
}

bool QDMCStream::readWaveData(QDMCBitStream &bits) {
	int stereoMode = 0;
	int amp2 = 0, phase2 = 0;

	for (int group = 0; group < 5; group++) {
		int groupSize = 1 << (_frameBits - group - 1);
		int groupBits = 4 - group;
		int pos2 = 0;
		int off = 0;
		int freq;

		for (int i = 1;; i = freq + 1) {
			int v = getVLC(bits, 3, true);
			if (v < 0)
				return false;

			freq = i + v;
			while (freq >= groupSize - 1) {
				freq += 2 - groupSize;
				pos2 += groupSize;
				off += 1 << groupBits;
			}

			if (pos2 >= _frameSize)
				break;

			if (_channels > 1)
				stereoMode = bits.getBits(2);

			int amp = getVLC(bits, 2, false);
			if (amp < 0)
				return false;
			int phase = bits.getBits(3);

			if (stereoMode > 1) {
				amp2 = getVLC(bits, 4, false);
				if (amp2 < 0)
					return false;
				amp2 = amp - amp2;

				phase2 = getVLC(bits, 5, false);
				if (phase2 < 0)
					return false;
				phase2 = phase - phase2;

				if (phase2 < 0)
					phase2 += 8;
			}

			if ((freq >> groupBits) + 1 < _subframeSize) {
				addTone(group, off, freq, stereoMode & 1, amp, phase);
				if (stereoMode > 1)
					addTone(group, off, freq, ~stereoMode & 1, amp2, phase2);
			}
		}
	}

	return true;
}

void QDMCStream::linCalc(float amplitude, int node1, int node2, int index) {
	float scale = 0.5 * amplitude;
	int subframeSize = MIN(_subframeSize, node2);
	int length = (subframeSize - node1) & 0xFFFC;
	int j = node1;
	float *noisePtr = &_noiseBuffer[256 * index];

	for (int i = 0; i < length; i += 4, j += 4, noisePtr += 4) {
		_noise2Buffer[j] += scale * noisePtr[0];
		_noise2Buffer[j + 1] += scale * noisePtr[1];
		_noise2Buffer[j + 2] += scale * noisePtr[2];
		_noise2Buffer[j + 3] += scale * noisePtr[3];
	}

	int k = length + node1;
	noisePtr = _noiseBuffer + length + (index << 8);
	for (int i = length; i < subframeSize - node1; i++, k++, noisePtr++)
		_noise2Buffer[k] += scale * noisePtr[0];
}

void QDMCStream::addNoise(int ch, int currentSubframe) {
	float *im = &_fftBuffer[0 + ch][_fftOffset + _subframeSize * currentSubframe];
	float *re = &_fftBuffer[2 + ch][_fftOffset + _subframeSize * currentSubframe];

	memset(_noise2Buffer, 0, 4 * _subframeSize);

	for (int i = 0; i < kNoiseBandsSize[_bandIndex]; i++) {
		if (kNodes[i + 21 * _bandIndex] > _subframeSize - 1)
			break;

		int aindex = _noise[ch][i][currentSubframe / 2];
		float amplitude = aindex > 0 ? kAmplitudeTable[aindex & 0x3F] : 0.0f;

		linCalc(amplitude, kNodes[21 * _bandIndex + i], kNodes[21 * _bandIndex + i + 2], i);
	}

	for (int j = 2; j < _subframeSize - 1; j++) {
		_rndval = 214013U * _rndval + 2531011;
		float rndIm = ((_rndval & 0x7FFF) - 16384.0f) * 0.000030517578f * _noise2Buffer[j];
		_rndval = 214013U * _rndval + 2531011;
		float rndRe = ((_rndval & 0x7FFF) - 16384.0f) * 0.000030517578f * _noise2Buffer[j];
		im[j] += rndIm;
		re[j] += rndRe;
		im[j + 1] -= rndIm;
		re[j + 1] -= rndRe;
	}
}

void QDMCStream::addWave(int offset, int freqs, int group, int stereoMode, int amp, int phase) {
	if (_channels == 1)
		stereoMode = 0;

	int groupBits = 4 - group;
	int pos = freqs >> (4 - group);
	float amplitude = kAmplitudeTable[amp & 0x3F];
	float *imPtr = &_fftBuffer[stereoMode][_fftOffset + _subframeSize * offset + pos];
	float *rePtr = &_fftBuffer[2 + stereoMode][_fftOffset + _subframeSize * offset + pos];
	int pindex = (phase << 6) - ((2 * (freqs >> (4 - group)) + 1) << 7);

	for (int j = 0; j < (1 << (groupBits + 1)) - 1; j++) {
		pindex += (2 * freqs + 1) << (7 - groupBits);
		float level = amplitude * _altSin[group][j];
		float im = level * _sinTable[pindex & 0x1FF];
		float re = level * _sinTable[(pindex + 128) & 0x1FF];
		imPtr[0] += im;
		imPtr[1] -= im;
		rePtr[0] += re;
		rePtr[1] -= re;
		imPtr += _subframeSize;
		rePtr += _subframeSize;
		if (imPtr >= &_fftBuffer[stereoMode][2 * _frameSize]) {
			imPtr = &_fftBuffer[0 + stereoMode][pos];
			rePtr = &_fftBuffer[2 + stereoMode][pos];
		}
	}
}

void QDMCStream::addWave0(int offset, int freqs, int stereoMode, int amp, int phase) {
	if (_channels == 1)
		stereoMode = 0;

	float level = kAmplitudeTable[amp & 0x3F];
	float im = level * _sinTable[(phase << 6) & 0x1FF];
	float re = level * _sinTable[((phase << 6) + 128) & 0x1FF];
	int pos = _fftOffset + freqs + _subframeSize * offset;
	_fftBuffer[stereoMode][pos] += im;
	_fftBuffer[2 + stereoMode][pos] += re;
	_fftBuffer[stereoMode][pos + 1] -= im;
	_fftBuffer[2 + stereoMode][pos + 1] -= re;
}

void QDMCStream::addWaves(int currentSubframe) {
	int w;

	for (int g = 0; g < 4; g++) {
		for (w = _curTone[g]; w < _nbTones[g]; w++) {
			const Tone &t = _tones[g][w];

			if (currentSubframe < t.offset)
				break;
			addWave(t.offset, t.freq, g, t.mode, t.amplitude, t.phase);
		}
		_curTone[g] = w;
	}

	for (w = _curTone[4]; w < _nbTones[4]; w++) {
		const Tone &t = _tones[4][w];

		if (currentSubframe < t.offset)
			break;
		addWave0(t.offset, t.freq, t.mode, t.amplitude, t.phase);
	}
	_curTone[4] = w;
}

bool QDMCStream::decodePacket(const byte *data, int16 *out) {
	if (!checkLabel(data))
		return false;

	Common::BitStreamMemoryStream memStream(data + 6, _packetSize - 6);
	QDMCBitStream bits(&memStream);

	memset(_nbTones, 0, sizeof(_nbTones));
	memset(_curTone, 0, sizeof(_curTone));

	_fftOffset = _frameSize - _fftOffset;
	_bufferPtr = &_buffer[_channels * _bufferOffset];

	if (!readNoiseData(bits) || !readWaveData(bits))
		return false;

	for (int n = 0; n < 32; n++) {
		for (uint ch = 0; ch < _channels; ch++)
			addNoise(ch, n);

		addWaves(n);

		for (uint ch = 0; ch < _channels; ch++) {
			Math::Complex *z = _fftData[ch];
			for (int i = 0; i < _subframeSize; i++) {
				z[i].re = _fftBuffer[ch + 2][_fftOffset + n * _subframeSize + i];
				z[i].im = _fftBuffer[ch + 0][_fftOffset + n * _subframeSize + i];
				z[_subframeSize + i].re = 0;
				z[_subframeSize + i].im = 0;
			}
			_fft->permute(z);
			_fft->calc(z);
		}

		float *r = &_bufferPtr[_channels * n * _subframeSize];
		for (int i = 0; i < 2 * _subframeSize; i++) {
			for (uint ch = 0; ch < _channels; ch++)
				*r++ += _fftData[ch][i].re;
		}

		r = &_bufferPtr[n * _subframeSize * _channels];
		for (uint i = 0; i < _channels * _subframeSize; i++)
			out[i] = (int16)CLIP<float>(r[i], -32768.0f, 32767.0f);
		out += _subframeSize * _channels;

		for (uint ch = 0; ch < _channels; ch++) {
			memset(_fftBuffer[ch + 0] + _fftOffset + n * _subframeSize, 0, 4 * _subframeSize);
			memset(_fftBuffer[ch + 2] + _fftOffset + n * _subframeSize, 0, 4 * _subframeSize);
		}
		memset(_buffer + _channels * (n * _subframeSize + _frameSize + _bufferOffset), 0, 4 * _subframeSize * _channels);
	}

	_bufferOffset += _frameSize;
	if (_bufferOffset >= 32768 - _frameSize) {
		memcpy(_buffer, &_buffer[_channels * _bufferOffset], 4 * _frameSize * _channels);
		_bufferOffset = 0;
	}

	return true;
}

AudioStream *QDMCStream::decodeFrame(Common::SeekableReadStream &stream) {
	QueuingAudioStream *audioStream = makeQueuingAudioStream(_sampleRate, _channels == 2);

	byte flags = FLAG_16BITS;
	if (_channels == 2)
		flags |= FLAG_STEREO;
#ifdef SCUMM_LITTLE_ENDIAN
	flags |= FLAG_LITTLE_ENDIAN;
#endif

	byte *packet = (byte *)malloc(_packetSize);
	uint32 outputSize = _frameSize * _channels * 2;

	// Each packet holds one frame
	while (stream.size() - stream.pos() >= (int64)_packetSize) {
		stream.read(packet, _packetSize);

		int16 *output = (int16 *)malloc(outputSize);

		if (!decodePacket(packet, output)) {
			// Keep the timing with a frame of silence
			warning("QDMCStream: Invalid packet");
			flush();
			memset(output, 0, outputSize);
		}

		audioStream->queueBuffer((byte *)output, outputSize, DisposeAfterUse::YES, flags);
	}

	free(packet);
	audioStream->finish();
	return audioStream;
}

Codec *makeQDMCDecoder(Common::SeekableReadStream *extraData, DisposeAfterUse::Flag disposeExtraData) {
	if (!extraData)
		return nullptr;

	Codec *codec = nullptr;

	// Find the 'frma' atom naming QDMC; the 'QDCA' atom follows it
	extraData->seek(0);
	const uint64 frmaTag = ((uint64)MKTAG('f', 'r', 'm', 'a') << 32) | MKTAG('Q', 'D', 'M', 'C');
	uint64 tag = 0;
	while (tag != frmaTag && extraData->pos() < extraData->size())
		tag = (tag << 8) | extraData->readByte();

	if (tag == frmaTag && extraData->size() - extraData->pos() >= 36) {
		uint32 size = extraData->readUint32BE();
		uint32 type = extraData->readUint32BE();
		extraData->readUint32BE(); // version

		uint32 channels = extraData->readUint32BE();
		uint32 sampleRate = extraData->readUint32BE();
		uint32 bitRate = extraData->readUint32BE();
		extraData->readUint32BE(); // block size
		uint32 fftSize = extraData->readUint32BE();
		uint32 packetSize = extraData->readUint32BE();

		int fftBits = fftSize ? Common::intLog2(fftSize) + 1 : 0;

		if (type != MKTAG('Q', 'D', 'C', 'A') || size < 36)
			warning("QDMC: Invalid extra data");
		else if (channels < 1 || channels > 2)
			warning("QDMC: Invalid number of channels %d", channels);
		else if (fftBits < 7 || fftBits > 9 || fftSize != (1u << (fftBits - 1)))
			warning("QDMC: Unsupported FFT size %d", fftSize);
		else if (packetSize <= 6 || packetSize >= (1u << 28))
			warning("QDMC: Invalid packet size %d", packetSize);
		else if (sampleRate == 0)
			warning("QDMC: Invalid sample rate");
		else
			codec = new QDMCStream(channels, sampleRate, bitRate, fftSize, packetSize);
	} else {
		warning("QDMC: Missing extra data");
	}

	if (disposeExtraData == DisposeAfterUse::YES)
		delete extraData;

	return codec;
}

} // End of namespace Audio

#endif // USE_QDMC
