/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post

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

#include "rawspeedconfig.h"            // for WITH_SSE2
#include "ThreadSafetyAnalysis.h"      // for GUARDED_BY, REQUIRES
#include "common/Array2DRef.h"         // for Array2DRef
#include "common/Common.h"             // for writeLog, DEBUG_PRIO_ERROR
#include "common/CroppedArray2DRef.h"  // for CroppedArray2DRef
#include "common/ErrorLog.h"           // for ErrorLog
#include "common/Mutex.h"              // for Mutex
#include "common/Point.h"              // for iPoint2D, iRectangle2D (ptr o...
#include "common/TableLookUp.h"        // for TableLookUp
#include "metadata/BlackArea.h"        // for BlackArea
#include "metadata/ColorFilterArray.h" // for ColorFilterArray
#include <array>                       // for array
#include <cassert>                     // for assert
#include <cmath>                       // for NAN
#include <cstdint>                     // for uint32_t, uint16_t, uint8_t
#include <memory>                      // for unique_ptr, operator==
#include <string>                      // for string
#include <vector>                      // for vector

namespace rawspeed {

class RawImage;

class RawImageData;

enum RawImageType { TYPE_USHORT16, TYPE_FLOAT32 };

class RawImageWorker {
public:
  enum RawImageWorkerTask {
    SCALE_VALUES = 1, FIX_BAD_PIXELS = 2, APPLY_LOOKUP = 3 | 0x1000, FULL_IMAGE = 0x1000
  };

private:
  RawImageData* data;
  RawImageWorkerTask task;
  int start_y;
  int end_y;

  void performTask() noexcept;

public:
  RawImageWorker(RawImageData* img, RawImageWorkerTask task, int start_y,
                 int end_y) noexcept;
};

class ImageMetaData {
public:
  // Aspect ratio of the pixels, usually 1 but some cameras need scaling
  // <1 means the image needs to be stretched vertically, (0.5 means 2x)
  // >1 means the image needs to be stretched horizontally (2 mean 2x)
  double pixelAspectRatio = 1;

  // White balance coefficients of the image
  std::array<float, 4> wbCoeffs = {{NAN, NAN, NAN, NAN}};

  // How many pixels far down the left edge and far up the right edge the image
  // corners are when the image is rotated 45 degrees in Fuji rotated sensors.
  uint32_t fujiRotationPos = 0;

  iPoint2D subsampling = {1, 1};
  std::string make;
  std::string model;
  std::string mode;

  std::string canonical_make;
  std::string canonical_model;
  std::string canonical_alias;
  std::string canonical_id;

  // ISO speed. If known the value is set, otherwise it will be '0'.
  int isoSpeed = 0;
};

class RawImageData : public ErrorLog {
  friend class RawImageWorker;
public:
  virtual ~RawImageData();
  [[nodiscard]] uint32_t getCpp() const { return cpp; }
  [[nodiscard]] uint32_t getBpp() const { return bpp; }
  void setCpp(uint32_t val);
  void createData();
  void poisonPadding();
  void unpoisonPadding();
  void checkRowIsInitialized(int row);
  void checkMemIsInitialized();
  void destroyData();
  void blitFrom(const RawImage& src, const iPoint2D& srcPos,
                const iPoint2D& size, const iPoint2D& destPos);
  [[nodiscard]] rawspeed::RawImageType getDataType() const { return dataType; }
  [[nodiscard]] inline Array2DRef<uint16_t>
  getU16DataAsUncroppedArray2DRef() const noexcept;
  [[nodiscard]] inline CroppedArray2DRef<uint16_t>
  getU16DataAsCroppedArray2DRef() const noexcept;
  [[nodiscard]] uint8_t* getData() const;
  uint8_t*
  getData(uint32_t x,
          uint32_t y); // Not super fast, but safe. Don't use per pixel.
  uint8_t* getDataUncropped(uint32_t x, uint32_t y);

