#include "Arduino.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Audio.h>
#include <driver/i2s.h>

#define MAX98357_LRC 16
#define MAX98357_BCLK 15
#define MAX98357_DIN 7

#define SAMPLE_RATE 16000

const char* ssid = "jtkj";           // Wi-Fi SSID
const char* password = "3Ljp@682";   // Wi-Fi密码
const char* url = "http://music.163.com/song/media/outer/url?id=447925558.mp3"; // 网易云MP3音频流

Audio audio; // 音频库对象

i2s_config_t i2sOut_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 1024
};

const i2s_pin_config_t i2sOut_pin_config = {
  .bck_io_num = MAX98357_BCLK,
  .ws_io_num = MAX98357_LRC,
  .data_out_num = MAX98357_DIN,
  .data_in_num = -1
};

void setup() {
  Serial.begin(115200);
  
  // 连接Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi Connected");

  // 安装I2S驱动并设置引脚
  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config); 

  // 设置音频输出
  audio.setPinout(MAX98357_BCLK, MAX98357_LRC, MAX98357_DIN);
  audio.setVolume(12);  // 设置音量

  // 尝试连接并播放音频流
  Serial.println("Starting audio stream");
  if (audio.connecttohost(url)) {
    Serial.println("Audio stream started");
  } else {
    Serial.println("Failed to connect to audio stream");
  }
}

void loop() {
  // 如果串口有数据，则停止当前音频并尝试连接新的音频流
  audio.loop();
  
  if (Serial.available()) { 
    String r = Serial.readString(); 
    r.trim();
    if (r.length() > 5) {
      audio.stopSong(); // 停止当前播放的音频
      if (audio.connecttohost(r.c_str())) { // 连接并播放新的音频流
        Serial.println("Audio stream started");
      } else {
        Serial.println("Failed to connect to new audio stream");
      }
    }
    log_i("Free heap: %i", ESP.getFreeHeap());
  }
}
