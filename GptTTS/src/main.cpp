#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <base64.hpp>
#include<UrlEncode.h>

// WiFi配置
const char* ssid = "CMCC-9iMc";
const char* password = "px2qyej7";

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

// 音频参数配置
#define SAMPLE_RATE 16000     // 16kHz采样率
#define RECORD_TIME_SECONDS 10
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS )  
#define READ_CHUNK_SIZE 1024  // 每次读取的块大小

//大模型api
const char *api="sk-df45f0d3cd5b429c8b2e6359bd311539";
String gptUrl="https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
String inputText="";
String answer="";

// 函数声明
String getAccessToken(); //获取百度的token
String getAnswer(String inputText); //获取大模型的回答
void baiduTTS_Send(String access_token, String text); //百度语音合成
String baiduSTT_Send(String token, uint8_t* audioData, int audioDataSize); //百度语音识别
void playAudio(uint8_t* audioData, size_t audioDataSize); //播放音频
void clearAudio(void); //清空音频


//开辟内存
static uint8_t* pcm_data = NULL;

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

  // I2S初始化
  esp_err_t result= i2s_driver_install(I2S_NUM_0, &inmp441_i2s_config, 0, NULL);
  if(result!=ESP_OK){
    Serial.println("音频输入初始化失败");
  }
  result=i2s_set_pin(I2S_NUM_0, &inmp441_gpio_config);
  if(result!=ESP_OK){
    Serial.println("音频输入引脚初始化失败");
  }
  result=i2s_driver_install(I2S_NUM_1, &max98357_i2s_config, 0, NULL);
  if(result!=ESP_OK){
    Serial.println("音频输出初始化失败");
  }
  result=i2s_set_pin(I2S_NUM_1, &max98357_gpio_config);
  if(result!=ESP_OK){
    Serial.println("音频输出引脚初始化失败");
  }
  delay(500);

  // 内存分配
  if (!pcm_data) {
    pcm_data = (uint8_t*)malloc(BUFFER_SIZE);
    if (!pcm_data) {
      Serial.println("Failed to allocate memory");
      delay(1000);
      return;
    }
    memset(pcm_data, 0, BUFFER_SIZE);
  }

  access_token=getAccessToken();
  Serial.println("Enter a prompt:");
  delay(2000);
}

void loop() {
  inputText="";
  Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
  memset(pcm_data, 0, BUFFER_SIZE);
  

  Serial.println("i2s_read");
  // 开始循环录音，将录制结果保存在pcm_data中
  size_t bytes_read = 0, recordingSize = 0, ttsSize = 0;
  int16_t data[512];
  size_t noVoicePre = 0, noVoiceCur = 0, noVoiceTotal = 0, VoiceCnt = 0;
  bool recording = true;


  while(1){
    // 记录刚开始的时间
    noVoicePre = millis();

    // i2s录音
    esp_err_t result = i2s_read(I2S_NUM_0, data, sizeof(data), &bytes_read, portMAX_DELAY);
    if (result != ESP_OK) {
      Serial.printf("I2S Read Error: 0x%04x\n", result);
      continue;
    }
    memcpy(pcm_data + recordingSize, data, bytes_read);
    recordingSize += bytes_read;
    //Serial.printf("%x recordingSize: %d bytes_read :%d\n", pcm_data + recordingSize, recordingSize, bytes_read);

    /* for (int i = 0; i < bytes_read / 2; i++) {
      Serial.println(data[i]);
    } */

    // 计算平均值
    uint32_t sum_data = 0;
    for (int i = 0; i < bytes_read / 2; i++) {
      sum_data += abs(data[i]);
    }
    sum_data = sum_data / bytes_read;
    //Serial.printf("sum_data :%d\n", sum_data);

    // 判断当没有说话时间超过一定时间时就退出录音
    noVoiceCur = millis();
    if (sum_data < 500) {
      noVoiceTotal += noVoiceCur - noVoicePre;
    } else {
      noVoiceTotal = 0;
      VoiceCnt += 1;
    }

    if (noVoiceTotal > 1500) {
      recording = false;
    }

    if (!recording || (recordingSize >= BUFFER_SIZE - bytes_read)) {
      Serial.printf("record done: %d", recordingSize);
      break;
    }
  }
  if (VoiceCnt == 0) recordingSize = 0;

  if (recordingSize > 0){
    // 语音识别处理
    Serial.println("Processing speech recognition...");
    String recognizedText = baiduSTT_Send(access_token, pcm_data, recordingSize);
    Serial.println("Recognition Result: " + recognizedText);
    inputText=recognizedText;
    if(inputText==""){
      Serial.println("No voice input");
    }
    //大模型
    else{
      answer=getAnswer(inputText);
      Serial.println("Answer: " + answer);
      baiduTTS_Send(access_token, answer); //百度语音合成
      Serial.println("Enter a prompt:");
    }
  }
  delay(500);
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

String getAnswer(String inputText){
  HTTPClient http;
  http.setTimeout(20000);
  http.begin(gptUrl);
  http.addHeader("Content-Type", "application/json");
  String token_key = String("Bearer ") + api;
  http.addHeader("Authorization", token_key);  
  String payload = "{\"model\":\"qwen-plus\",\"messages\":[{\"role\":\"system\",\"content\":\"你好,你必须用中文回答且字数不超过80字\"},{\"role\":\"user\",\"content\":\"" + inputText + "\"}],\"temperature\": 0.3}";
  Serial.println("Payload: " + payload);
  int http_code=http.POST(payload);
  if(http_code==200){
     String response = http.getString();
     http.end();

     //解析Json
     DynamicJsonDocument doc(1024);
     deserializeJson(doc, response);
     String outputText = doc["choices"][0]["message"]["content"].as<String>();
     return outputText;

  }
  else{
     http.end();
     Serial.printf("Error %d \n", http_code);
     return "error";
  }  
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
  const int spd=6;
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
