#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <ESP_I2S.h>
#include <MD5Builder.h>
#include <esp_camera.h>
#include <esp_random.h>
#include "sd_diskio.h"
#include "diskio.h"

#define CAMERA_MODEL_XIAO_ESP32S3
#include "../../../src/camera_pins.h"

static void sendFile(const char* name, const uint8_t* data, size_t size) {
  MD5Builder md5;
  md5.begin();
  md5.add(data, size);
  md5.calculate();
  Serial.printf("\nHWFILE %s %u %s\n", name, (unsigned)size, md5.toString().c_str());
  size_t sent = 0;
  uint32_t started = millis();
  while (sent < size && millis() - started < 30000) {
    sent += Serial.write(data + sent, std::min((size_t)4096, size - sent));
    delay(1);
  }
  Serial.printf("\nHWFILE_END %s %u\n", name, (unsigned)sent);
}

static void testSD() {
  SPI.begin(SD_SPI_SCK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
  bool mounted = SD.begin(SD_SPI_CS, SPI, 4000000, "/sdcard", 10, false);
  Serial.printf("HW SD mount=%d\n", mounted);
  if (!mounted) {
    uint8_t drive = sdcard_init(SD_SPI_CS, &SPI, 1000000);
    if (drive != 0xff) {
      DSTATUS status = disk_initialize(drive);
      Serial.printf("HW SD raw drive=%u status=%u type=%u\n", drive, status, sdcard_type(drive));
      uint8_t sector[512];
      if (!(status & STA_NOINIT) && sd_read_raw(drive, sector, 0)) {
        Serial.printf("HW SD sector0 signature=%02x%02x OEM=%.8s partition_type=%02x\n",
                      sector[510], sector[511], sector + 3, sector[450]);
        uint32_t lba = (uint32_t)sector[454] | ((uint32_t)sector[455] << 8) |
                       ((uint32_t)sector[456] << 16) | ((uint32_t)sector[457] << 24);
        if (lba && lba < 0x10000000 && sd_read_raw(drive, sector, lba))
          Serial.printf("HW SD volume lba=%lu OEM=%.8s fs_label=%.8s exfat=%d\n",
                        (unsigned long)lba, sector + 3, sector + 82,
                        !memcmp(sector + 3, "EXFAT   ", 8));
      }
      sdcard_uninit(drive);
    } else Serial.println("HW SD raw driver registration failed");
    return;
  }
  uint8_t bootSector[512];
  if (SD.readRAW(bootSector, 0)) {
    uint32_t lba = (uint32_t)bootSector[454] | ((uint32_t)bootSector[455] << 8) |
                   ((uint32_t)bootSector[456] << 16) | ((uint32_t)bootSector[457] << 24);
    if (lba < 0x10000000 && SD.readRAW(bootSector, lba))
      Serial.printf("HW SD mounted_volume lba=%lu signature=%02x%02x filesystem=%.8s\n",
                    (unsigned long)lba, bootSector[510], bootSector[511], bootSector + 82);
  }
  Serial.printf("HW SD card_type=%u bytes=%llu free=%llu\n", SD.cardType(), SD.cardSize(), SD.totalBytes() - SD.usedBytes());
  char path[48];
  snprintf(path, sizeof(path), "/codex-hw-%08lx.bin", (unsigned long)esp_random());
  if (SD.exists(path)) { Serial.println("HW SD test path collision; skipped"); return; }
  uint8_t block[512];
  for (unsigned i = 0; i < sizeof(block); ++i) block[i] = i & 255;
  File test = SD.open(path, FILE_WRITE);
  bool passed = (bool)test;
  uint32_t started = millis();
  for (unsigned i = 0; i < 512 && passed; ++i) passed = test.write(block, sizeof(block)) == sizeof(block);
  test.close();
  uint32_t writeMs = millis() - started;
  test = SD.open(path, FILE_READ);
  passed = passed && test && test.size() == 262144;
  uint8_t readBlock[512];
  for (unsigned i = 0; i < 512 && passed; ++i)
    passed = test.read(readBlock, sizeof(readBlock)) == sizeof(readBlock) && !memcmp(block, readBlock, sizeof(block));
  test.close();
  bool cleaned = SD.remove(path);
  Serial.printf("HW SD write_read=%d bytes=262144 write_ms=%lu test_file_removed=%d\n", passed, (unsigned long)writeMs, cleaned);
  SD.end();
}

static void testCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_1;
  config.ledc_timer = LEDC_TIMER_1;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  esp_err_t error = esp_camera_init(&config);
  Serial.printf("HW CAMERA init=%s xclk=%u\n", esp_err_to_name(error), (unsigned)config.xclk_freq_hz);
  if (error != ESP_OK) return;
  Serial.printf("HW CAMERA sensor_pid=0x%04x\n", esp_camera_sensor_get()->id.PID);
  for (unsigned i = 0; i < 3; ++i) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (!frame) { Serial.println("HW CAMERA frame failed"); break; }
    Serial.printf("HW CAMERA frame=%u width=%u height=%u bytes=%u jpeg=%d\n", i, frame->width, frame->height, (unsigned)frame->len,
                  frame->format == PIXFORMAT_JPEG && frame->len > 2 && frame->buf[0] == 0xff && frame->buf[1] == 0xd8);
    if (i == 2) sendFile("camera.jpg", frame->buf, frame->len);
    esp_camera_fb_return(frame);
    delay(200);
  }
  esp_camera_deinit();
}

static void testMic() {
  I2SClass microphone;
  microphone.setPinsPdmRx(I2S_WS, I2S_SD);
  bool ready = microphone.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  Serial.printf("HW MIC init=%d rate=16000 bits=16 channels=1\n", ready);
  if (!ready) return;
  size_t bytes = 0;
  uint8_t* wav = microphone.recordWAV(3, &bytes);
  Serial.printf("HW MIC wav_bytes=%u error=%d\n", (unsigned)bytes, microphone.lastError());
  if (wav && bytes) sendFile("microphone.wav", wav, bytes);
  free(wav);
  microphone.end();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.printf("\nHW BEGIN flash=%u psram=%u free_psram=%u reset=%u\n", ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreePsram(), esp_reset_reason());
  testSD();
  testCamera();
  testMic();
  Serial.printf("HW DONE heap=%u minimum_heap=%u temperature=%.1f\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), temperatureRead());
}

void loop() { delay(1000); }
