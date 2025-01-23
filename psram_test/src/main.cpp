#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <base64.h>
#include<ArduinoJson.h>

//定义引脚
#define I2S_WS 15
#define I2S_SCK 2
#define I2S_SD 13
//定义端口号
#define I2S_Port I2S_NUM_0
//定义数据缓存区


#define SAMPLE_RATE 16000
#define RECORD_TIME_SECONDS 3
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS)

#define bufferlen BUFFER_SIZE / sizeof(int16_t)
int16_t sBuffer[bufferlen];

#define SILENCE_THRESHOLD 500 //静音阈值
//WiFi账号密码
const char * ssid = "jtkj";
const char * password = "3Ljp@682";

//百度api配置
const char* api_key="KzB41v7dQdyqs5pPCH6vkM6f"; 
const char* secret_key="CRvrFtKGBRzE8quSx8HZ9MkHlc60HyVY"; 
String access_token = "";

// put function declarations here:
//获取百度api的Token
String getAccessToken();
String sendAudioToBaidu(String encoded_audio, size_t audio_length); //发送音频数据

//音频初始化
void i2s_install();
void i2s_pin();

bool isSilent(int16_t* audio_data, size_t length);

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid,password);
  delay(1000);
  Serial.print("Connecting to WiFi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(300);
    Serial.print(".");
  } 
  Serial.print('\n');
  Serial.println("WiFi Connect Success");

  //获取百度api的token
  access_token=getAccessToken();
  //音频初始化
  i2s_install();
  i2s_pin();
  
  
}

void loop() {
  // put your main code here, to run repeatedly:
  int recognizeSize=0;
  int noVoicePre = 0, noVoiceCur = 0, noVoiceTotal = 0, VoiceCnt = 0;
  bool recording = true;
  size_t bytes_read;
  while(recognizeSize<BUFFER_SIZE){
  noVoicePre = millis();
  esp_err_t result=i2s_read(I2S_Port,&sBuffer,bufferlen,&bytes_read,portMAX_DELAY);
  recognizeSize+=bytes_read;
  
  //计算音量平均值
  uint32_t sum_data = 0;
  for (int i = 0; i < bytes_read / 2; i++) {
          sum_data += abs(sBuffer[i]);
  }
  sum_data = sum_data / bytes_read;
  // 判断当没有说话时间超过一定时间时就退出录音
  noVoiceCur = millis();
  if (sum_data < 15) {
          noVoiceTotal += noVoiceCur - noVoicePre;
  } else {
          noVoiceTotal = 0;
          VoiceCnt += 1;
        }
  if (noVoiceTotal > 3000) {
          recording = false;
  }
  if (!recording || (recognizeSize >= BUFFER_SIZE - bytes_read)) {
          Serial.printf("record done: %d", recognizeSize);
}
  break;
   if (isSilent(sBuffer, bytes_read / sizeof(int16_t))){
    Serial.println("Detected silence....");
    delay(2000);
  }
  else {
    Serial.print("context:");
    String encoded_audio = base64::encode((uint8_t*)sBuffer, bytes_read);
    String recognition_result=sendAudioToBaidu(encoded_audio,recognizeSize);
    Serial.printf("%s\n",recognition_result.c_str());
  } 
  
 }
  if (VoiceCnt == 0) {
    recognizeSize = 0;
    
  }
  if (recognizeSize > 0){
    Serial.print("context:");
    String encoded_audio = base64::encode((uint8_t*)sBuffer, recognizeSize);
     String recognition_result=sendAudioToBaidu(encoded_audio,recognizeSize);
    Serial.printf("%s\n",recognition_result.c_str());
  }
}

// put function definitions here:

void i2s_install(){
  const i2s_config_t i2s_config={
    .mode=(i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),   // 选择主从模式
    .sample_rate=16000,  //采样率
    .bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT,  //每帧的采样
    .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,  //声道选择
    .communication_format=(i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S), //采样模式
    .intr_alloc_flags=0,  //设置中断
    .dma_buf_count=8,//dma数量
    .dma_buf_len=bufferlen, //缓冲区长度
  };
  i2s_driver_install(I2S_Port,&i2s_config,0,NULL);
}
void i2s_pin(){
  const i2s_pin_config_t pin_config={
    .bck_io_num=I2S_SCK,
    .ws_io_num=I2S_WS,
    .data_out_num=I2S_PIN_NO_CHANGE, //不需要输出数据
    .data_in_num=I2S_SD,
  };
  i2s_set_pin(I2S_Port,&pin_config); 
}


String getAccessToken(){
  String token="";
  HTTPClient http;
  String baidu_url=(String)("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=") + String(api_key) + "&client_secret=" + String(secret_key);
  http.begin(baidu_url);
  int httpCode = http.POST(" ");
  if (httpCode > 0){
    String response=http.getString();
    Serial.println("HTTP Response:");
    Serial.println(response);
    DynamicJsonDocument doc(4096) ;
    DeserializationError error=deserializeJson(doc,response);
    if(!error){
      token=doc["access_token"].as<String>();
      Serial.printf("HTTP GET access_token: %s\n", token.c_str());
    }else {
      Serial.println("JSON Parse Error: " + String(error.c_str()));
    }
  }
  else{
    Serial.printf("HTTP GET failed");
  }
  http.end();
  return token;
}

String sendAudioToBaidu(String encoded_audio, size_t audio_length){
  String recognizedText = "";
  DynamicJsonDocument doc(4096);
  HTTPClient http;
  http.begin("http://vop.baidu.com/server_api");
  http.addHeader("Content-Type", "application/json");
  doc["format"] = "pcm";
  doc["rate"] = 16000;
  doc["channel"] = 1;
  doc["token"] = access_token;
  doc["cuid"] = "57722215";
  doc["len"] = audio_length;
  doc["speech"] = encoded_audio;
  String json_request;
  serializeJson(doc,json_request);

  int httpCode = http.POST(json_request);
  String response;
  if(httpCode==200){
    response = http.getString();
    DynamicJsonDocument response_doc(4096);
    deserializeJson(response_doc,response);
    int err_code=response_doc["err_no"].as<int>();
    if (err_code== 0) {
            return response_doc["result"][0].as<String>();
        } else {
            Serial.printf("Error Code: %d\n",err_code);
            return "Error: " + String(response_doc["err_msg"].as<String>());
        }
    } else {
        response = "Failed to recognize audio, HTTP Code: " + String(httpCode);
    }

    http.end();
    return response;
  
}

bool isSilent(int16_t* audio_data, size_t length){
  uint32_t sum = 0;
  for (size_t i = 0; i < length; i++) {
        sum += abs(audio_data[i]);
    }
  uint16_t average = sum / length;
  return average < SILENCE_THRESHOLD;
}  