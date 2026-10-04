// Isolated provisioning of unchanged public upstream assets to the Sense SD.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <MD5Builder.h>
#include "esp_mac.h"

struct SeedAsset { const char* path; const uint8_t* data; size_t bytes; const char* md5; };
#include "seed_assets.h"

static String fileMD5(File& file) {
  MD5Builder md5;
  md5.begin();
  uint8_t block[1024];
  while (file.available()) {
    size_t bytes = file.read(block, sizeof(block));
    if (!bytes) break;
    md5.add(block, bytes);
  }
  md5.calculate();
  return md5.toString();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  const uint8_t expectedMac[] = {0xe0, 0x72, 0xa1, 0xfb, 0x4c, 0x08};
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != ESP_OK || memcmp(mac, expectedMac, 6)) {
    Serial.println("HW ASSETS_REFUSED unexpected board MAC");
    return;
  }
  SPI.begin(7, 8, 9, 21);
  if (!SD.begin(21, SPI, 4000000, "/sdcard", 10, false)) {
    Serial.println("HW ASSETS_REFUSED SD mount failed; no formatting");
    return;
  }
  if (!SD.exists("/data") && !SD.mkdir("/data")) return;
  bool passed = true;
  for (const auto& asset : assets) {
    bool existed = SD.exists(asset.path);
    if (!existed) {
      File file = SD.open(asset.path, FILE_WRITE);
      bool written = file && file.write(asset.data, asset.bytes) == asset.bytes;
      file.close();
      if (!written) {
        SD.remove(asset.path); // only a file created by this utility
        passed = false;
        Serial.printf("HW ASSET_FAILED %s write error\n", asset.path);
        continue;
      }
    }
    File file = SD.open(asset.path, FILE_READ);
    bool valid = file && file.size() == asset.bytes && fileMD5(file) == asset.md5;
    file.close();
    Serial.printf("HW ASSET %s bytes=%u md5=%s verified=%d existed=%d\n",
                  asset.path, (unsigned)asset.bytes, asset.md5, valid, existed);
    passed &= valid;
  }
  SD.end();
  Serial.printf("HW ASSETS_DONE success=%d\n", passed);
}

void loop() { delay(1000); }
