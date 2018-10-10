/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2018 Stefan Löffler

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "common/Array2DRef.h"                  // for Array2DRef
#include "common/Common.h"                      // for uint32
#include "common/RawImage.h"                    // for RawImageData
#include "common/SimpleLUT.h"                   // for SimpleLUT
#include "decompressors/AbstractDecompressor.h" // for AbstractDecompressor
#include "io/BitPumpMSB.h"                      // for BitPumpMSB
#include "io/ByteStream.h"                      // for ByteStream

namespace rawspeed {

const int MAX_NUM_PRESCALE = 8;

class ByteStream;
class RawImage;

// Decompresses VC-5 as used by GoPro

class VC5Decompressor final : public AbstractDecompressor {
  RawImage mImg;
  ByteStream mBs;

  static constexpr auto VC5_LOG_TABLE_BITWIDTH = 12;
  SimpleLUT<unsigned, VC5_LOG_TABLE_BITWIDTH> mVC5LogTable;

  static constexpr int numSubbands = 10;

  struct {
    ushort16 iChannel, iSubband;
    ushort16 imgWidth, imgHeight, imgFormat;
    ushort16 patternWidth, patternHeight;
    ushort16 cps, bpc, lowpassPrecision;
    uint8_t image_sequence_identifier[16];
    uint32_t image_sequence_number;
    short16 quantization;
  } mVC5;

  class Wavelet {
  public:
    uint16_t width, height, pitch;
    std::vector<int16_t> data_storage;

    static constexpr uint16_t numBands = 4;
    std::array<uint16_t, numBands> scale = {};
    std::array<int16_t, numBands> quant = {};
    std::array<int16_t*, numBands> data = {};

    void initialize(uint16_t waveletWidth, uint16_t waveletHeight);
    void clear();

    bool isInitialized() const { return mInitialized; }
    void setBandValid(int band);
    bool isBandValid(int band) const;
    uint32_t getValidBandMask() const { return mDecodedBandMask; }
    bool allBandsValid() const;

    void reconstructLowband(Array2DRef<int16_t> dest, int16_t prescale,
                            bool clampUint = false);

    Array2DRef<int16_t> bandAsArray2DRef(unsigned int iBand);

  protected:
    uint32 mDecodedBandMask = 0;
    bool mInitialized = false;

    static void dequantize(Array2DRef<int16_t> out, Array2DRef<int16_t> in,
                           int16_t quant);
  };

  struct Transform {
    static constexpr int numWavelets = 3;
    std::array<Wavelet, numWavelets> wavelet;
    std::array<int16_t, numWavelets> prescale;
  };

  static constexpr int numChannels = 4;
  std::array<Transform, numChannels> mTransforms;

  static void getRLV(BitPumpMSB* bits, int* value, unsigned int* count);

  void decodeLargeCodeblock(const ByteStream& bs);

  // FIXME: this *should* be threadedable nicely.
  void decodeFinalWavelet();

public:
  VC5Decompressor(ByteStream bs, const RawImage& img);

  void decode(unsigned int offsetX, unsigned int offsetY);
};

} // namespace rawspeed
