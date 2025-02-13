#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <base64.hpp>
#include<UrlEncode.h>

// WiFi配置
const char* ssid = "201-1";
const char* password = "3Ljp@682";

//音频引脚设置
#define INMP441_WS 4
#define INMP441_SCK 5
#define INMP441_SD 6


#define MAX98357_LRC 18
#define MAX98357_BCLK 17
#define MAX98357_DIN 16

#define SAMPLE_RATE 16000

// 百度语音API配置
const char* api_key = "KzB41v7dQdyqs5pPCH6vkM6f";
const char* secret_key = "CRvrFtKGBRzE8quSx8HZ9MkHlc60HyVY";
String access_token = "";
String answer="你好我是交小科";

// 函数声明
String getAccessToken(); //获取百度的token
void baiduTTS_Send(String access_token, String text);
void playAudio(uint8_t* audioData, size_t audioDataSize); //播放音频
void clearAudio(void); //清空音频


i2s_config_t inmp441_i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_EDGE,
  .dma_buf_count = 8,   // buffer 的数量
  .dma_buf_len = 128    // buffer的大小，单位是i2s_bits_per_sample_t 采样位数，越小播放需要越及时时延越小，否则相反
};

const i2s_pin_config_t inmp441_gpio_config = {
  .bck_io_num = INMP441_SCK,
  .ws_io_num = INMP441_WS,
  .data_out_num = -1,
  .data_in_num = INMP441_SD
};

i2s_config_t max98357_i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 128
};

const i2s_pin_config_t max98357_gpio_config = {
  .bck_io_num = MAX98357_BCLK,
  .ws_io_num = MAX98357_LRC,
  .data_out_num = MAX98357_DIN,
  .data_in_num = -1
};

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status()!=WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connect Success");
  esp_err_t result=i2s_driver_install(I2S_NUM_1, &max98357_i2s_config, 0, NULL);
  if(result!=ESP_OK){
    Serial.println("音频输出初始化失败");
  }
  result=i2s_set_pin(I2S_NUM_1, &max98357_gpio_config);
  if(result!=ESP_OK){
    Serial.println("音频输出引脚初始化失败");
  }
  delay(500);
  access_token=getAccessToken();
  baiduTTS_Send(access_token, answer);
}

void loop() {
  
}


String getAccessToken() {
  HTTPClient http;
  String url = "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials"
               "&client_id=" + String(api_key) + 
               "&client_secret=" + String(secret_key);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    Serial.printf("HTTP GET access_token: %s\n", doc["access_token"].as<String>().c_str());
    return doc["access_token"].as<String>();
  }
  http.end();
  return "";
}


void baiduTTS_Send(String access_token, String text){
  if (access_token == "") {
    Serial.println("access_token is null");
    return;
  }
  if (text.length() == 0) {
    Serial.println("text is null");
    return;
  }
  const int per=0;
  const int spd=5;
  const int pit=5;
  const int vol=5;
  const int aue=6;

  const char* header[] = { "Content-Type", "Content-Length" };
  String encodedText = urlEncode(urlEncode(text)); // 对文本进行两次URL编码
  String  url = "http://tsn.baidu.com/text2audio";

  url += "?tok=" + access_token;
  url += "&tex=" + encodedText;
  url += "&per=" + String(per);
  url += "&spd=" + String(spd);
  url += "&pit=" + String(pit);
  url += "&vol=" + String(vol);
  url += "&aue=" + String(aue);
  url += "&cuid=esp32s3";
  url += "&lan=zh";
  url += "&ctp=1";

  HTTPClient http;
  http.begin(url);
  http.collectHeaders(header, 2);
  int httpResponseCode = http.GET();
  if(httpResponseCode > 0) {
    if (httpResponseCode == HTTP_CODE_OK){
      String contentType = http.header("Content-Type");
      Serial.println(contentType);
      if(contentType.startsWith("audio")){
        Serial.println("合成成功");
        // 获取返回的音频数据流
        WiFiClient* stream = http.getStreamPtr();
        int len = http.getSize(); // 获取音频数据流的长度
        uint8_t buffer[512];
        size_t bytesRead = 0;
        stream->setTimeout(200);
        while(http.connected() && (len > 0 || len == -1)){
          size_t size = stream->available(); 
          if(size){
            int c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
            playAudio(buffer, c);
            if (len > 0)
            {
              len -= c;
            }
          }
        }
        delay(200);
        clearAudio();
      }else if(contentType.equals("application/json")){
        Serial.println("合成出现错误");
        String response = http.getString(); 
        Serial.println(response);
      }else {
        Serial.println("未知的Content-Type: " + contentType);
    }
  }else {
    Serial.println("Failed to receive audio file");
  }
}else {
  Serial.print("Error code: ");
  Serial.println(httpResponseCode);
}
  http.end();
}

void playAudio(uint8_t* audioData, size_t audioDataSize){
  if (audioDataSize > 0){
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_1, (int16_t*)audioData, audioDataSize, &bytes_written, portMAX_DELAY);
  }
}

void clearAudio(void){
  i2s_zero_dma_buffer(I2S_NUM_1);
  Serial.print("clearAudio");
}
