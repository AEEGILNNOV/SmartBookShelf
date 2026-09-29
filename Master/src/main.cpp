#include <Arduino.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "WiFiconfig.h"
#include "LLMconfig.h"

//测试网址

//static const char* Test_URL = "https://api.deepseek.com";
static const char* Test_URL = "https://jsonplaceholder.typicode.com/posts";

//测试POST JSON
static const String Test_JSON = R"({
    "title": "ESP32-S3 Test",
    "body": "Hello from ESP32-S3",
    "userId": 1,
    "message":{
        "first":"Hello JSON",
        "second":"Hello C"
    }
})";

//前置函数声明
static void HTTPSTestTask(void *pvParameters);
static void WiFiConnectTask(void *pvParameters);

//初始化
void setup(){
    Serial.begin(115200);
    delay(1000);

    xTaskCreatePinnedToCore(WiFiConnectTask, "WIFI CONNECT TASK", 4096, NULL, 1, NULL, 1);

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

    xTaskCreatePinnedToCore(HTTPSTestTask, "HTTPS Test Task", 32768, NULL, 1, NULL, 1);

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
    uint16_t httpcode = 0;

    //http正文
    String http_context = "";

    //JSON信息

    //https证书 
    static WiFiClientSecure client;

    //关闭证书验证
    client.setInsecure();

    //创建https客户端
    static HTTPClient https;

    //启动并判断http状态
    if(!https.begin(client, Test_URL)){
        Serial.println("https begin failure");

        vTaskDelete(NULL);
    }
    else{
        httpcode = https.GET();
        Serial.println("httpscode:");
        Serial.println(httpcode);

        //获取具体错误信息
        Serial.println(https.errorToString(httpcode));
    }

    //请求内容
    http_context = https.getString();
    //Serial.println(http_context);

    //发送内容
    //设置http头
    https.addHeader("Content-Type", "application/json");

    //发送
    httpcode = https.POST(Test_JSON);
    
    //JSON解析
    if(httpcode > 0){

        //接收信息
        http_context = https.getString();
        //Serial.println(http_context);

        //创建JSON文档
        static JsonDocument JSON_doc;

        //解析
        static DeserializationError JSON_error = deserializeJson(JSON_doc, http_context); //解析为数据结构

        if(JSON_error){//检查JSON格式是否正确
            Serial.println("It's not JSON");
            Serial.println(JSON_error.c_str());//转化为c风格字符串
        }
        else{
            const char* JSON_title = JSON_doc["title"];
            const char* JSON_body = JSON_doc["body"];
            const uint8_t JSON_userId = JSON_doc["userId"];
            const char* JSON_msg = JSON_doc["message"]["second"];

            Serial.println(JSON_title);
            Serial.println(JSON_body);
            Serial.println(JSON_userId);
            Serial.println(JSON_msg);
        }
    }


    //结束会话
    https.end();
    vTaskDelete(NULL);

}