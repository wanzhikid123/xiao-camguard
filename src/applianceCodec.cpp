#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceCodec.h"
#include "applianceSettings.h"
#include "esp_h264_enc_single_sw.h"
#include "esp_jpeg_dec.h"
#include "esp_aac_enc.h"
#include "mp4_muxer.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <atomic>
#include "CodecFrame.h"

namespace {
std::atomic<bool> codecAllocationFailed{false};
std::atomic<uint32_t> initialPsram{0},minimumPsram{0},minimumLargestBlock{0},rawBytes{0},blockBytes{0};
void sampleMemory(){uint32_t free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  minimumPsram.store(std::min(minimumPsram.load(),free));minimumLargestBlock.store(std::min(minimumLargestBlock.load(),largest));}
void* codecAllocated(void* p){if(!p)codecAllocationFailed.store(true);sampleMemory();return p;}
}

// Only the pinned software codec archives reference these allocators. Generic
// SDK malloc, explicit capability allocations and RTOS locks retain their rules.
extern "C" void* appliance_codec_malloc(size_t bytes){return codecAllocated(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));}
extern "C" void* appliance_codec_calloc(size_t count,size_t bytes){return codecAllocated(heap_caps_calloc(count,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));}
extern "C" void* appliance_codec_realloc(void* pointer,size_t bytes){void* p=heap_caps_realloc(pointer,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(bytes)codecAllocated(p);return p;}
// The adapted JPEG component uses CPU-only workspaces. Preserve alignment and
// put those allocations in PSRAM so live camera/TCP DMA retains internal RAM.
extern "C" void* appliance_jpeg_calloc_prefer(size_t count,size_t bytes,size_t,...){
  return codecAllocated(heap_caps_calloc(count,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
}
extern "C" void* appliance_jpeg_aligned_calloc(size_t alignment,size_t count,size_t bytes,uint32_t){
  return codecAllocated(heap_caps_aligned_calloc(alignment,count,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
}

namespace {
uint32_t le32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint32_t be32(const uint8_t* p){return uint32_t(p[3])|uint32_t(p[2])<<8|uint32_t(p[1])<<16|uint32_t(p[0])<<24;}
bool readAt(File& f,uint32_t at,void* p,size_t n){return uint64_t(at)+n<=f.size()&&f.seek(at)&&f.read(static_cast<uint8_t*>(p),n)==n;}
bool write32(File& f,uint32_t at,uint32_t value){uint8_t bytes[]={uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};
  return uint64_t(at)+4<=f.size()&&f.seek(at)&&f.write(bytes,4)==4;}
bool normalizeTimeline(const char* path,const ApplianceCodecReport& expected,ApplianceCodecAbort abort);
bool fingerprint(File& input,uint32_t& result,ApplianceCodecAbort abort){uint8_t bytes[1024];uint32_t crc=0xffffffff;
  if(!input.seek(0))return false;
  while(input.available()){if(abort&&abort())return false;size_t n=input.read(bytes,sizeof(bytes));if(!n)return false;
    for(size_t i=0;i<n;++i){crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&uint32_t(-int32_t(crc&1)));}vTaskDelay(1);}
  result=~crc;return true;}
bool normalizeAvcHeader(File& file,uint32_t start,uint32_t end,unsigned depth,unsigned& repaired){
  if(depth>8)return false;uint8_t data[12];uint32_t pos=start;
  while(pos<end){if(!readAt(file,pos,data,8))return false;uint32_t size=be32(data),body=pos+8,limit=pos+size;
    if(size<8||uint64_t(pos)+size>end)return false;
    if(!memcmp(data+4,"moov",4)||!memcmp(data+4,"trak",4)||!memcmp(data+4,"mdia",4)||!memcmp(data+4,"minf",4)||!memcmp(data+4,"stbl",4)){
      if(!normalizeAvcHeader(file,body,limit,depth+1,repaired))return false;
    }else if(!memcmp(data+4,"stsd",4)){
      if(size<24||!readAt(file,body+4,data,12)||be32(data)!=1)return false;
      if(!memcmp(data+8,"avc1",4)){if(size<102||!normalizeAvcHeader(file,body+8+86,limit,depth+1,repaired))return false;}
    }else if(!memcmp(data+4,"avcC",4)){
      if(size<23||!readAt(file,body,data,12)||data[0]!=1||(data[4]&3)!=3||!(data[5]&31)
        ||((uint16_t(data[6])<<8)|data[7])<4||(data[8]&31)!=7||data[9]!=66)return false;
      // The pinned muxer emits profile fields from the wrong SPS offset.
      // Correct the three avcC fields from the SPS bytes it actually stored.
      if(!file.seek(body+1)||file.write(data+9,3)!=3)return false;++repaired;
    }pos=limit;
  }return pos==end;}
bool normalizeAvcHeader(const char* path){File file=SD.open(path,"r+");unsigned repaired=0;
  bool ok=file&&normalizeAvcHeader(file,0,file.size(),0,repaired)&&repaired==1;
  if(file){file.flush();file.close();}return ok;}
struct Avi {
  uint32_t frames=0,scale=0,rate=0,maxJpeg=0,audioAt=0,audioBytes=0,indexAt=0;
  uint16_t width=0,height=0;
};
bool inspect(File& file,Avi& info,ApplianceCodecAbort abort) {
  uint8_t header[310];
  if(file.size()>UINT32_MAX||!readAt(file,0,header,sizeof(header))||memcmp(header,"RIFF",4)||memcmp(header+8,"AVI ",4)
    ||memcmp(header+306,"movi",4))return false;
  // This project's finalized writer has a 310-byte header and DWORD-padded 00dc chunks.
  // The handler/compression FourCC is zero in inherited files; JPEG data is verified separately.
  info.frames=le32(header+0x8c);info.scale=le32(header+0x80);info.rate=le32(header+0x84);
  const uint32_t width=le32(header+0xa8),height=le32(header+0xac);
  if(!info.frames||!info.scale||!info.rate||width>UINT16_MAX||height>UINT16_MAX)return false;
  info.width=width;info.height=height;
  uint32_t pos=310,frames=0;uint8_t chunk[8];
  while(uint64_t(pos)+8<=file.size()) {
    if(abort&&abort())return false;
    if(!readAt(file,pos,chunk,8))return false;
    const uint32_t bytes=le32(chunk+4);
    if(uint64_t(pos)+8+bytes>file.size())return false;
    if(!memcmp(chunk,"00dc",4)) {
      if(bytes<4||bytes>1048576)return false;
      uint8_t ends[4];if(!readAt(file,pos+8,ends,2)||ends[0]!=255||ends[1]!=216)return false;
      info.maxJpeg=std::max(info.maxJpeg,bytes);++frames;
    } else if(!memcmp(chunk,"01wb",4)) {
      if(info.audioBytes||!bytes||(bytes&1)||le32(header+0x11c)!=16000||header[0x11a]!=1||header[0x126]!=16)return false;
      info.audioAt=pos+8;info.audioBytes=bytes;
    } else if(!memcmp(chunk,"idx1",4)) {
      if(bytes!=(info.frames+(info.audioBytes?1:0))*16||uint64_t(pos)+8+bytes!=file.size())return false;
      info.indexAt=pos;break;
    } else return false;
    pos+=8+bytes+(bytes&1);vTaskDelay(1);
  }
  if(!info.indexAt||frames!=info.frames)return false;
  // Validate every offset/length against the chunk, rather than trusting the frame count.
  for(uint32_t i=0;i<frames+(info.audioBytes?1:0);++i) {
    uint8_t item[16];if(!readAt(file,info.indexAt+8+i*16,item,16))return false;
    const uint32_t offset=le32(item+8),length=le32(item+12);
    if(offset>info.indexAt-306||!readAt(file,306+offset,chunk,8)||memcmp(item,chunk,4)||length!=le32(chunk+4))return false;
    if((i<frames&&memcmp(item,"00dc",4))||(i==frames&&memcmp(item,"01wb",4)))return false;
    if(!(i%64)){if(abort&&abort())return false;vTaskDelay(1);}
  }
  return true;
}
struct Resources {
  File output;esp_h264_enc_handle_t h264=nullptr;jpeg_dec_handle_t jpeg=nullptr;
  void* aac=nullptr;esp_muxer_handle_t mux=nullptr;
  uint8_t *compressed=nullptr,*raw=nullptr,*encoded=nullptr,*block=nullptr,*pcm=nullptr,*sound=nullptr;
  uint8_t codecInfo[256]={};size_t codecInfoBytes=0;
  bool writeFailed=false,writerOpened=false;uint32_t high=0;
  void releaseVideo(){if(h264){esp_h264_enc_del(h264);h264=nullptr;}if(jpeg){jpeg_dec_close(jpeg);jpeg=nullptr;}
    free(compressed);compressed=nullptr;free(raw);raw=nullptr;free(encoded);encoded=nullptr;free(block);block=nullptr;}
  ~Resources(){if(mux)esp_muxer_close(mux);if(output){output.flush();output.close();}
    releaseVideo();if(aac)esp_aac_enc_close(aac);free(pcm);free(sound);}
};
// There is one low-priority codec owner; writer callbacks never run on HTTP/BLE callbacks.
Resources* writer=nullptr;const char* writerPath=nullptr;
void* openWriter(char* path){if(!writer||strcmp(path,writerPath)||writer->writerOpened||!writer->output)return nullptr;
  writer->writerOpened=true;return writer;}
int writeWriter(void* context,void* data,int bytes){auto& r=*static_cast<Resources*>(context);
  const size_t count=r.output.write(static_cast<uint8_t*>(data),bytes);if(count!=size_t(bytes))r.writeFailed=true;
  r.high=std::max(r.high,uint32_t(r.output.position()));return count;}
int seekWriter(void* context,uint64_t pos){auto& r=*static_cast<Resources*>(context);
  if(pos>UINT32_MAX||!r.output.seek(pos)){r.writeFailed=true;return -1;}return 0;}
int closeWriter(void* context){auto& r=*static_cast<Resources*>(context);r.output.flush();
  r.writeFailed=r.writeFailed||r.output.size()!=r.high;r.output.close();return r.writeFailed?-1:0;}
int filePattern(esp_muxer_slice_info_t* info,void*){if(info->slice_index||strlen(writerPath)>=size_t(info->len))return -1;
  strlcpy(info->file_path,writerPath,info->len);return 0;}
uint8_t* buffer(size_t bytes){return static_cast<uint8_t*>(heap_caps_aligned_alloc(16,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));}
bool fail(ApplianceCodecReport& r,const char* error){strlcpy(r.error,error,sizeof(r.error));return false;}
bool codecFail(ApplianceCodecReport& r,const char* error){return fail(r,codecAllocationFailed.load()?"psram_unavailable":error);}
bool parameterSets(Resources& r,size_t bytes){size_t pos=0;unsigned found=0;
  auto prefix=[&](size_t at)->size_t{if(at+3>bytes||r.encoded[at]||r.encoded[at+1])return 0;
    if(r.encoded[at+2]==1)return 3;return at+4<=bytes&&!r.encoded[at+2]&&r.encoded[at+3]==1?4:0;};
  while(pos<bytes){size_t start=pos;while(start<bytes&&!prefix(start))++start;if(start==bytes)break;
    size_t nal=start+prefix(start),end=nal;while(end<bytes&&!prefix(end))++end;if(nal==end)return false;
    unsigned type=r.encoded[nal]&31;if(type==7||type==8){if((found&(type==7?1:2))||r.codecInfoBytes+4+end-nal>sizeof(r.codecInfo))return false;
      uint8_t* target=r.codecInfo+r.codecInfoBytes;target[0]=target[1]=target[2]=0;target[3]=1;
      memcpy(target+4,r.encoded+nal,end-nal);r.codecInfoBytes+=4+end-nal;found|=type==7?1:2;}
    pos=end;
  }return found==3;}
}

bool applianceInspectAvi(const char* path,ApplianceCodecReport& report,ApplianceCodecAbort abort){
  File input=SD.open(path,FILE_READ);Avi info;
  if(!input||!inspect(input,info,abort))return false;
  report={};report.width=info.width;report.height=info.height;report.totalFrames=info.frames;
  report.timeScale=info.scale;report.timeRate=info.rate;report.audioSamples=info.audioBytes/2;report.sourceBytes=input.size();
  return fingerprint(input,report.sourceCrc,abort);
}

static bool convertIdleAvi(const char* source,const char* temporary,ApplianceCodecReport& report,
                         ApplianceCodecAbort abort,ApplianceCodecProgress progress) {
  report={};File input=SD.open(source,FILE_READ);Avi avi;const uint32_t started=millis();
  if(!input||!inspect(input,avi,abort))return fail(report,abort&&abort()?"aborted":"invalid_avi");
  report.sourceBytes=input.size();report.totalFrames=avi.frames;report.width=avi.width;report.height=avi.height;
  report.timeScale=avi.scale;report.timeRate=avi.rate;
  if((avi.width!=320||avi.height!=240)&&(avi.width!=640||avi.height!=480))return fail(report,"unsupported_source_resolution");
  ApplianceCodecReport identity;if(!applianceInspectAvi(source,identity,abort))return fail(report,abort&&abort()?"aborted":"source_read_failed");report.sourceCrc=identity.sourceCrc;
  appliance::CodecClock clock;if(!appliance::codecClock(avi.frames,avi.scale,avi.rate,clock))return fail(report,"unsupported_duration");
  if(SD.exists(temporary))return fail(report,"temporary_exists");
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  // One-shot fixture pressure acts after camera buffers are released. It
  // reaches the production admission path and always frees before resuming
  // the camera; it cannot touch any source/codec allocations or normal builds.
  struct Pressure {void* blocks[16]={};~Pressure(){for(void* p:blocks)free(p);}} pressure;
  if(applianceCodecTestMemoryPressure())for(void*& p:pressure.blocks){
    size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(largest<=64*1024)break;p=heap_caps_malloc(largest-64*1024,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p)break;}
  sampleMemory();
#endif
  struct Reset {~Reset(){writer=nullptr;writerPath=nullptr;}} reset;
  Resources r;writer=&r;writerPath=temporary;
  // Allocate the FILE object and its internal libc locks before the encoders.
  // The mux callback must not lazily allocate file locks at peak codec memory.
  r.output=SD.open(temporary,FILE_WRITE);if(!r.output)return fail(report,"output_open_failed");
  const uint32_t pixels=uint32_t(avi.width)*avi.height,outputCapacity=pixels*2+65536;
  const uint32_t rawCapacity=pixels*3/2,blockCapacity=uint32_t(avi.width)*16*2;
  rawBytes.store(rawCapacity);blockBytes.store(blockCapacity);
  if(ESP.getFreeHeap()<32768)return fail(report,"internal_memory_unavailable");
  if(!appliance::codecBuffersFit(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),avi.maxJpeg,rawCapacity,outputCapacity,blockCapacity))
    return fail(report,"psram_unavailable");
  r.compressed=buffer(avi.maxJpeg);r.raw=buffer(rawCapacity);r.encoded=buffer(outputCapacity);r.block=buffer(blockCapacity);sampleMemory();
  if(!r.compressed||!r.raw||!r.encoded||!r.block)return fail(report,"psram_unavailable");
  jpeg_dec_config_t jpegConfig={};jpegConfig.output_type=JPEG_PIXEL_FORMAT_CbYCrY;
  jpegConfig.block_enable=true;
  const auto jpegStatus=jpeg_dec_open(&jpegConfig,&r.jpeg);
  if(jpegStatus!=JPEG_ERR_OK)return fail(report,jpegStatus==JPEG_ERR_NO_MEM?"psram_unavailable":"jpeg_init_failed");
  esp_h264_enc_cfg_sw_t videoConfig={};videoConfig.pic_type=ESP_H264_RAW_FMT_I420;
  videoConfig.res={avi.width,avi.height};videoConfig.fps=std::min<uint64_t>(255,std::max<uint64_t>(1,(uint64_t(avi.rate)+avi.scale-1)/avi.scale));
  videoConfig.gop=std::min(255u,unsigned(videoConfig.fps)*2);videoConfig.rc={pixels*4,20,36};
  const auto h264Status=esp_h264_enc_sw_new(&videoConfig,&r.h264);
  if(h264Status==ESP_H264_ERR_MEM)return fail(report,"psram_unavailable");
  if(h264Status!=ESP_H264_ERR_OK||esp_h264_enc_open(r.h264)!=ESP_H264_ERR_OK)return codecFail(report,"h264_init_failed");
  mp4_muxer_config_t muxConfig={};muxConfig.base_config.muxer_type=ESP_MUXER_TYPE_MP4;
  const uint64_t duration=std::max(uint64_t(clock.milliseconds),uint64_t(avi.audioBytes/2)*1000/16000);
  if(duration>UINT32_MAX-1000)return fail(report,"unsupported_duration");
  muxConfig.base_config.slice_duration=uint32_t(duration+1000);
  muxConfig.base_config.url_pattern_ex=filePattern;muxConfig.base_config.ram_cache_size=4096;
  muxConfig.display_in_order=true;muxConfig.moov_before_mdat=true;
  if(mp4_muxer_register()!=ESP_MUXER_ERR_OK)return fail(report,"mux_register_failed");
  r.mux=esp_muxer_open(&muxConfig.base_config,sizeof(muxConfig));
  esp_muxer_file_writer_t callbacks={openWriter,writeWriter,seekWriter,closeWriter};
  if(!r.mux||esp_muxer_set_file_writer(r.mux,&callbacks)!=ESP_MUXER_ERR_OK)return codecFail(report,"mux_init_failed");
  int videoTrack=-1,audioTrack=-1;uint8_t asc[]={0x14,0x08};
  if(avi.audioBytes) {
    esp_muxer_audio_stream_info_t audio={};audio.codec=ESP_MUXER_ADEC_AAC;audio.channel=1;audio.bits_per_sample=16;
    audio.sample_rate=16000;audio.min_packet_duration=64;audio.codec_spec_info=asc;audio.spec_info_len=2;
    if(esp_muxer_add_audio_stream(r.mux,&audio,&audioTrack)!=ESP_MUXER_ERR_OK)return codecFail(report,"audio_track_failed");
  }
  uint32_t pos=310;
  for(uint32_t frame=0;frame<avi.frames;++frame) {
    if(abort&&abort())return fail(report,"aborted");
    if(!applianceStorageSpaceAvailable())return fail(report,"insufficient_space");
    uint8_t chunk[8];if(!readAt(input,pos,chunk,8)||memcmp(chunk,"00dc",4))return fail(report,"frame_read_failed");
    const uint32_t bytes=le32(chunk+4);if(bytes>avi.maxJpeg||!readAt(input,pos+8,r.compressed,bytes))return fail(report,"frame_read_failed");
    // The AVI writer includes up to three padding bytes in the chunk's JPEG length.
    uint32_t actual=bytes;while(actual>bytes-std::min<uint32_t>(3,bytes)&&!(r.compressed[actual-2]==255&&r.compressed[actual-1]==217))--actual;
    if(actual<4||r.compressed[actual-2]!=255||r.compressed[actual-1]!=217)return fail(report,"invalid_jpeg_end");
    jpeg_dec_io_t io={};io.inbuf=r.compressed;io.inbuf_len=actual;io.outbuf=r.block;
    jpeg_dec_header_info_t dimensions={};int decodedBytes=0,processCount=0;
    if(jpeg_dec_parse_header(r.jpeg,&io,&dimensions)!=JPEG_ERR_OK||dimensions.width!=avi.width||dimensions.height!=avi.height
      ||jpeg_dec_get_outbuf_len(r.jpeg,&decodedBytes)!=JPEG_ERR_OK||decodedBytes<=0||decodedBytes>int(blockCapacity)
      ||jpeg_dec_get_process_count(r.jpeg,&processCount)!=JPEG_ERR_OK||processCount<=0||processCount>avi.height)
      return codecFail(report,"jpeg_decode_failed");
    uint16_t row=0;
    for(int part=0;part<processCount;++part){
      if(abort&&abort())return fail(report,"aborted");
      if(jpeg_dec_process(r.jpeg,&io)!=JPEG_ERR_OK||io.out_size<=0||io.out_size>int(blockCapacity)
        ||!appliance::codecBlockToI420(r.block,io.out_size,r.raw,avi.width,avi.height,row))return codecFail(report,"jpeg_decode_failed");
      row+=io.out_size/(avi.width*2);
    }
    if(row!=avi.height)return fail(report,"jpeg_decode_failed");
    const uint32_t pts=uint64_t(frame)*avi.scale*1000/avi.rate;
    esp_h264_enc_in_frame_t in={{r.raw,rawCapacity},pts};esp_h264_enc_out_frame_t out={};out.raw_data={r.encoded,outputCapacity};
    if(esp_h264_enc_process(r.h264,&in,&out)!=ESP_H264_ERR_OK||!out.length||out.length>outputCapacity)return codecFail(report,"h264_encode_failed");
    if(abort&&abort())return fail(report,"aborted");
    if(videoTrack<0) {
      // IDR output contains Annex-B SPS/PPS, which the muxer copies into avcC.
      esp_muxer_video_stream_info_t video={};video.codec=ESP_MUXER_VDEC_H264;video.width=avi.width;video.height=avi.height;
      video.fps=videoConfig.fps;video.min_packet_duration=std::max<uint32_t>(1,uint64_t(avi.scale)*1000/avi.rate);
      // The muxer borrows configuration until close; retain immutable SPS/PPS
      // independently of the packet buffer, which is reused and then freed.
      if(!parameterSets(r,out.length))return fail(report,"h264_configuration_failed");
      video.codec_spec_info=r.codecInfo;video.spec_info_len=r.codecInfoBytes;
      if(esp_muxer_add_video_stream(r.mux,&video,&videoTrack)!=ESP_MUXER_ERR_OK)return codecFail(report,"video_track_failed");
    }
    esp_muxer_video_packet_t packet={r.encoded,int(out.length),pts,pts,out.frame_type==ESP_H264_FRAME_TYPE_IDR};
    if(esp_muxer_add_video_packet(r.mux,videoTrack,&packet)!=ESP_MUXER_ERR_OK||r.writeFailed)return codecFail(report,"video_write_failed");
    sampleMemory();report.frames=frame+1;report.elapsedMs=millis()-started;
    const float temperature=readInternalTemp();if(isfinite(temperature))report.peakTemperature=std::max(report.peakTemperature,temperature);
    if(progress)progress(report);pos+=8+bytes+(bytes&1);vTaskDelay(1);
  }
  r.releaseVideo();
  if(avi.audioBytes) {
    esp_aac_enc_config_t cfg={16000,1,16,32000,false};
    const auto aacStatus=esp_aac_enc_open(&cfg,sizeof(cfg),&r.aac);
    if(aacStatus==ESP_AUDIO_ERR_MEM_LACK)return fail(report,"psram_unavailable");
    if(aacStatus!=ESP_AUDIO_ERR_OK)return codecFail(report,"aac_init_failed");
    int pcmSize=0,soundSize=0;
    if(esp_aac_enc_get_frame_size(r.aac,&pcmSize,&soundSize)!=ESP_AUDIO_ERR_OK||pcmSize<=0||pcmSize>16384||soundSize<=0||soundSize>16384)
      return fail(report,"aac_buffer_failed");
    r.pcm=buffer(pcmSize);r.sound=buffer(soundSize);if(!r.pcm||!r.sound)return fail(report,"psram_unavailable");
    for(uint32_t at=0;at<avi.audioBytes;at+=pcmSize) {
      if(abort&&abort())return fail(report,"aborted");
      if(!(at%uint32_t(pcmSize*32))&&!applianceStorageSpaceAvailable())return fail(report,"insufficient_space");
      const uint32_t bytes=std::min(uint32_t(pcmSize),avi.audioBytes-at);memset(r.pcm,0,pcmSize);
      if(!readAt(input,avi.audioAt+at,r.pcm,bytes))return fail(report,"audio_read_failed");
      esp_audio_enc_in_frame_t in={r.pcm,uint32_t(pcmSize)};esp_audio_enc_out_frame_t out={};out.buffer=r.sound;out.len=soundSize;
      if(esp_aac_enc_process(r.aac,&in,&out)!=ESP_AUDIO_ERR_OK||!out.encoded_bytes)return fail(report,"aac_encode_failed");
      esp_muxer_audio_packet_t packet={r.sound,int(out.encoded_bytes),uint32_t(uint64_t(at/2)*1000/16000)};
      if(esp_muxer_add_audio_packet(r.mux,audioTrack,&packet)!=ESP_MUXER_ERR_OK||r.writeFailed)return codecFail(report,"audio_write_failed");
      report.audioSamples+=bytes/2;report.elapsedMs=millis()-started;if(progress)progress(report);vTaskDelay(1);
    }
  }
  if(abort&&abort())return fail(report,"aborted");
  const auto result=esp_muxer_close(r.mux);r.mux=nullptr;sampleMemory();
  if(result!=ESP_MUXER_ERR_OK||r.writeFailed)return codecFail(report,"mux_finalize_failed");
  if(!applianceStorageSpaceAvailable())return fail(report,"insufficient_space");
  if(!normalizeAvcHeader(temporary)||!normalizeTimeline(temporary,report,abort))return fail(report,abort&&abort()?"aborted":"mux_configuration_failed");
  File completed=SD.open(temporary,FILE_READ);report.outputBytes=completed?completed.size():0;
  if(!completed||!fingerprint(completed,report.outputCrc,abort))return fail(report,abort&&abort()?"aborted":"output_read_failed");completed.close();
  report.elapsedMs=millis()-started;
  return report.outputBytes>128&&applianceValidateMp4(temporary,report,abort)?true:fail(report,abort&&abort()?"aborted":"mp4_validation_failed");
}
bool applianceConvertAvi(const char* source,const char* temporary,ApplianceCodecReport& report,
                         ApplianceCodecAbort abort,ApplianceCodecProgress progress){
  const uint32_t began=millis();
  codecAllocationFailed.store(false);rawBytes.store(0);blockBytes.store(0);
  minimumPsram.store(UINT32_MAX);minimumLargestBlock.store(UINT32_MAX);
  // The retained 3 MP camera buffers leave too little PSRAM for VGA OpenH264.
  // Only the camera owner tears them down/restores them. All codec resources
  // are released before restoring the configured sensor and audio pipeline.
  File input=SD.open(source,FILE_READ);uint8_t header[310];
  bool recognized=input&&readAt(input,0,header,sizeof(header))&&!memcmp(header,"RIFF",4)&&!memcmp(header+8,"AVI ",4)&&!memcmp(header+306,"movi",4);
  bool supported=recognized&&((le32(header+0xa8)==320&&le32(header+0xac)==240)||(le32(header+0xa8)==640&&le32(header+0xac)==480));input.close();
  if(recognized&&!supported){report={};report.width=le32(header+0xa8);report.height=le32(header+0xac);return fail(report,"unsupported_source_resolution");}
  if(!supported)return convertIdleAvi(source,temporary,report,abort,progress);
  if((abort&&abort())||!applianceCodecCameraSuspend()){report={};return fail(report,"aborted");}
  initialPsram.store(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));sampleMemory();
  const bool converted=convertIdleAvi(source,temporary,report,abort,progress);
  const bool resumed=applianceCodecCameraResume();report.elapsedMs=millis()-began;
  if(!resumed)return fail(report,"camera_resume_failed");return converted;
}
#if defined(XIAO_APPLIANCE_CODEC_TEST)
bool applianceCodecTestPad(const char* path,ApplianceCodecReport& report,ApplianceCodecAbort abort){
  // A valid ISO free box proves size is not a publication/deletion criterion.
  const uint64_t padding=report.sourceBytes>report.outputBytes?report.sourceBytes-report.outputBytes+4096:4096;
  if(padding>UINT32_MAX||report.outputBytes+padding>UINT32_MAX)return false;
  File file=SD.open(path,"r+");uint8_t bytes[1024]={};if(!file||!file.seek(file.size()))return false;
  const uint32_t size=padding;bytes[0]=size>>24;bytes[1]=size>>16;bytes[2]=size>>8;bytes[3]=size;memcpy(bytes+4,"free",4);
  if(file.write(bytes,8)!=8)return false;memset(bytes,0,sizeof(bytes));
  for(uint32_t at=8;at<size;){if(abort&&abort())return false;size_t n=std::min(size-at,uint32_t(sizeof(bytes)));
    if(file.write(bytes,n)!=n)return false;at+=n;vTaskDelay(1);}
  file.flush();file.close();file=SD.open(path,FILE_READ);report.outputBytes=file?file.size():0;
  return file&&fingerprint(file,report.outputCrc,abort)&&applianceValidateMp4(path,report,abort);
}
#endif

namespace {
struct Track {bool video=false,audio=false,codec=false,configuration=false;
  uint32_t samples=0,scale=0,sizes=0,commonSize=0,chunks=0,offsets=0,runs=0,mapping=0,timedSamples=0;
  uint32_t mdhd=0,tkhd=0,stts=0,timingEntries=0;uint8_t mdhdVersion=0;
  uint64_t ticks=0;uint16_t width=0,height=0;};
bool avcConfig(File& file,uint32_t start,uint32_t end){uint8_t data[8];if(end-start<15||!readAt(file,start,data,7)
  ||data[0]!=1||data[1]!=66||(data[4]&3)!=3)return false;unsigned sps=data[5]&31;uint32_t pos=start+6;
  if(!sps)return false;
  for(unsigned i=0;i<sps;++i){if(!readAt(file,pos,data,3))return false;uint16_t n=uint16_t(data[0])<<8|data[1];
    if(!n||pos+2+n>end||(data[2]&31)!=7)return false;pos+=2+n;}
  if(!readAt(file,pos++,data,1)||!data[0])return false;unsigned pps=data[0];
  for(unsigned i=0;i<pps;++i){if(!readAt(file,pos,data,3))return false;uint16_t n=uint16_t(data[0])<<8|data[1];
    if(!n||pos+2+n>end||(data[2]&31)!=8)return false;pos+=2+n;}return pos==end;
}
bool boxes(File& file,uint32_t start,uint32_t end,unsigned depth,Track* track,Track (&tracks)[2],unsigned& count,
           bool& ftyp,bool& moov,bool& mdat,ApplianceCodecAbort abort) {
  if(depth>8)return false;
  uint8_t header[40];uint32_t pos=start;unsigned scanned=0;
  while(pos<end) {
    if(abort&&abort())return false;
    if(++scanned>100000||!readAt(file,pos,header,8))return false;
    const uint32_t size=be32(header);if(size<8||uint64_t(pos)+size>end)return false;
    const char* type=reinterpret_cast<char*>(header+4);const uint32_t body=pos+8,limit=pos+size;
    if(!memcmp(type,"ftyp",4)){if(depth||size<16)return false;ftyp=true;}
    else if(!memcmp(type,"mdat",4)){if(depth||size<12)return false;mdat=true;}
    else if(!memcmp(type,"trak",4)){if(count>=2)return false;Track* next=&tracks[count++];
      if(!boxes(file,body,limit,depth+1,next,tracks,count,ftyp,moov,mdat,abort))return false;}
    else if(!memcmp(type,"moov",4)||!memcmp(type,"mdia",4)||!memcmp(type,"minf",4)||!memcmp(type,"stbl",4)) {
      if(!memcmp(type,"moov",4))moov=true;
      if(!boxes(file,body,limit,depth+1,track,tracks,count,ftyp,moov,mdat,abort))return false;
    } else if(track&&!memcmp(type,"hdlr",4)){if(size<24||!readAt(file,body+8,header,4))return false;
      track->video=!memcmp(header,"vide",4);track->audio=!memcmp(header,"soun",4);
    } else if(track&&!memcmp(type,"mdhd",4)){if(size<32||!readAt(file,body,header,24)||header[0]>1)return false;
      track->mdhdVersion=header[0];if(header[0]&&(size<44||!readAt(file,body,header,36)))return false;
      track->scale=be32(header+(header[0]?20:12));track->mdhd=body;
    } else if(track&&!memcmp(type,"tkhd",4)){if(size<32||!readAt(file,body,header,4)||header[0])return false;track->tkhd=body;
    } else if(track&&!memcmp(type,"stsz",4)){if(size<20||!readAt(file,body,header,12))return false;
      track->samples=be32(header+8);track->commonSize=be32(header+4);track->sizes=body+12;
      if(!track->commonSize&&uint64_t(track->samples)*4+20!=size)return false;
    } else if(track&&!memcmp(type,"stco",4)){if(size<16||!readAt(file,body+4,header,4))return false;
      track->chunks=be32(header);track->offsets=body+8;if(uint64_t(track->chunks)*4+16!=size)return false;
    } else if(track&&!memcmp(type,"stsc",4)){if(size<16||!readAt(file,body+4,header,4))return false;
      track->runs=be32(header);track->mapping=body+8;if(uint64_t(track->runs)*12+16!=size)return false;
    } else if(track&&!memcmp(type,"stts",4)){if(size<16||!readAt(file,body+4,header,4))return false;
      const uint32_t entries=be32(header);if(uint64_t(entries)*8+16!=size)return false;
      track->stts=body+8;track->timingEntries=entries;
      for(uint32_t i=0;i<entries;++i){if(!readAt(file,body+8+i*8,header,8)||!be32(header)||!be32(header+4))return false;
        if(!(i%64)&&abort&&abort())return false;
        track->ticks+=uint64_t(be32(header))*be32(header+4);track->timedSamples+=be32(header);}
    } else if(track&&!memcmp(type,"stsd",4)){if(size<24||!readAt(file,body,header,16)||be32(header+4)!=1)return false;
      const uint32_t entrySize=be32(header+8);if(entrySize+16!=size)return false;
      if(!memcmp(header+12,"avc1",4)){if(entrySize<86||!readAt(file,body+16+24,header,4))return false;
        track->width=uint16_t(header[0])<<8|header[1];track->height=uint16_t(header[2])<<8|header[3];track->codec=true;
        uint32_t at=body+8+86;while(at<limit){if(!readAt(file,at,header,8))return false;uint32_t n=be32(header);
          if(n<8||uint64_t(at)+n>limit)return false;if(!memcmp(header+4,"avcC",4))track->configuration=avcConfig(file,at+8,at+n);at+=n;}
      } else if(!memcmp(header+12,"mp4a",4)){if(entrySize<36||!readAt(file,body+32,header,12)||header[0]||header[1]!=1
        ||header[2]||header[3]!=16||be32(header+8)!=(16000UL<<16))return false;track->codec=true;
        uint32_t at=body+8+36;while(at<limit){if(!readAt(file,at,header,8))return false;uint32_t n=be32(header);
          if(n<8||uint64_t(at)+n>limit)return false;if(!memcmp(header+4,"esds",4)){
            uint8_t config[128];uint32_t length=n-8;if(length>sizeof(config)||!readAt(file,at+8,config,length))return false;
            for(uint32_t i=4;i+3<length;++i)if(config[i]==5){uint32_t p=i+1,bytes=0;unsigned count=0;
              do{if(p>=length||++count>4)return false;bytes=(bytes<<7)|(config[p]&127);}while(config[p++]&128);
              if(bytes==2&&p+2<=length&&config[p]==0x14&&config[p+1]==8)track->configuration=true;}}
          at+=n;}
      }else return false;
    }
    pos=limit;if(!(scanned%64))vTaskDelay(1);
  }
  return pos==end;
}
uint64_t be64(const uint8_t* p){return uint64_t(be32(p))<<32|be32(p+4);}
bool write64(File& file,uint32_t at,uint64_t value){return write32(file,at,uint32_t(value>>32))&&write32(file,at+4,uint32_t(value));}
bool appendBytes(File& file,uint32_t& out,const uint8_t* bytes,size_t count){
  if(uint64_t(out)+count>UINT32_MAX||!file.seek(out)||file.write(bytes,count)!=count)return false;out+=count;return true;}
bool appendWord(File& file,uint32_t& out,uint32_t value){uint8_t bytes[]={uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};
  return appendBytes(file,out,bytes,sizeof(bytes));}
bool copyTimedBoxes(File& file,uint32_t start,uint32_t end,uint32_t& out,unsigned depth,const Track (&tracks)[2],
                    unsigned count,const appliance::CodecClock& clock,uint32_t created,ApplianceCodecAbort abort){
  if(depth>8)return false;uint8_t bytes[1024];
  for(uint32_t at=start;at<end;){
    if(abort&&abort())return false;if(!readAt(file,at,bytes,8))return false;
    uint32_t size=be32(bytes);if(size<8||uint64_t(at)+size>end)return false;
    uint32_t body=at+8,limit=at+size,growth=0;const Track* upgrade=nullptr;
    for(unsigned i=0;i<count;++i)if(tracks[i].video&&!tracks[i].mdhdVersion&&clock.ticks>UINT32_MAX
      &&tracks[i].mdhd>=body&&tracks[i].mdhd<limit){growth+=12;if(tracks[i].mdhd==body)upgrade=&tracks[i];}
    bool container=!memcmp(bytes+4,"moov",4)||!memcmp(bytes+4,"trak",4)||!memcmp(bytes+4,"mdia",4)
      ||!memcmp(bytes+4,"minf",4)||!memcmp(bytes+4,"stbl",4);
    if(!appendWord(file,out,size+growth)||!appendBytes(file,out,bytes+4,4))return false;
    if(upgrade){
      // Append a version-1 mdhd. Its 64-bit duration preserves coprime AVI
      // clocks exactly; no frame dropping or rounded timestamp conversion.
      if(size!=32||!readAt(file,body,bytes,24))return false;bytes[0]=1;
      if(!appendBytes(file,out,bytes,4)||!appendWord(file,out,0)||!appendWord(file,out,created)
        ||!appendWord(file,out,0)||!appendWord(file,out,created)||!appendWord(file,out,clock.rate)
        ||!appendWord(file,out,uint32_t(clock.ticks>>32))||!appendWord(file,out,uint32_t(clock.ticks))
        ||!appendBytes(file,out,bytes+20,4))return false;
    }else if(container){if(!copyTimedBoxes(file,body,limit,out,depth+1,tracks,count,clock,created,abort))return false;
    }else for(uint32_t pos=body;pos<limit;){size_t n=std::min(uint32_t(sizeof(bytes)),limit-pos);
      if((abort&&abort())||!readAt(file,pos,bytes,n)||!appendBytes(file,out,bytes,n))return false;pos+=n;vTaskDelay(1);}
    at=limit;
  }return true;
}
bool normalizeTimeline(const char* path,const ApplianceCodecReport& expected,ApplianceCodecAbort abort){
  File file=SD.open(path,"r+");if(!file)return false;
  Track tracks[2];unsigned count=0;bool ftyp=false,moov=false,mdat=false;
  if(!boxes(file,0,file.size(),0,nullptr,tracks,count,ftyp,moov,mdat,abort))return false;
  appliance::CodecClock clock;if(!appliance::codecClock(expected.totalFrames,expected.timeScale,expected.timeRate,clock))return false;
  uint32_t movie=0,movieScale=0,moovAt=0,moovEnd=0;uint8_t header[24];
  // The pinned muxer quantizes packet timestamps to milliseconds, then writes
  // a rounded average rate. Restore the AVI rational clock exactly; AAC uses
  // its actual 1024-sample access units. This prevents accumulated time drift.
  for(uint32_t at=0;at<file.size();){if(!readAt(file,at,header,8))return false;uint32_t size=be32(header);
    if(!memcmp(header+4,"moov",4)){moovAt=at;moovEnd=at+size;for(uint32_t child=at+8;child<at+size;){if(!readAt(file,child,header,8))return false;
      uint32_t bytes=be32(header);if(bytes<8||uint64_t(child)+bytes>at+size)return false;
      if(!memcmp(header+4,"mvhd",4)){movie=child+8;if(!readAt(file,movie,header,24)||header[0])return false;movieScale=be32(header+12);}child+=bytes;}}
    if(size<8||uint64_t(at)+size>file.size())return false;at+=size;}
  if(!movie||!movieScale)return false;uint64_t movieDuration=0;
  const uint64_t epoch=timeSynchronized?uint64_t(time(nullptr))+2082844800ULL:0;
  const uint32_t created=epoch<=UINT32_MAX?uint32_t(epoch):0;
  for(unsigned i=0;i<count;++i){const auto& t=tracks[i];
    if(!t.mdhd||!t.tkhd||!t.stts||!t.timingEntries||t.samples!=t.timedSamples||!t.samples)return false;
    uint32_t delta=t.video?clock.scale:1024,scale=t.video?clock.rate:16000;
    if(!scale||!delta||(!t.video&&!t.audio)||(t.video&&t.samples!=expected.totalFrames))return false;
    const uint64_t ticks=uint64_t(t.samples)*delta;
    const uint64_t duration=(ticks/scale)*movieScale+((ticks%scale)*movieScale+scale-1)/scale;
    if(duration>UINT32_MAX||(!t.video&&ticks>UINT32_MAX))return false;
    for(uint32_t entry=0;entry<t.timingEntries;++entry){if(abort&&abort())return false;
      if(!write32(file,t.stts+entry*8+4,delta))return false;}
    if(t.mdhdVersion){if(!write32(file,t.mdhd+20,scale)||!write64(file,t.mdhd+24,ticks)
      ||!write64(file,t.mdhd+4,created)||!write64(file,t.mdhd+12,created))return false;
    }else if(ticks<=UINT32_MAX){if(!write32(file,t.mdhd+12,scale)||!write32(file,t.mdhd+16,uint32_t(ticks))
      ||!write32(file,t.mdhd+4,created)||!write32(file,t.mdhd+8,created))return false;}
    if(!write32(file,t.tkhd+20,uint32_t(duration))||!write32(file,t.tkhd+4,created)||!write32(file,t.tkhd+8,created))return false;
    movieDuration=std::max(movieDuration,duration);
  }
  bool ok=write32(file,movie+16,uint32_t(movieDuration))&&write32(file,movie+4,created)&&write32(file,movie+8,created);
  bool upgrade=false;for(unsigned i=0;i<count;++i)upgrade|=tracks[i].video&&!tracks[i].mdhdVersion&&clock.ticks>UINT32_MAX;
  if(ok&&upgrade){
    // Relocate metadata only, leaving all encoded media and stco offsets in
    // place. The obsolete moov becomes free only after its replacement closes.
    uint32_t out=file.size();ok=uint64_t(out)+moovEnd-moovAt+12<=UINT32_MAX
      &&copyTimedBoxes(file,moovAt,moovEnd,out,0,tracks,count,clock,created,abort);
    if(ok){file.flush();const uint8_t freeType[]={'f','r','e','e'};ok=file.seek(moovAt+4)&&file.write(freeType,4)==4;}
  }
  file.flush();file.close();return ok;
}
bool samples(File& file,const Track& t,uint32_t dataStart,uint32_t dataEnd,ApplianceCodecAbort abort){
  if(!t.chunks||!t.runs||t.runs>t.chunks||t.timedSamples!=t.samples)return false;
  uint8_t data[12];if(!readAt(file,t.mapping,data,12)||be32(data)!=1)return false;
  uint32_t run=0,perChunk=be32(data+4),next=UINT32_MAX,sample=0;
  if(!perChunk||be32(data+8)!=1)return false;
  if(t.runs>1){if(!readAt(file,t.mapping+12,data,4))return false;next=be32(data);if(next<=1)return false;}
  for(uint32_t chunk=1;chunk<=t.chunks;++chunk){
    if(abort&&abort())return false;
    if(chunk==next){++run;if(!readAt(file,t.mapping+run*12,data,12))return false;perChunk=be32(data+4);
      if(!perChunk||be32(data+8)!=1)return false;next=UINT32_MAX;
      if(run+1<t.runs){if(!readAt(file,t.mapping+(run+1)*12,data,4))return false;next=be32(data);if(next<=chunk)return false;}}
    if(!readAt(file,t.offsets+(chunk-1)*4,data,4))return false;uint32_t at=be32(data);
    for(uint32_t i=0;i<perChunk;++i){if(sample>=t.samples)return false;uint32_t size=t.commonSize;
      if(!size){if(!readAt(file,t.sizes+sample*4,data,4))return false;size=be32(data);}++sample;
      if(!size||at<dataStart||uint64_t(at)+size>dataEnd)return false;
      if(t.video){uint32_t pos=at;bool vcl=false;while(pos<at+size){if(at+size-pos<5||!readAt(file,pos,data,5))return false;
          uint32_t n=be32(data);if(!n||uint64_t(pos)+4+n>at+size)return false;unsigned type=data[4]&31;
          if(type==1||type==5)vcl=true;pos+=4+n;}if(!vcl)return false;}
      at+=size;
    }if(!(chunk%16))vTaskDelay(1);
  }return sample==t.samples&&run+1==t.runs;
}
}
bool applianceValidateMp4(const char* path,const ApplianceCodecReport& expected,ApplianceCodecAbort abort) {
  File file=SD.open(path,FILE_READ);if(!file||file.size()<128||file.size()>UINT32_MAX)return false;
  uint32_t checked=0;if(file.size()!=expected.outputBytes||!fingerprint(file,checked,abort)||checked!=expected.outputCrc)return false;
  Track tracks[2];unsigned count=0;bool ftyp=false,moov=false,mdat=false;
  if(!boxes(file,0,file.size(),0,nullptr,tracks,count,ftyp,moov,mdat,abort)||!ftyp||!moov||!mdat)return false;
  uint32_t dataStart=0,dataEnd=0,pos=0;uint8_t header[8];
  while(pos<file.size()){if(!readAt(file,pos,header,8))return false;uint32_t size=be32(header);if(!size)return false;
    if(!memcmp(header+4,"mdat",4)){if(dataStart)return false;dataStart=pos+8;dataEnd=pos+size;}pos+=size;}
  appliance::CodecClock clock;if(!appliance::codecClock(expected.totalFrames,expected.timeScale,expected.timeRate,clock))return false;
  unsigned videos=0,audios=0;
  for(unsigned i=0;i<count;++i){const auto& t=tracks[i];if(!t.codec||!t.configuration||!t.scale||!t.ticks||!t.samples||!samples(file,t,dataStart,dataEnd,abort))return false;
    if(t.video){++videos;if(t.width!=expected.width||t.height!=expected.height||t.samples!=expected.totalFrames||!expected.timeRate)return false;
      uint8_t duration[8];uint64_t stored=0;
      if(!readAt(file,t.mdhd+(t.mdhdVersion?24:16),duration,t.mdhdVersion?8:4))return false;
      stored=t.mdhdVersion?be64(duration):be32(duration);
      bool canonical=t.scale==clock.rate&&t.ticks==clock.ticks;
      bool legacy=t.scale==expected.timeRate&&t.ticks==uint64_t(expected.totalFrames)*expected.timeScale;
      if((!canonical&&!legacy)||stored!=t.ticks)return false;}
    else if(t.audio){++audios;if(t.scale!=16000||std::llabs(int64_t(t.ticks)-expected.audioSamples)>2048)return false;}else return false;
  }
  return videos==1&&audios==(expected.audioSamples?1u:0u);
}
ApplianceCodecMemory applianceCodecMemory(){return {initialPsram.load(),minimumPsram.load(),minimumLargestBlock.load(),rawBytes.load(),blockBytes.load()};}
bool applianceCodecResourceUnavailable(const ApplianceCodecReport& report){return !strcmp(report.error,"psram_unavailable")||!strcmp(report.error,"internal_memory_unavailable");}
#endif
