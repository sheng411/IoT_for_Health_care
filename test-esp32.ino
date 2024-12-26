#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <LiquidCrystal_PCF8574.h>

// Wi-Fi 設定
const char* ssid = "00";
const char* password = "222db776d523";

// MQTT Broker 設定
const char* server = "broker.hivemq.com";
int port = 1883;

// MQTT topics
#define TOPIC_INFO  "11103081A/info"
#define TOPIC_TEMP  "11103081A/temp"
#define TOPIC_HUM   "11103081A/hum"

// Line API 設定
const String lineToken = "hw/cfnhTM0AUdbOL7Dla0QHmOByhAwsCvd9w3F8TfEM3zxDWPdO3oxyuZK6xEbRMQ2HoQrLfdoh5f2qPHG0zZxgTCLL80P/k+yMjy9pbNUgCE8oxoDfRv4giDLIkCvjMFty29waBXwp0+rby/2rF5QdB04t89/1O/w1cDnyilFU="; // 替換為你的 Line Token
const String userId = "U1c9aa11ab8ad0625e73cc01057b09b6d";     

// DHT 設定
#define DHTPIN 17
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Timer 設定
unsigned long mqtt_last_time = 0;
#define MQTT_PERIOD 10000 // MQTT 每 10 秒上傳一次

// Pin 定義
#define relayPin 16
#define buttonPin 18
#define dipSwitchPin 19

// 溫度門檻
#define TEMP_THRESHOLD 23.0

// 狀態變數
bool isFanOn = false;
bool buttonPressed = false;
bool autoMode = true;

// Wi-Fi 和 MQTT 客戶端
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// 初始化液晶螢幕 (I2C 地址為 0x27)
LiquidCrystal_PCF8574 lcd(0x27);

// 傳送 Line 訊息函式
void sendLineMessage(float temperature, float humidity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://api.line.me/v2/bot/message/push");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + lineToken);

    String message = "⚠️ 溫度高於 " + String(TEMP_THRESHOLD, 1) + " 度！目前溫度：" + String(temperature, 1) + "°C，濕度：" + String(humidity, 1) + "%，風扇自動開啟!";
    String payload = "{\"to\":\"" + userId + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + message + "\"}]}";

    int httpResponseCode = http.POST(payload);
    if (httpResponseCode == 200) {
      Serial.println("Line Message Sent!");
    } else {
      Serial.print("Error sending message: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

// MQTT 重連函式
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("SensorNode_11103081A")) {
      Serial.println("connected");
      client.publish(TOPIC_INFO, "sensor node ready ...");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  client.setServer(server, port);
  dht.begin();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(dipSwitchPin, INPUT_PULLUP);

  // 初始化液晶螢幕
  lcd.begin(16, 2); // 設定 LCD 為 16x2
  lcd.setBacklight(255); // 開啟背光
  lcd.setCursor(0, 0);
  lcd.print("System Ready");
}

void loop() {
  unsigned long current_time = millis();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 讀取撥碼器狀態
  autoMode = digitalRead(dipSwitchPin) == HIGH;

  // **每 10 秒上傳溫濕度到 MQTT**
  if (current_time - mqtt_last_time > MQTT_PERIOD) {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (!isnan(temperature) && !isnan(humidity)) {
      Serial.print("Temperature: ");
      Serial.println(temperature);
      Serial.print("Humidity: ");
      Serial.println(humidity);

      // 顯示溫濕度到 LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(temperature, 1);
      lcd.print(" C");
      lcd.setCursor(0, 1);
      lcd.print("Hum: ");
      lcd.print(humidity, 1);
      lcd.print(" %");

      // 上傳到 MQTT
      client.publish(TOPIC_TEMP, String(temperature).c_str());
      client.publish(TOPIC_HUM, String(humidity).c_str());

      // **自動模式：檢查溫度並控制風扇**
      if (autoMode) {
        if (temperature > TEMP_THRESHOLD && !isFanOn) {
          Serial.println("Temperature exceeds threshold. Turning fan ON.");
          digitalWrite(relayPin, HIGH);
          isFanOn = true;

          // 發送 Line 通知（僅第一次啟動風扇時發送）
          sendLineMessage(temperature, humidity);
        } else if (temperature <= TEMP_THRESHOLD && isFanOn) {
          Serial.println("Temperature below threshold. Turning fan OFF.");
          digitalWrite(relayPin, LOW);
          isFanOn = false;
        }
      }
    }
    mqtt_last_time = current_time;
  }

  // **手動模式：按鈕控制風扇**
  if (!autoMode) {
    if (digitalRead(buttonPin) == LOW) {
      delay(50);
      if (!buttonPressed) {
        buttonPressed = true;
        isFanOn = !isFanOn;
        Serial.print("Button pressed. Fan state: ");
        Serial.println(isFanOn ? "ON" : "OFF");
        digitalWrite(relayPin, isFanOn ? HIGH : LOW);
      }
    } else {
      buttonPressed = false;
    }
  }
}
