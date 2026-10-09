#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace appliance {
// Version 2 frames already contain visible clock pixels. The APP15 metadata
// keeps their original capture time for inspection; conversion does not draw it.
constexpr size_t EVENT_CLOCK_BYTES = 19, EVENT_MARKER_BYTES = 31;
inline bool validEventClock(const char* clock) {
  for(size_t i=0;i<EVENT_CLOCK_BYTES;++i) {
    const char separator=i==4||i==7?'-':i==10?' ':i==13||i==16?':':0;
    if(separator ? clock[i]!=separator : clock[i]<'0'||clock[i]>'9')return false;
  }
  return true;
}
inline bool eventTimestampMarker(uint8_t (&marker)[EVENT_MARKER_BYTES],const char* clock) {
  if(!validEventClock(clock))return false;
  marker[0]=255;marker[1]=239;marker[2]=0;marker[3]=29;
  memcpy(marker+4,"CGEVT002",8);memcpy(marker+12,clock,EVENT_CLOCK_BYTES);return true;
}
inline bool readEventTimestamp(const uint8_t* jpeg,size_t bytes,char (&clock)[20]) {
  if(bytes<2||jpeg[0]!=255||jpeg[1]!=216)return false;
  size_t at=2;
  while(at+4<=bytes&&jpeg[at]==255) {
    const uint8_t kind=jpeg[at+1];if(kind==218||kind==217)return false;
    const size_t count=size_t(jpeg[at+2])*256+jpeg[at+3];
    if(count<2||count>bytes-at-2)return false;
    if(kind==239&&count==29&&!memcmp(jpeg+at+4,"CGEVT002",8)) {
      memcpy(clock,jpeg+at+12,19);clock[19]=0;return validEventClock(clock);
    }
    at+=2+count;
  }
  return false;
}
inline const uint8_t* eventClockGlyph(char c) {
  static constexpr uint8_t digits[10][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}};
  static constexpr uint8_t dash[7]={0,0,0,31,0,0,0}, colon[7]={0,4,4,0,4,4,0}, blank[7]={};
  return c>='0'&&c<='9'?digits[c-'0']:c=='-'?dash:c==':'?colon:blank;
}
inline bool drawEventClock(uint8_t* uyvy,uint16_t width,uint16_t height,const char* clock) {
  if(!uyvy||width<248||height<30||width%2||!validEventClock(clock))return false;
  constexpr unsigned scale=2, cell=12, top=8, margin=8, boxHeight=18;
  const unsigned left=width-margin-EVENT_CLOCK_BYTES*cell;
  // JPEG decoder/encoder use full-range UYVY: chroma at byte 0, luma at byte 1.
  for(unsigned y=top;y<top+boxHeight;++y)for(unsigned x=left-2;x<width-margin;++x) {
    uyvy[(size_t(y)*width+x)*2]=128;uyvy[(size_t(y)*width+x)*2+1]=0;
  }
  for(unsigned i=0;i<EVENT_CLOCK_BYTES;++i) {
    const uint8_t* glyph=eventClockGlyph(clock[i]);
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<5;++x)if(glyph[y]&(16>>x))
      for(unsigned dy=0;dy<scale;++dy)for(unsigned dx=0;dx<scale;++dx)
        uyvy[(size_t(top+2+y*scale+dy)*width+left+i*cell+x*scale+dx)*2+1]=255;
  }
  return true;
}
} // namespace appliance