  void subFrame(iRectangle2D cropped);
  void clearArea(iRectangle2D area, uint8_t value = 0);
  [[nodiscard]] iPoint2D __attribute__((pure)) getUncroppedDim() const;
  [[nodiscard]] iPoint2D __attribute__((pure)) getCropOffset() const;
  virtual void scaleBlackWhite() = 0;
  virtual void calculateBlackAreas() = 0;
  virtual void setWithLookUp(uint16_t value, uint8_t* dst,
                             uint32_t* random) = 0;
  void sixteenBitLookup();
  void transferBadPixelsToMap() REQUIRES(!mBadPixelMutex);
  void fixBadPixels() REQUIRES(!mBadPixelMutex);
  void expandBorder(iRectangle2D validData);
  void setTable(const std::vector<uint16_t>& table_, bool dither);
  void setTable(std::unique_ptr<TableLookUp> t);

  bool isAllocated() {return !!data;}
  void createBadPixelMap();
  iPoint2D dim;
  int pitch = 0;

  // padding is the size of the area after last pixel of line n
  // and before the first pixel of line n+1
  uint32_t padding = 0;

  bool isCFA{true};
  ColorFilterArray cfa;
  int blackLevel = -1;
  std::array<int, 4> blackLevelSeparate;
  int whitePoint = 65536;
  std::vector<BlackArea> blackAreas;

  /* Vector containing the positions of bad pixels */
  /* Format is x | (y << 16), so maximum pixel position is 65535 */
  // Positions of zeroes that must be interpolated
  std::vector<uint32_t> mBadPixelPositions GUARDED_BY(mBadPixelMutex);
  uint8_t* mBadPixelMap = nullptr;
  uint32_t mBadPixelMapPitch = 0;
  bool mDitherScale =
      true; // Should upscaling be done with dither to minimize banding?
  ImageMetaData metadata;

  Mutex mBadPixelMutex; // Mutex for 'mBadPixelPositions, must be used if more
                        // than 1 thread is accessing vector

private:
  uint32_t dataRefCount GUARDED_BY(mymutex) = 0;

protected:
  RawImageType dataType;
  RawImageData();
  RawImageData(const iPoint2D& dim, int bpp, int cpp = 1);
  virtual void scaleValues(int start_y, int end_y) = 0;
  virtual void doLookup(int start_y, int end_y) = 0;
  virtual void fixBadPixel(uint32_t x, uint32_t y, int component = 0) = 0;
  void fixBadPixelsThread(int start_y, int end_y);
  void startWorker(RawImageWorker::RawImageWorkerTask task, bool cropped );
  uint8_t* data = nullptr;
  int cpp = 1; // Components per pixel
  int bpp = 0; // Bytes per pixel.
  friend class RawImage;
  iPoint2D mOffset;
  iPoint2D uncropped_dim;
  std::unique_ptr<TableLookUp> table;
  Mutex mymutex;
};

class RawImageDataU16 final : public RawImageData {
public:
  void scaleBlackWhite() override;
  void calculateBlackAreas() override;
  void setWithLookUp(uint16_t value, uint8_t* dst, uint32_t* random) override;

private:
  void scaleValues_plain(int start_y, int end_y);
#ifdef WITH_SSE2
  void scaleValues_SSE2(int start_y, int end_y);
#endif
  void scaleValues(int start_y, int end_y) override;
  void fixBadPixel(uint32_t x, uint32_t y, int component = 0) override;
  void doLookup(int start_y, int end_y) override;

  RawImageDataU16();
  explicit RawImageDataU16(const iPoint2D& dim_, uint32_t cpp_ = 1);
  friend class RawImage;
};

class RawImageDataFloat final : public RawImageData {
public:
  void scaleBlackWhite() override;
  void calculateBlackAreas() override;
  void setWithLookUp(uint16_t value, uint8_t* dst, uint32_t* random) override;

private:
  void scaleValues(int start_y, int end_y) override;
  void fixBadPixel(uint32_t x, uint32_t y, int component = 0) override;
  [[noreturn]] void doLookup(int start_y, int end_y) override;
  RawImageDataFloat();
  explicit RawImageDataFloat(const iPoint2D& dim_, uint32_t cpp_ = 1);
  friend class RawImage;
};

