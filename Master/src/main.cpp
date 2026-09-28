#include<Arduino.h>

#include<WiFi.h>
#include<WiFiClientSecure.h>
#include<ArduinoJson.h>

#include "WiFiconfig.h"
#include "LLMconfig.h"

void WiFiConnectTask(void *pvParameters){

    Serial.println("WiFi Connect Start");

    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

    while(WiFi.status() == WL_CONNECT_FAILED){
        Serial.println("WiFi is connecting");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    for(;;){
        if(WiFi.status() == WL_CONNECT_FAILED){
            WiFi.reconnect();
            Serial.println("WiFi is reconnecting");
        }
        else{
            Serial.println("WiFi is going");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void setup(){
    Serial.begin(115200);
    delay(1000);

    //WiFi证书
    WiFiClientSecure client;
    //关闭验证
    client.setInsecure();

    xTaskCreatePinnedToCore(WiFiConnectTask, "WIFI CONNECT TASK", 4096, NULL, 1, NULL, 1);

}

void loop(){
    vTaskDelay(pdMS_TO_TICKS(1000));
}