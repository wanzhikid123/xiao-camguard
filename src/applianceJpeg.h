#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr unsigned APPLIANCE_CAMERA_BUFFERS=4;
// Frame metadata reflects the current sensor setting, including queued frames
// captured before a resolution change. Read the JPEG's actual SOF dimensions.
inline bool applianceJpegSize(const uint8_t* bytes,size_t length,uint16_t& width,uint16_t& height){
  if(length<4||bytes[0]!=255||bytes[1]!=216)return false;
  for(size_t pos=2;pos+4<=length;){if(bytes[pos++]!=255)return false;
    while(pos<length&&bytes[pos]==255)++pos;if(pos>=length)return false;uint8_t marker=bytes[pos++];
    if(marker==0xda||marker==0xd9)return false;if(pos+2>length)return false;
    size_t count=size_t(bytes[pos])<<8|bytes[pos+1];if(count<2||count>length-pos)return false;
    if(marker==0xc0||marker==0xc1||marker==0xc2){if(count<8)return false;
      height=uint16_t(bytes[pos+3])<<8|bytes[pos+4];width=uint16_t(bytes[pos+5])<<8|bytes[pos+6];return width&&height;}
    pos+=count;
  }return false;
}
