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
    "userId": 1
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
    Serial.println(http_context);

    //发送内容
    //设置http头
    https.addHeader("Content-Type", "application/json");

    //发送
    httpcode = https.POST(Test_JSON);
    http_context = https.getString();
    Serial.println(http_context);

    //结束会话
    https.end();
    vTaskDelete(NULL);

}