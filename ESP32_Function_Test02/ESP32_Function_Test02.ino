#include <WiFi.h>
#include <Wire.h>
#include <SimpleDHT.h>
#include <WiFiClientSecure.h>
#include <TridentTD_LineNotify.h>

//----HTTP----------------------------------------
#include <HTTPClient.h>
String serverserverIPAddress = "http://192.168.66.27:8080";
WiFiServer server(8080);
//--------------------------------------------

//--------------------------------------------
//ssids[] = {"かみ-と", "csmulib", "Winted-2G", "R604-JCS"};
//passwords[] = {"r125115310", "8112281123", "0956826126", "JCS11806"};
char SSID[] = "Winted-2G";
char PASSWORD[] = "0956826126";
String Linetoken = "xThEWDzqFvPs7IwKaFoZf864RKc35wAsxRq2v4EY4jW";//跟LINE申請
WiFiClientSecure client;//網路連線物件
char host[] = "notify-api.line.me";//LINE Notify API網址
//---------------------------------------------------------

// 定義感測器數據結構
struct SensorData {
  float temperature;
  float humidity;
  int airQuality;
  float particulateMatter;
};

//DHT11
int pinDHT11 = 15;
SimpleDHT11 dht11;

//MQ135 由於本專案會使用WIFI功能因此改用ADC1通道腳位
int pinMQ135 = 32;
int dataMQ135;

//GP2Y1014
int pinGP2YVo = 39; //Vo接到IO39
int pinGP2Y_IRED = 5; //IRED接到IO5
float Vo = 0; //用以紀錄電壓

//三色LED
const int Red = 17;
const int Green = 16;
const int Blue = 4;

//按鈕
const int BUTTON_PIN = 18;  // 按鍵的接腳
int buttonState = 0;   // 按鈕的狀態

//按鈕HTTP判斷
unsigned long buttonLastChanged = 0;
unsigned long httpLastChanged = 0;

//模式定義
enum Mode {safeMode, WarningMode, CleanMode};
Mode mode;  // 全局變數來存儲當前模式
bool WarningBool = false;
bool CleanBool = false;
bool HTTPCleanBool = false;

//蜂鳴器、蜂鳴器狀態和計時器變量
int buzzerPin = 23;
bool buzzerActive = false;          // 蜂鳴器是否激活
unsigned long buzzerStartTime = 0;  // 記錄蜂鳴器開始響起的時間
const int buzzerDuration = 500;     // 蜂鳴器響起的持續時間，單位為毫秒
unsigned long lastBuzzerTime = 0;
const unsigned long BuzzerInterval = 15000;

//按鈕On/Off判斷
bool buttonBool = false;

// 記錄上一次發送LINE通知的時間、設定最小發送間隔為15秒
unsigned long lastLineNotifyTime = 0;
const unsigned long lineNotifyInterval = 15000;

//函數宣告
bool readDHT11(float* temperature, float* humidity);
int readMQ135();
float readGP2Y();
void buzz();
void checkAndNotify(const SensorData& data);
void LED_Red();
void LED_Green();
void LED_Blue();
void readButtonState();

void updateButtonState();
void updateHTTPState(bool newHTTPState) ;

void updateMode();
void printMode();
void handleClient(String RequestLine, WiFiClient client, String key1, int value1, String key2, int value2, String key3, double value3, String key4, int value4);
void sendJSONData(WiFiClient client, String key1, int value1, String key2, int value2, String key3, double value3, String key4, int value4);

void setup() {
  Serial.begin(115200);

  //連線到指定的WiFi SSID
  Serial.print("Connecting Wifi: ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(200);
  }
  //連線成功，顯示取得的IP
  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
  client.setInsecure();

  //接角
  pinMode(pinMQ135, INPUT);
  pinMode(pinGP2YVo, INPUT); //GP2Y1014讀取電壓腳位
  pinMode(pinGP2Y_IRED, OUTPUT);//GP2Y1014控制IRED腳位
  //三色LED
  pinMode(Red, OUTPUT);
  pinMode(Green, OUTPUT);
  pinMode(Blue, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);//按鈕
  pinMode(buzzerPin, OUTPUT); //蜂鳴器

  //初始化模式
  mode = safeMode;  // 初始模式設為 safeMode

  //HTTP收信
  server.begin();
}

void loop() {
  SensorData data;
  if (readDHT11(&data.temperature, &data.humidity)) {
    Serial.print("Temperature: ");
    Serial.print(data.temperature);
    Serial.print(" C, Humidity: ");
    Serial.print(data.humidity);
    Serial.println(" %");
  }

  data.airQuality = readMQ135();
  Serial.print("Air Quality: ");
  Serial.print(data.airQuality);
  Serial.println(" PPM");

  data.particulateMatter = readGP2Y();
  Serial.print("Particulate Matter: ");
  Serial.print(data.particulateMatter);
  Serial.println(" ug/m3");

  //HTTP通信
  WiFiClient client = server.available();
  if (client) {
    String InitRequest = client.readStringUntil('\r');
    handleClient(InitRequest, client, "temp", data.temperature, "humid", data.humidity, "toxgas", data.airQuality, "partic", data.particulateMatter);
  }

  //LINE
  checkAndNotify(data);

  //按鈕偵測
  readButtonState();

  updateMode();
  printMode();
  delay(1000);
}

