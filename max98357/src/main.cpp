#include <Arduino.h>
#include<driver/i2s.h>



// WiFi配置
const char* ssid = "jtkj";
const char* password = "3Ljp@682";

#define SAMPLE_RATE 16000 //音频采样率

//麦克风硬件引脚定义
#define I2S_WS 5
#define I2S_SCK 6
#define I2S_SD 7
#define I2S_Port I2S_NUM_0  //输入

//扬声器引脚定义
#define I2S_LRC 15
#define I2S_BCLK 16
#define I2S_DIN 17
#define I2S_Port_On I2S_NUM_1 //音频输出


//函数声明
void i2s_install_in();
void i2s_pin_setup_in();
void i2s_install_on();
void i2s_pin_setup_on();

void setup() {
  Serial.begin(115200);
  i2s_install_in();
  i2s_pin_setup_in();
  i2s_install_on();
  i2s_pin_setup_on();
}

void loop() {
  size_t bytes_read;
  int16_t data[1024];
  esp_err_t result=i2s_read(I2S_Port,&data,sizeof(data),&bytes_read,portMAX_DELAY);

  for(int i=0;i<bytes_read/2;i++){
    Serial.println(data[i]);
  }

  float volume_scale = 2.0; 
  for (int i = 0; i < bytes_read / sizeof(int32_t); i++) {
    data[i]=data[i]*volume_scale;
    data[i] = data[i] >> 8; // 去除高位填充（24→32位对齐）
  }

  

  size_t bytes_write;
  result=i2s_write(I2S_Port_On,&data, sizeof(data), &bytes_write, portMAX_DELAY);
}


void i2s_install_in(){
  const i2s_config_t i2s_config_in={
    .mode=(i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate=SAMPLE_RATE,
    .bits_per_sample=I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format=I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags=ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count=8,
    .dma_buf_len=512
  };
  i2s_driver_install(I2S_Port, &i2s_config_in, 0, NULL);
}

void i2s_pin_setup_in(){
  const i2s_pin_config_t i2s_pin_in={
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  i2s_set_pin(I2S_Port,&i2s_pin_in);
}

void i2s_install_on(){
  i2s_config_t i2s_config_on={
    .mode=(i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate=SAMPLE_RATE,
    .bits_per_sample=I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format=I2S_COMM_FORMAT_STAND_I2S ,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len=512
  };
  i2s_driver_install(I2S_Port_On, &i2s_config_on, 0, NULL);
}

void i2s_pin_setup_on(){
  const i2s_pin_config_t i2s_pin_out={
    .bck_io_num=I2S_BCLK,
    .ws_io_num=I2S_LRC,
    .data_out_num=I2S_DIN,
    .data_in_num=-1
  };
  i2s_set_pin(I2S_Port_On,&i2s_pin_out);
}