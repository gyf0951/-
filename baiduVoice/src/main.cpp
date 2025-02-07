#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <base64.hpp>

// WiFi配置
const char* ssid = "jtkj";
const char* password = "3Ljp@682";

// 硬件引脚定义
#define I2S_WS 5
#define I2S_SCK 6
#define I2S_SD 7
#define I2S_Port I2S_NUM_0

// 音频参数配置
#define SAMPLE_RATE 16000     // 16kHz采样率
#define RECORD_TIME_SECONDS 10
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS )  
#define READ_CHUNK_SIZE 1024  // 每次读取的块大小

// 百度语音API配置
const char* api_key = "KzB41v7dQdyqs5pPCH6vkM6f";
const char* secret_key = "CRvrFtKGBRzE8quSx8HZ9MkHlc60HyVY";
String access_token = "";

// 函数声明
String getAccessToken();
String baiduSTT_Send(String token, uint8_t* audioData, int audioDataSize);
void i2s_install();
void i2s_pin_setup();

//开辟内存
static uint8_t* pcm_data = NULL;

void setup() {
  Serial.begin(115200);
  // WiFi连接
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connect Success" );

  // 获取百度Token
  access_token = getAccessToken();
  
  // I2S初始化
  i2s_install();
  i2s_pin_setup();

   // 内存分配
  if (!pcm_data) {
    pcm_data = (uint8_t*)ps_malloc(BUFFER_SIZE);
    if (!pcm_data) {
      Serial.println("Memory allocation failed!");
      delay(1000);
      return;
    }
    memset(pcm_data, 0, BUFFER_SIZE);
  }
}

void loop() {
  Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
  static size_t recordingSize = 0;
  memset(pcm_data, 0, BUFFER_SIZE);
 

  // 录音参数
  size_t bytes_read = 0;
  int16_t readBuffer[READ_CHUNK_SIZE];
  bool isRecording = true;
  unsigned long silenceStart = 0;
  const int silenceThreshold = 200;    // 静音检测阈值
  const unsigned long maxSilence = 1500; // 最大静音时间(ms)
  bool hasSpeech = false; //录音标志
  const unsigned long minRecord = 1000; //最小录音时间
  unsigned long recordStart = millis();

  // 开始录音
  Serial.println("Start recording...");
  while (isRecording && (recordingSize < BUFFER_SIZE - READ_CHUNK_SIZE*2)) {
    // 读取音频数据
    esp_err_t result = i2s_read(I2S_Port, readBuffer, sizeof(readBuffer), &bytes_read, portMAX_DELAY);
    if (result != ESP_OK) {
      Serial.printf("I2S Read Error: 0x%04x\n", result);
      continue;
    }


    // 计算音频能量
    uint32_t sum = 0;
    for (int i=0; i<bytes_read/2; i++) {
      sum += abs(readBuffer[i]);
    }
    uint16_t avgEnergy = sum / (bytes_read/2);

    // 静音检测逻辑
    if (avgEnergy < silenceThreshold) {
      if (silenceStart == 0) silenceStart = millis();
      if ((millis() - silenceStart > maxSilence) && (millis() - recordStart > minRecord) ) {
        Serial.println("Silence detected, stop recording");
        break;
      }
    } 
    else {
      silenceStart = 0;  // 重置静音计时
      hasSpeech = true;
    }

    // 存储数据
    memcpy(pcm_data + recordingSize, readBuffer, bytes_read);
    recordingSize += bytes_read;
  }

  // 语音识别处理
  if (recordingSize > 0 && hasSpeech) {
    Serial.println("Processing speech recognition...");
    String result = baiduSTT_Send(access_token, pcm_data, recordingSize);
    Serial.println("Recognition Result: " + result);
  }

  // 重置参数
/*   free(pcm_data);
  pcm_data = NULL; */
  recordingSize = 0;
  delay(2000);
}

// I2S配置函数
void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512
  };
  i2s_driver_install(I2S_Port, &i2s_config, 0, NULL);
}

void i2s_pin_setup() {
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  i2s_set_pin(I2S_Port, &pin_config);
}

// 百度API访问函数
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

String baiduSTT_Send(String access_token, uint8_t* audioData, int audioDataSize) {
  String recognizedText = "";

  if (access_token == "") {
    Serial.println("access_token is null");
    return recognizedText;
  }

  // audio数据包许愿哦进行Base64编码，数据量会增大1/3
  int audio_data_len = audioDataSize * sizeof(char) * 1.4;
  unsigned char* audioDataBase64 = (unsigned char*)ps_malloc(audio_data_len);
  if (!audioDataBase64) {
    Serial.println("Failed to allocate memory for audioDataBase64");
    return recognizedText;
  }

  // json包大小，由于需要将audioData数据进行Base64的编码，数据量会增大1/3
  int data_json_len = audioDataSize * sizeof(char) * 1.4;
  char* data_json = (char*)ps_malloc(data_json_len);
  if (!data_json) {
    Serial.println("Failed to allocate memory for data_json");
    return recognizedText;
  }
  encode_base64(audioData, audioDataSize, audioDataBase64);
  

  memset(data_json, '\0', data_json_len);
  strcat(data_json, "{");
  strcat(data_json, "\"format\":\"pcm\",");
  strcat(data_json, "\"rate\":16000,");
  strcat(data_json, "\"dev_pid\":1537,");
  strcat(data_json, "\"channel\":1,");
  strcat(data_json, "\"cuid\":\"5772220015\",");
  strcat(data_json, "\"token\":\"");
  strcat(data_json, access_token.c_str());
  strcat(data_json, "\",");
  sprintf(data_json + strlen(data_json), "\"len\":%d,", audioDataSize);
  strcat(data_json, "\"speech\":\"");
  strcat(data_json, (const char*)audioDataBase64);
  strcat(data_json, "\"");
  strcat(data_json, "}");

  // 创建http请求
  HTTPClient http_client;

  http_client.begin("http://vop.baidu.com/server_api");
  http_client.addHeader("Content-Type", "application/json");
  int httpCode = http_client.POST(data_json);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      // 获取返回结果
      String response = http_client.getString();
      Serial.println(response);

      // 从json中解析对应的result
      DynamicJsonDocument responseDoc(2048);
      deserializeJson(responseDoc, response);
      if(responseDoc["err_no"]!=0){
        Serial.printf("API错误: %d - %s\n", 
        responseDoc["err_no"].as<int>(),
        responseDoc["err_msg"].as<String>().c_str());
      }
      recognizedText = responseDoc["result"][0].as<String>();
    }
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http_client.errorToString(httpCode).c_str());
  }

  // 释放内存
  if (audioDataBase64) {
    free(audioDataBase64);
  }

  if (data_json) {
    free(data_json);
  }

  http_client.end();

  return recognizedText;
}
