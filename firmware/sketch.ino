// 運行情報ブザー: ステップ4(OLED表示を追加)
// 対象: Wokwi の ESP32 (DevKit C V4)
// 配線:
//   ブザー  : 「1」(+側) → GPIO18 / 「2」 → GND
//   緑LED   : GPIO19 → 抵抗220Ω → LEDのA(足の長いほう) → LEDのC → GND
//   黄LED   : GPIO17 → 抵抗220Ω → LEDのA → LEDのC → GND
//   赤LED   : GPIO16 → 抵抗220Ω → LEDのA → LEDのC → GND
//   OLED    : VCC → 3V3 / GND → GND / SCL → GPIO22 / SDA → GPIO21
// 必要ライブラリ: PubSubClient, Adafruit SSD1306, Adafruit GFX Library

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const int BUZZER_PIN = 18;
const int LED_GREEN  = 19;  // 平常
const int LED_YELLOW = 17;  // 遅延
const int LED_RED    = 16;  // 運転見合わせ

// OLED (SSD1306 128x64, I2C)
const int OLED_WIDTH  = 128;
const int OLED_HEIGHT = 64;
const int OLED_ADDR   = 0x3C;
const int OLED_SDA    = 21;
const int OLED_SCL    = 22;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;

// Wi-Fi (Wokwiのシミュレータ用。パスワードなし)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// MQTT (公開ブローカー)
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
// 他の人と被らないよう、必ず自分だけの名前に変えてください
const char* MQTT_TOPIC = "train-buzzer-x7k2q9/status";

// 運行状態
enum TrainState { NORMAL, DELAY_STATE, SUSPENDED };

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// 受信した状態は、ここに一度ためてから loop() で処理する
bool hasPending = false;
TrainState pendingState = NORMAL;

// ---- ブザー制御(ESP32 Arduinoコアの版差を吸収) ----
void buzzerBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // コア3.x: tone()がそのまま使える
#else
  ledcSetup(0, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 0);
#endif
}

void buzzerOn(int freq) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  tone(BUZZER_PIN, freq);
#else
  ledcWriteTone(0, freq);
#endif
}

void buzzerOff() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  noTone(BUZZER_PIN);
#else
  ledcWriteTone(0, 0);
#endif
}

void beep(int freq, int onMs, int offMs) {
  buzzerOn(freq);
  delay(onMs);
  buzzerOff();
  delay(offMs);
}

// ---- LED制御 ----
void ledsBegin() {
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
}

void ledsOff() {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
}

void showLed(TrainState state) {
  ledsOff();
  switch (state) {
    case NORMAL:      digitalWrite(LED_GREEN, HIGH);  break;
    case DELAY_STATE: digitalWrite(LED_YELLOW, HIGH); break;
    case SUSPENDED:   digitalWrite(LED_RED, HIGH);    break;
  }
}

void ledSelfTest() {
  int pins[3] = {LED_GREEN, LED_YELLOW, LED_RED};
  for (int i = 0; i < 3; i++) {
    digitalWrite(pins[i], HIGH);
    delay(300);
    digitalWrite(pins[i], LOW);
  }
}

// ---- OLED表示 ----
// 標準フォントは日本語が出ないので、英字で表示する
void oledBegin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledReady = true;
  } else {
    Serial.println("OLEDが見つかりません。配線(SDA/SCL/VCC/GND)を確認してください");
  }
}

void showMessage(const char* line1, const char* line2) {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  display.setTextSize(2);
  display.setCursor(0, 28);
  display.println(line2);
  display.display();
}

void showOled(TrainState state) {
  switch (state) {
    case NORMAL:      showMessage("JOBAN LINE", "NORMAL");    break;
    case DELAY_STATE: showMessage("JOBAN LINE", "DELAY");     break;
    case SUSPENDED:   showMessage("JOBAN LINE", "SUSPENDED"); break;
  }
}

// ---- 状態ごとの音パターン ----
void playPattern(TrainState state) {
  switch (state) {
    case NORMAL:
      beep(1500, 150, 0);
      break;
    case DELAY_STATE:
      beep(1000, 300, 0);
      break;
    case SUSPENDED:
      for (int i = 0; i < 3; i++) {
        beep(800, 600, 250);
      }
      break;
  }
}

// ---- 状態を受け取る共通の入口 ----
void setPending(TrainState s) {
  pendingState = s;
  hasPending = true;
}

// 文字列(NORMAL / DELAY / SUSPENDED)を状態に変換する
void handleMessage(String msg) {
  msg.trim();
  msg.toUpperCase();
  Serial.print("受信: ");
  Serial.println(msg);
  if (msg == "NORMAL") {
    setPending(NORMAL);
  } else if (msg == "DELAY") {
    setPending(DELAY_STATE);
  } else if (msg == "SUSPENDED") {
    setPending(SUSPENDED);
  } else {
    Serial.println("未知のメッセージなので無視します");
  }
}

// MQTTのメッセージが届いた時に呼ばれる
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  handleMessage(msg);
}

// ---- 接続処理 ----
void connectWifi() {
  Serial.print("Wi-Fi接続中");
  showMessage("WiFi", "connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(" 接続しました");
}

void connectMqtt() {
  while (!mqtt.connected()) {
    Serial.print("MQTT接続中...");
    showMessage("MQTT", "connecting");
    String clientId = "esp32-buzzer-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("成功");
      mqtt.subscribe(MQTT_TOPIC);
      Serial.print("購読開始: ");
      Serial.println(MQTT_TOPIC);
      showMessage("JOBAN LINE", "WAITING");
    } else {
      Serial.print("失敗(rc=");
      Serial.print(mqtt.state());
      Serial.println(")。2秒後に再試行");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  buzzerBegin();
  ledsBegin();
  oledBegin();
  ledSelfTest();
  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  connectMqtt();
  Serial.println("シリアルでも n / d / s で動作確認できます");
}

void loop() {
  if (!mqtt.connected()) {
    connectMqtt();
  }
  mqtt.loop();

  // 動作確認用: シリアル入力でも状態を切り替えられる
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'n') handleMessage("NORMAL");
    else if (c == 'd') handleMessage("DELAY");
    else if (c == 's') handleMessage("SUSPENDED");
  }

  // ためておいた状態を処理する(表示とLEDを更新してから音を鳴らす)
  if (hasPending) {
    hasPending = false;
    showLed(pendingState);
    showOled(pendingState);
    playPattern(pendingState);
  }
}
