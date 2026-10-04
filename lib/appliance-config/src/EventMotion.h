#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
namespace appliance {
// Spatial changes after subtracting a uniform exposure/lighting offset.
// Comparing consecutive small grayscale frames also lets a moved object
// become part of the static scene instead of recording forever.
inline unsigned eventChangedPermille(const uint8_t* current, const uint8_t* previous,
                                     size_t count, unsigned pixelThreshold = 16) {
  if (!count) return 0;
  int32_t sum = 0;
  for (size_t i=0; i<count; ++i) sum += int(current[i])-int(previous[i]);
  const int offset = sum / int32_t(count);
  size_t changed=0;
  for (size_t i=0; i<count; ++i)
    if (abs(int(current[i])-int(previous[i])-offset) >= int(pixelThreshold)) ++changed;
  return unsigned(changed*1000/count);
}
inline unsigned eventMotionThreshold(unsigned sensitivity) {
  return 110 - sensitivity*10; // 1..10: 10%..1% of the image.
}
}