bool readDHT11(float* temperature, float* humidity) {
  byte temp = 0;
  byte hum = 0;
  int err = SimpleDHTErrSuccess;

  if ((err = dht11.read(pinDHT11, &temp, &hum, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT11 failed, err=");
    Serial.println(err);
    return false;
  }

  *temperature = (float)temp;
  *humidity = (float)hum;
  return true;
}

int readMQ135() {
  return analogRead(pinMQ135);
}

float readGP2Y() {
  digitalWrite(pinGP2Y_IRED, LOW);
  delayMicroseconds(280);
  float Vo = analogRead(pinGP2YVo);
  Vo = (Vo / 4095.0) * 3.3;
  delayMicroseconds(40);
  digitalWrite(pinGP2Y_IRED, HIGH);
  delayMicroseconds(9680);

  if (Vo >= 0.6) {
    return ((0.172 * Vo - 0.1) * 1000);
  }
  return 0; // 小於0.6，都當成0懸浮微粒
}

void buzz() {
  digitalWrite(buzzerPin, HIGH); // 有源蜂鳴器響起
  delay(500); // 持續 0.5 秒
  digitalWrite(buzzerPin, LOW);  // 有源蜂鳴器關閉
}

void checkAndNotify(const SensorData& data) {
  unsigned long currentMillis = millis();  // 獲取當前時間

  if (data.temperature < 18 || data.temperature > 24 ||
      data.humidity < 40 || data.humidity > 60 ||
      data.airQuality > 700 ||
      data.particulateMatter > 1050) {
    WarningBool = true;
    Serial.println("WarningBool = true;");
    if (currentMillis - lastLineNotifyTime >= lineNotifyInterval) {
      // 準備發送的信息
      String tempe = "溫度:" + String(data.temperature) + "℃\n";
      String humid = "濕度:" + String(data.humidity) + "%\n";
      String airqua = "有害氣體濃度:" + String(data.airQuality) + "PPM\n";
      String parti = "灰塵、懸浮微粒:" + String(data.particulateMatter) + "ug/m3";
      String message = "\n環境異常警報：\n" + tempe + humid + airqua + parti;

      Serial.println(LINE.getVersion());
      LINE.setToken(Linetoken);
      LINE.notify(message);  // 發送通知

      Serial.println("LINE Notify sent: " + message);
      lastLineNotifyTime = currentMillis;  // 更新上一次發送時間
      buzz();//蜂鳴器
    }
  } else {
    WarningBool = false;
    Serial.println("WarningBool = false;");
  }
}

void LED_Red() {
  analogWrite(Red, 255);
  analogWrite(Green, 0);
  analogWrite(Blue, 0);
}

void LED_Green() {
  analogWrite(Red, 0);
  analogWrite(Green, 255);
  analogWrite(Blue, 0);
}

void LED_Blue() {
  analogWrite(Red, 0);
  analogWrite(Green, 0);
  analogWrite(Blue, 255);
}

void readButtonState() {
  buttonState = digitalRead(BUTTON_PIN);
  delay(100);
  if (buttonState == LOW) {        //如果按鍵按了
    buttonBool = !buttonBool;
  }

  if (buttonBool || HTTPCleanBool) {
    //代表現在清淨機是開啟狀態
    CleanBool = true;
    Serial.println("CleanBool = true;");
  }
  else if (!buttonBool) {
    CleanBool = false;
    Serial.println("CleanBool = false;");
  }
}

void updateMode() {
  if (CleanBool) {
    mode = CleanMode;
    LED_Green();
  } else if (WarningBool) {
    mode = WarningMode;
    LED_Red();
  } else {
    mode = safeMode;
    LED_Blue();
  }
}

void printMode() {
  switch (mode) {
    case safeMode:
      Serial.println("Current mode: Safe Mode");
      break;
    case WarningMode:
      Serial.println("Current mode: Warning Mode");
      break;
    case CleanMode:
      Serial.println("Current mode: Clean Mode");
      break;
  }
}

void handleClient(String RequestLine, WiFiClient client, String key1, int value1, String key2, int value2, String key3, double value3, String key4, int value4) {
  String header;
  String request = client.readStringUntil('\r');
  String currentLine = "";

  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      Serial.write(c);
      header += c;

      if (c == '\n') {
        if (currentLine.length() == 0) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.println("Access-Control-Allow-Origin: *");
          client.println("Connection: close");
          client.println();

          sendJSONData(client, key1, value1, key2, value2, key3, value3, key4, value4);

          break;
        } else {
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }
  }
  Serial.print("RequestLine: ");
  Serial.println(RequestLine);
  if (RequestLine.indexOf("GET /on") >= 0) {
    Serial.println("Air purifier is On");
    HTTPCleanBool = true;
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.println("Air purifier is On");
  }
  else if (RequestLine.indexOf("GET /off") >= 0) {
    Serial.println("Air purifier is Off");
    HTTPCleanBool = false;
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.println("Air purifier is Off");
  }
  client.stop();
  Serial.println("Client disconnected.");
  Serial.println("");
}

void sendJSONData(WiFiClient client, String key1, int value1, String key2, int value2, String key3, double value3, String key4, int value4) {

  String jsonData = "{";
  jsonData += "\"" + key1 + "\":" + String(value1) + ",";
  jsonData += "\"" + key2 + "\":" + String(value2) + ",";
  jsonData += "\"" + key3 + "\":" + String(value3) + ",";
  jsonData += "\"" + key4 + "\":" + String(value4);
  jsonData += "}";

  client.println(jsonData);
  Serial.println("JSON data sent: " + jsonData);
}
