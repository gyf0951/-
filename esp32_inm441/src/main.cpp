#include <Arduino.h>
#include<driver/i2s.h>
#include<HTTPClient.h>
#include <WiFi.h>
#include<ArduinoJson.h>
#include<base64.h>

//定义引脚
#define I2S_WS 15
#define I2S_SCK 2
#define I2S_SD 13
//定义端口号
#define I2S_Port I2S_NUM_0
//定义数据缓存区
#define bufferlen 1024
int16_t sBuffer[bufferlen];

const int recordTimeSeconds = 3; //录音时间
const int adc_data_len = 16000 * recordTimeSeconds;
const int data_json_len = adc_data_len*2*1.4;
uint16_t *adc_data; //存储音频数据
char *data_json;  //存储上传的json数据

//静音处理
int silenceThreshold = 100; // 静音阈值
int silenceDuration = 3000; // 静音时间（毫秒）
long silenceStartTime = 0;
bool record_status=true;

//百度api
const char* api_key="KzB41v7dQdyqs5pPCH6vkM6f"; 
const char* secret_key="CRvrFtKGBRzE8quSx8HZ9MkHlc60HyVY"; 
String access_token = "";

//WiFi账号密码
const char * ssid = "jtkj";
const char * password = "3Ljp@682";

// put function declarations here:
//获取百度api的Token
String getAccessToken();
//百度语音识别API访问
String baiduSTT_Send(String access_token);
//静音检测
void checkSilence(int16_t *audioData, size_t length);

//驱动初始化
void i2s_install(){
  const i2s_config_t i2s_config={
    .mode=(i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),   // 选择主从模式
    .sample_rate=44100,  //采样率
    .bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT,  //每帧的采样
    .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,  //声道选择
    .communication_format=(i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S), //采样模式
    .intr_alloc_flags=0,  //设置中断
    .dma_buf_count=8,//dma数量
    .dma_buf_len=bufferlen, //缓冲区长度
  };
  i2s_driver_install(I2S_Port,&i2s_config,0,NULL);
}
//设置引脚
void i2s_pin(){
  const i2s_pin_config_t pin_config={
    .bck_io_num=I2S_SCK,
    .ws_io_num=I2S_WS,
    .data_out_num=I2S_PIN_NO_CHANGE, //不需要输出数据
    .data_in_num=I2S_SD,
  };
  i2s_set_pin(I2S_Port,&pin_config); 
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);
  Serial.printf("start");
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(300);
    Serial.print(".");
  }
  Serial.println("WiFi Connect Success");

  //分配PSRAM
  adc_data=(uint16_t*)ps_malloc(adc_data_len * sizeof(uint16_t));
  if (!adc_data) {
    Serial.println("Failed to allocate memory for adc_data");
    return;
  }
  data_json = (char *)ps_malloc(data_json_len * sizeof(char)); 
  if(!data_json){
    Serial.println("Failed to allocate memory for adc_data");
    return;
  }

  i2s_install();
  i2s_pin();
  //获取百度token
  access_token=getAccessToken();
  
}

void loop() {
  // 获取I2S的数据
  size_t bytes_in; //实际读取的数据
  esp_err_t result=i2s_read(I2S_Port,&sBuffer,bufferlen,&bytes_in,portMAX_DELAY); //portMAX_DELAY表示多久超时
  checkSilence(sBuffer, bytes_in / 2);
  
  if(record_status){
    memcpy(adc_data,sBuffer,bytes_in); 
    String result=baiduSTT_Send(access_token);
    Serial.println("Recognized text: " + result);
  }
  else {
    record_status=true;
  }
  

}

// put function definitions here:
String getAccessToken(){
  String access_token;
  HTTPClient http;
  String baidu_url=(String)("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=") + api_key + "&client_secret=" + secret_key;
  http.begin(baidu_url);
  int httpCode = http.GET();
  if (httpCode > 0){
    String response=http.getString();
    JsonDocument doc ;
    deserializeJson(doc,response);
    access_token=doc["access_token"].as<String>();
    Serial.printf("[HTTP] GET access_token: %s\n", access_token);
  }
  else{
    Serial.printf("[HTTP] GET failed");
  }
  http.end();
  return access_token;
}

String baiduSTT_Send(String access_token){
  String recognizedText = "";
  if(access_token=""){
    Serial.println("access_token is null");
    return recognizedText;
  }
  

  memset(data_json, '\0', data_json_len * sizeof(char));
  strcat(data_json, "{");
  strcat(data_json, "\"format\":\"pcm\",");
  strcat(data_json, "\"rate\":16000,");
  strcat(data_json, "\"dev_pid\":1537,");
  strcat(data_json, "\"channel\":1,");
  strcat(data_json, "\"cuid\":\"57722200\",");
  strcat(data_json, "\"token\":\"");
  strcat(data_json, access_token.c_str());
  strcat(data_json, "\",");
  sprintf(data_json + strlen(data_json), "\"len\":%d,", adc_data_len * 2);
  strcat(data_json, "\"speech\":\"");
  strcat(data_json, base64::encode((uint8_t *)adc_data, adc_data_len * sizeof(uint16_t)).c_str());
  strcat(data_json, "\"");
  strcat(data_json, "}");

  //创建请求
  HTTPClient http_client;
  http_client.begin("http://vop.baidu.com/server_api");
  http_client.addHeader("Content-Type", "application/json");
  int httpCode = http_client.POST(data_json);
  if(httpCode>0){
    if (httpCode == HTTP_CODE_OK) {
      String payload = http_client.getString();
      Serial.println(payload);
      JsonDocument responseDoc;
      deserializeJson(responseDoc,payload);
      recognizedText = responseDoc["result"].as<String>();
    }
  }
  else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http_client.errorToString(httpCode).c_str());
  }
  http_client.end();
  return  recognizedText;
}

void checkSilence(int16_t *audioData, size_t length){
  uint32_t sumEnergy = 0;
  for (size_t i = 0; i < length; i++) {
    sumEnergy += abs(audioData[i]);
  }
  uint32_t averageEnergy = sumEnergy / length;
  if (averageEnergy < silenceThreshold){
    if (silenceStartTime == 0) {
      silenceStartTime = millis(); 
    }
  } else {
    silenceStartTime = 0; 
  }
  if (silenceStartTime > 0 && (millis() - silenceStartTime > silenceDuration)) {
    Serial.println("Silence detected, stopping recording...");
  }
}