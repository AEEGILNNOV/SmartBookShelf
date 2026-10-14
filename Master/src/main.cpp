#include <Arduino.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "WiFiconfig.h"
#include "LLMconfig.h"
#include "PINSETconfig.h"

//超声波测量 全局变量声明
static volatile uint32_t _EchoStartUS = 0;
static volatile uint32_t _EchoPulseUs = 0;
static volatile bool _EchoDone = false;

//定义对象
//创建https客户端
static HTTPClient https;

//https证书 
static WiFiClientSecure client;

//创建JSON文档
static JsonDocument JSON_doc;

//LLM JSON 请求
static String LLM_JSON = R"({
    "model": ")" + String(LLM_MODEL) + R"(",
    "messages": [
        {
            "role": "user",
            "content": "你好，请用一句话介绍你自己。"
        }
    ],
    "temperature": )" + String(LLM_TEMPERATURE) + R"(,
    "max_tokens": )" + String(LLM_MAX_TOKENS) + R"(
})";


//前置函数声明
static void HTTPSTestTask(void *pvParameters);
static void WiFiConnectTask(void *pvParameters);
static void HCSR04Task(void *pvParameters);

static void delayUs(uint16_t _time);

static void _EchoISR();

//初始化
void setup(){
    Serial.begin(115200);
    delay(1000);

    //xTaskCreatePinnedToCore(WiFiConnectTask, "WIFI CONNECT TASK", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(HCSR04Task, "HC-SR04Task", 1024, NULL, 1, NULL, 1);

//    pinMode(4,OUTPUT);
//    while(1){
//    digitalWrite(4,HIGH);
//    vTaskDelay(pdMS_TO_TICKS(1000));
//    digitalWrite(4,LOW);
//    vTaskDelay(pdMS_TO_TICKS(1000));
//    }
}

//循环
void loop(){
    vTaskDelay(pdMS_TO_TICKS(1000));
}

//FreeRTOS Task
static void WiFiConnectTask(void *pvParameters){

    Serial.println("WiFi Connect Start");

    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

    while(WiFi.status() != WL_CONNECTED){
        Serial.println("WiFi is connecting");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    xTaskCreatePinnedToCore(HTTPSTestTask, "HTTPS Test Task", 65536, NULL, 1, NULL, 1);

    for(;;){
        if(WiFi.status() != WL_CONNECTED){
            WiFi.reconnect();
            Serial.println("WiFi is reconnecting");
        }   
        else{
            Serial.println("WiFi is going");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void HTTPSTestTask(void *pvParameters){

    //http状态码
    int16_t httpcode = 0;

    //http正文
    String http_context = "";

    //关闭证书验证
    client.setInsecure();

    //启动并判断http状态
    if(!https.begin(client, LLM_URL)){
        Serial.println("https begin failure");

        vTaskDelete(NULL);
    }

    //发送内容
    //设置http头
    https.addHeader(
        "Content-Type",
        "application/json"
    );


    https.addHeader(
        "Authorization",
        "Bearer " + String(LLM_API_KEY)
    );

    Serial.println("https start send");
    
    // 设置超时时间
    https.setTimeout(30000);

    //发送
    httpcode = https.POST(LLM_JSON);

    //JSON解析
    if(httpcode > 0){

        Serial.println(httpcode);
        //接收信息
        http_context = https.getString();
        //Serial.println(http_context);

        Serial.print("HTTP response length: ");
        Serial.println(http_context.length());

        Serial.println("HTTP response:");
        Serial.println(http_context);

        Serial.println(http_context);

        //解析
        DeserializationError JSON_error = deserializeJson(JSON_doc, http_context); //解析为数据结构

        if(JSON_error){//检查JSON格式是否正确
            Serial.println("It's not JSON");
            Serial.println(JSON_error.c_str());//转化为c风格字符串
        }
        else{   
            const char* LLM_message = JSON_doc["choices"][0]["message"]["content"];
            Serial.println(LLM_message);
        }
    }

    //结束会话
    https.end();
    vTaskDelete(NULL);
}

static void HCSR04Task(void *pvParameters){
    pinMode(TRIGPIN, OUTPUT);
    pinMode(ECHOPIN, OUTPUT);

    float distance = 0;

    //注册中断
    attachInterrupt(
        digitalPinToInterrupt(ECHOPIN),
        _EchoISR,
        CHANGE //双边沿
    );

    for(;;){
        digitalWrite(TRIGPIN, LOW);
        delayUs(2);
        digitalWrite(TRIGPIN, HIGH);
        delayUs(10);
        digitalWrite(TRIGPIN, LOW);

        while(!_EchoDone){
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        distance = _EchoPulseUs / 5.80f;
        
        Serial.println(distance);

        // 控制测距频率，避免触发过快
        vTaskDelay(pdMS_TO_TICKS(60));
    }
        
    

}

void delayUs(uint16_t _time){
    uint16_t _updateTime = micros();
    uint16_t _nowTime = micros();

    while ((_nowTime - _updateTime) >= _time)
    {
        _nowTime = micros();
    }

}

void IRAM_ATTR _EchoISR(){
    uint32_t now = micros();

    _EchoDone = false;

    if(digitalRead(ECHOPIN) == HIGH){
        _EchoStartUS = now;
    }
    else{
        _EchoPulseUs = now - _EchoStartUS;
    }

    _EchoDone = true;
}