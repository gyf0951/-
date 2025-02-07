#include <Arduino.h>
#include <WiFi.h>
#include<stdio.h>
#include<HTTPClient.h>
#include<ArduinoJson.h>


//WiFi配置
const char * ssid = "jtkj";
const char * password = "3Ljp@682";

//豆包api key
const char *api_key="sk-df45f0d3cd5b429c8b2e6359bd311539";

String apiUrl="https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
String inputText = "你好";

String answer;
String getAnswer(String inputText){
   HTTPClient http;
   http.setTimeout(30000);
   http.begin(apiUrl);
   http.addHeader("Content-Type", "application/json");
   String token_key = String("Bearer ") + api_key;
   http.addHeader("Authorization", token_key);  
   String payload = "{\"model\":\"qwen-plus\",\"messages\":[{\"role\":\"system\",\"content\":\"你好,你必须用中文回答且字数不超过85个\"},{\"role\":\"user\",\"content\":\"" + inputText + "\"}]}";
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

void setup(){
//WiFi配置
 Serial.begin(115200);
 WiFi.mode(WIFI_STA);
 WiFi.begin(ssid, password);
 Serial.print("Connecting to WiFi");
 while(WiFi.status()!=WL_CONNECTED){
    delay(300);
    Serial.print(".");
 }

 Serial.println("\nConnect Success");
 Serial.print("IP 地址");
 Serial.println(WiFi.localIP()); 
 answer = getAnswer(inputText);
 Serial.println("Answer: " + answer);
}

void loop(){
   if(Serial.available()){
      inputText = Serial.readStringUntil('\n'); //串口输入
      Serial.println("\n Input:"+inputText);
      answer = getAnswer(inputText);
      Serial.println("Answer: " + answer);
      Serial.println("Enter a prompt:");
   }
}

