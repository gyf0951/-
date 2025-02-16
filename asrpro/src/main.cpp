#include <Arduino.h>

#define RX_PIN 10  // ASRPRO的TX连接到ESP32S3的RX (GPIO16)
#define TX_PIN 11  // ASRPRO的RX连接到ESP32S3的TX (GPIO17)

HardwareSerial mySerial(1);  // 使用硬件串口1

void setup() {
  // 启动串口调试输出
  Serial.begin(115200);
  
  // 初始化UART1（ASRPRO连接串口）
  mySerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // 设置PA4为输入引脚
  pinMode(34, INPUT);  // 假设使用GPIO34来读取PA4电平
}

void loop() {
  // 读取ASRPRO串口数据
  if (mySerial.available()) {
    Serial.println("串口通信成功");
    String data = mySerial.readString();  // 读取字符串
    Serial.print("Received: ");
    Serial.println(data);  // 输出接收到的字符串
  }

  // 读取PA4的电平
  /* int pa4_state = digitalRead(34);  // 读取PA4电平
  if (pa4_state == HIGH) {
    Serial.println("PA4 is HIGH");
  } else {
    Serial.println("PA4 is LOW");
  } */

  delay(100);  // 每100ms读取一次
}
