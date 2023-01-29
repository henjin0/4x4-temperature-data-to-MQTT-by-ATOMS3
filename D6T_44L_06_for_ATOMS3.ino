#include <Arduino.h>
#define LGFX_AUTODETECT 
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

#include <WiFi.h>
#include <MQTT.h>
#include <ArduinoJson.h>

#include <M5AtomS3.h>
#include "OmronD6T.h"
#include <math.h>

#define WIDTH (128 / 4)
#define HEIGHT (128 / 4)

static LGFX lcd;                 // LGFXのインスタンスを作成。
static LGFX_Sprite sprite(&lcd); // スプライトを使う場合はLGFX_Spriteのインスタンスを作成。

unsigned long lastMillis = 0;

// Wi-FiのSSID
char *ssid = "xxxxxxx";
// Wi-Fiのパスワード
char *pass = "xxxxxxxx";

WiFiClient httpsClient;
MQTTClient client;

OmronD6T sensor;

float gain = 10.0;
float offset_x = 0.2;
float offset_green = 0.6;

float sigmoid(float x, float g, float o) {
  return (tanh((x + o) * g / 2) + 1) / 2;
}

uint16_t heat(float x) {  // 0.0〜1.0の値を青から赤の色に変換する
  x = x * 2 - 1;          // -1 <= x < 1 に変換

  float r = sigmoid(x, gain, -1 * offset_x);
  float b = 1.0 - sigmoid(x, gain, offset_x);
  float g = sigmoid(x, gain, offset_green) + (1.0 - sigmoid(x, gain, -1 * offset_green)) - 1;

  return (((int)(r * 255) >> 3) << 11) | (((int)(g * 255) >> 2) << 5) | ((int)(b * 255) >> 3);
}

void connect() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.print("\nconnecting...");
  while (!client.connect("M5Stack", "public", "public")) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

  client.subscribe("hello");
}

void messageReceived(String &topic, String &payload) {
  Serial.println(topic + ": " + payload);
}

void setup() {
  //dac_output_disable(DAC_CHANNEL_1);
  Serial.begin(115200);
  M5.begin();

  lcd.init();
  lcd.clear();
  sprite.createSprite(128, 128);
  sprite.fillRect(0,0,128,128,TFT_BLACK);
  sprite.pushSprite(0,0);

  // Start WiFi
  Serial.println("Connecting to ");
  Serial.print(ssid);

  // start wifi and mqtt
  WiFi.begin(ssid, pass);
  client.begin("public.cloud.shiftr.io", httpsClient);
  client.onMessage(messageReceived);

  connect();
}

void loop() {

  StaticJsonDocument<192> doc;
  JsonArray temps = doc.createNestedArray("temps");  
  // 常にチェックして切断されたら復帰できるように
  client.loop();
  delay(10);

  // check if connected
  if (!client.connected()) {
    connect();
  }

  sensor.scanTemp();
  sprite.createSprite(128, 128);
  sprite.fillRect(0,0,128,128,TFT_BLACK);
  int x, y;

  for (y = 0; y < 4; y++) {
    for (x = 0; x < 4; x++) {
      float temp = sensor.temp[x][y];
      if (temp > 100.0) {  // 値が100℃以上はエラー。描画の途中でも、終わる
        return;
      }
      float t = map(constrain((int)temp, 0, 60), 0, 60, 0, 100);
      uint16_t color = heat(t / 100.0);
      sprite.fillRect(y * WIDTH, x * HEIGHT, WIDTH, HEIGHT, color);
      sprite.setCursor(y * WIDTH + WIDTH / 2, x * HEIGHT + HEIGHT / 2);
      sprite.setTextColor(BLACK, color);
      sprite.printf("%.1f", temp);
      temps.add((int)floor(temp*10));
    }
  }

  sprite.pushSprite(0,0);

  String output;
  serializeJson(doc, output);

  client.publish("/pub/M5Stack", output.c_str());

  delay(500);
}