 class RawImage {
 public:
   static RawImage create(RawImageType type = TYPE_USHORT16);
   static RawImage create(const iPoint2D& dim,
                          RawImageType type = TYPE_USHORT16,
                          uint32_t componentsPerPixel = 1);
   RawImageData* operator->() const { return p_; }
   RawImageData& operator*() const { return *p_; }
   explicit RawImage(RawImageData* p); // p must not be NULL
   ~RawImage();
   RawImage(const RawImage& p);
   RawImage& operator=(const RawImage& p) noexcept;
   RawImage& operator=(RawImage&& p) noexcept;

   RawImageData* get() { return p_; }
 private:
   RawImageData* p_;    // p_ is never NULL
 };

inline RawImage RawImage::create(RawImageType type)  {
  switch (type)
  {
    case TYPE_USHORT16:
      return RawImage(new RawImageDataU16());
    case TYPE_FLOAT32:
      return RawImage(new RawImageDataFloat());
    default:
      writeLog(DEBUG_PRIO_ERROR, "RawImage::create: Unknown Image type!");
      __builtin_unreachable();

  }
}

inline RawImage RawImage::create(const iPoint2D& dim, RawImageType type,
                                 uint32_t componentsPerPixel) {
  switch (type) {
  case TYPE_USHORT16:
    return RawImage(new RawImageDataU16(dim, componentsPerPixel));
  case TYPE_FLOAT32:
    return RawImage(new RawImageDataFloat(dim, componentsPerPixel));
  default:
    writeLog(DEBUG_PRIO_ERROR, "RawImage::create: Unknown Image type!");
    __builtin_unreachable();
  }
}

inline Array2DRef<uint16_t>
RawImageData::getU16DataAsUncroppedArray2DRef() const noexcept {
  assert(dataType == TYPE_USHORT16 &&
         "Attempting to access floating-point buffer as uint16_t.");
  assert(data && "Data not yet allocated.");
  return {reinterpret_cast<uint16_t*>(data), cpp * uncropped_dim.x,
          uncropped_dim.y, static_cast<int>(pitch / sizeof(uint16_t))};
}

inline CroppedArray2DRef<uint16_t>
RawImageData::getU16DataAsCroppedArray2DRef() const noexcept {
  return {getU16DataAsUncroppedArray2DRef(), cpp * mOffset.x, mOffset.y,
          cpp * dim.x, dim.y};
}

// setWithLookUp will set a single pixel by using the lookup table if supplied,
// You must supply the destination where the value should be written, and a pointer to
// a value that will be used to store a random counter that can be reused between calls.
// this needs to be inline to speed up tight decompressor loops
inline void RawImageDataU16::setWithLookUp(uint16_t value, uint8_t* dst,
                                           uint32_t* random) {
  auto* dest = reinterpret_cast<uint16_t*>(dst);
  if (table == nullptr) {
    *dest = value;
    return;
  }
  if (table->dither) {
    const auto* t = reinterpret_cast<const uint32_t*>(table->tables.data());
    uint32_t lookup = t[value];
    uint32_t base = lookup & 0xffff;
    uint32_t delta = lookup >> 16;
    uint32_t r = *random;

    uint32_t pix = base + ((delta * (r & 2047) + 1024) >> 12);
    *random = 15700 *(r & 65535) + (r >> 16);
    *dest = pix;
    return;
  }
  *dest = table->tables[value];
}

class RawImageCurveGuard final {
  RawImage* mRaw;
  const std::vector<uint16_t>& curve;
  const bool uncorrectedRawValues;

public:
  RawImageCurveGuard(RawImage* raw, const std::vector<uint16_t>& curve_,
                     bool uncorrectedRawValues_)
      : mRaw(raw), curve(curve_), uncorrectedRawValues(uncorrectedRawValues_) {
    if (uncorrectedRawValues)
      return;

    (*mRaw)->setTable(curve, true);
  }

  ~RawImageCurveGuard() {
    // Set the table, if it should be needed later.
    if (uncorrectedRawValues)
      (*mRaw)->setTable(curve, false);
    else
      (*mRaw)->setTable(nullptr);
  }
};

} // namespace rawspeed
