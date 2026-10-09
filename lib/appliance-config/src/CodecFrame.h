#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>

namespace appliance {
struct CodecClock {uint32_t scale=0,rate=0,milliseconds=0;uint64_t ticks=0;};
inline bool codecClock(uint32_t frames,uint32_t scale,uint32_t rate,CodecClock& out){
  if(!frames||!scale||!rate)return false;
  uint32_t a=scale,b=rate;while(b){uint32_t r=a%b;a=b;b=r;}
  out.scale=scale/a;out.rate=rate/a;out.ticks=uint64_t(frames)*out.scale;
  // Divide first: malformed/long source clocks must not overflow *1000.
  uint64_t seconds=out.ticks/out.rate;
  if(seconds>UINT32_MAX/1000)return false;
  uint64_t ms=seconds*1000+(out.ticks%out.rate)*1000/out.rate;
  if(ms>UINT32_MAX-1000)return false;out.milliseconds=uint32_t(ms);return true;
}
inline bool codecBuffersFit(size_t freeBytes,size_t largest,size_t jpeg,size_t raw,size_t encoded,size_t block){
  uint64_t required=uint64_t(jpeg)+raw+encoded+block+256*1024;
  return freeBytes>=required&&largest>=std::max(std::max(jpeg,raw),std::max(encoded,block));
}
// Match the existing full-range UYVY -> limited-range YUYV -> I420 pipeline.
// Chroma uses the first row of each pair, matching the pinned encoder converter.
inline bool codecBlockToI420(const uint8_t* block,size_t bytes,uint8_t* frame,uint16_t width,uint16_t height,uint16_t firstRow){
  if(!block||!frame||!width||(width&1)||(height&1)||bytes%(size_t(width)*2))return false;
  size_t rows=bytes/(size_t(width)*2);if(firstRow>height||!rows||rows>size_t(height-firstRow))return false;
  const size_t pixels=size_t(width)*height;
  for(size_t row=0;row<rows;++row){size_t yrow=firstRow+row;
    for(size_t x=0;x<width;x+=2){const uint8_t* p=block+(row*width+x)*2;
      frame[yrow*width+x]=16+(uint32_t(p[1])*219+127)/255;
      frame[yrow*width+x+1]=16+(uint32_t(p[3])*219+127)/255;
      if(!(yrow&1)){size_t at=(yrow/2)*(width/2)+x/2;
        frame[pixels+at]=16+(uint32_t(p[0])*224+127)/255;
        frame[pixels+pixels/4+at]=16+(uint32_t(p[2])*224+127)/255;}
    }
  }return true;
}
}
