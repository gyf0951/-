#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

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
#define RECORD_TIME_SECONDS 15
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS * sizeof(int16_t))  // 修正缓冲区计算
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


void formatSPIFFS() {
  Serial.println("正在格式化 SPIFFS...");
  if (SPIFFS.format()) {
    Serial.println("格式化成功");
  } else {
    Serial.println("格式化失败");
  }
}

void setup() {
  Serial.begin(115200);
  /* formatSPIFFS();
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS初始化失败");
    ESP.restart();
  } */
  Serial.println("SPIFFS 挂载成功");
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
}

void loop() {
  Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
  static uint8_t* pcm_data = NULL;
  static size_t recordingSize = 0;
  
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

  // 录音参数
  size_t bytes_read = 0;
  int16_t readBuffer[READ_CHUNK_SIZE];
  bool isRecording = true;
  unsigned long silenceStart = 0;
  const int silenceThreshold = 15;    // 静音检测阈值
  const unsigned long maxSilence = 1500; // 最大静音时间(ms)

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
      if (millis() - silenceStart > maxSilence) {
        Serial.println("Silence detected, stop recording");
        isRecording = false;
      }
    } else {
      silenceStart = 0;  // 重置静音计时
    }

    // 存储数据
    memcpy(pcm_data + recordingSize, readBuffer, bytes_read);
    recordingSize += bytes_read;
    //Serial.printf("Recording: %d bytes (Energy: %u)\n", recordingSize, avgEnergy);
  }

  // 语音识别处理
  if (recordingSize > 0) {
    Serial.println("Processing speech recognition...");
    String result = baiduSTT_Send(access_token, pcm_data, recordingSize);
    Serial.println("Recognition Result: " + result);
  }

  // 重置参数
  free(pcm_data);
  pcm_data = NULL;
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

String baiduSTT_Send(String token, uint8_t* audioData, int audioDataSize) {
  if (token.length() < 30) { // 有效性检查
    Serial.println("Invalid Token: " + token);
    return "";
  }

  // Base64编码
  String audioBase64 = base64::encode(audioData, audioDataSize);
  
  //保存文件
  /* File file = SPIFFS.open("./audio_base64.txt", FILE_WRITE);
  if(!file){
    Serial.println("文件创建失败");
  } else {
    file.print(audioBase64);
    file.close();
    Serial.println("Base64已保存到/audio_base64.txt");
  } */

  // 内存监控
  Serial.printf("[Memory] Pre-JSON: %d bytes free\n", ESP.getFreeHeap());

  
  const size_t requiredCapacity = JSON_OBJECT_SIZE(7) 
                                + audioBase64.length() + 300;
  DynamicJsonDocument doc(requiredCapacity + 512);

  doc["format"] = "pcm";
  doc["rate"] = 16000;
  doc["dev_pid"] = 1537;
  doc["channel"] = 1;
  doc["cuid"] = "ESP32_V1.2";
  doc["token"] = token;
  doc["len"] = audioDataSize;
  doc["speech"] = audioBase64;

  // 序列化验证
  String requestBody;
  if (serializeJson(doc, requestBody) == 0) {
    Serial.println("Failed to serialize JSON");
    return "";
  }
  
  //验证json
  if (!doc.containsKey("format") || !doc.containsKey("speech")) {
  Serial.println("JSON structure is invalid!");
  return "";
  }
  Serial.println("JSON Structure:");
  Serial.println("{\"format\":\"" + doc["format"].as<String>() + "\","
               "\"rate\":" + doc["rate"].as<String>() + ","
               "\"dev_pid\":" + doc["dev_pid"].as<String>() + ","
               "\"channel\":" + doc["channel"].as<String>() + ","
               "\"cuid\":\"" + doc["cuid"].as<String>() + "\","
               "\"token\":\"" + doc["token"].as<String>() + "\","
               "\"len\":" + doc["len"].as<String>() + "}");


  // 关键日志输出
  Serial.printf("Token Length: %d\n", token.length());
  Serial.printf("Full Token: %s\n", token.c_str());
  Serial.printf("JSON Size: %d bytes\n", requestBody.length());
  Serial.printf("[Memory] Post-JSON: %d bytes free\n", ESP.getFreeHeap());

  // HTTP请求
  HTTPClient http;
  http.begin("https://vop.baidu.com/server_api");
  http.addHeader("Content-Type", "application/json");
  
  // 性能监控
  unsigned long startTime = millis();
  int httpCode = http.POST(requestBody);
  Serial.printf("HTTP POST耗时: %d ms\n", millis() - startTime);

  // 响应处理
  String result = "";
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.printf("原始响应: %s\n", response.c_str());
    
    DynamicJsonDocument resDoc(2048);
    if(deserializeJson(resDoc, response)){
      Serial.println("JSON解析失败");
    } else if(resDoc["err_no"] != 0){
      Serial.printf("API错误: %d - %s\n", 
                   resDoc["err_no"].as<int>(),
                   resDoc["err_msg"].as<String>().c_str());
    } else {
      result = resDoc["result"][0].as<String>();
    }
  } else {
    Serial.printf("HTTP错误代码: %d\n", httpCode);
    Serial.printf("错误详情: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  return result;
}
