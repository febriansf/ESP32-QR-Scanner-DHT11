// Library yang dibutuhkan
#include <ThingsBoard.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <DHT.h>
#include <Arduino_MQTT_Client.h>
#include <ArduinoJson.h>
#include <ESP32QRCodeReader.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <qrcodeoled.h>
#include <HTTPClient.h>

// Tipe Layar yang digunakan adalah adalah OLED
#define OLEDDISPLAY

// Ukuran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Pin SDA OLED ke pin 32 ESP32
#define OLED_SDA 32
// Pin SCL OLED ke pin 33 ESP32
#define OLED_SCL 33

// Pengaturan pin dan tipe DHT yang digunakan
#define DHTPIN 14 // Pin data DHT -> Pin 14 ESP32
#define DHTTYPE DHT11
DHT dhtSensor(DHTPIN, DHTTYPE);

// Instansiasi Class untuk display, camera, dan qrcode
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
ESP32QRCodeReader reader(CAMERA_MODEL_WROVER_KIT);
QRcodeOled qrcode(&display); // Class qrcode untuk generate QR

WiFiClient wifiClient;
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient);

// Parameter yang digunakan pada web server saat melakukan POST
const char *AP_SSID = "ssid";
const char *AP_PASS = "pass";
const char *AP_IP = "ip";
const char *AP_GATEWAY = "gateway";
const char *TB_SERVER = "tbserver";
const char *TB_TOKEN = "tbtoken";
const char *API_ADDRESS = "apiaddress";

// Parameter pada web server untuk QR generator
const char *QR_TEXT = "qrtext";

// Port Thingsboard
constexpr uint16_t TB_PORT = 1883U;

// Variabel untuk mengatur penampilan hasil generate QR ke Layar
bool qrGenerate = false;

// Variabel untuk menyimpan nilai dari Form
String ssid;
String pass;
String ip;
String gateway;
String tbserver;
String tbtoken;
String apiaddress;

String qrText;

// Batas atas temperature untuk mengirim warning ke API
float tempLimit = 23.0;
float humidLimit = 50.0;
boolean isExceeded = false;

// Variabel tambahan untuk menyimpan nilai pada saat program running
String wifiStatus;
String config;
String qrCodePayload;

// File untuk menyimpan konfigurasi
const char *configPath = "/config.txt";

// Kelas variabel untuk menyimpan informasi alamat IP, GW, dan Subnet
IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255, 255, 255, 0);

// Variabel untuk timing
unsigned long previousMillis;
unsigned long sendDataMillis;

int send_delay = 1000; // Interval untuk mengirim data ke server
unsigned long millis_counter;
const long intervalWifi = 10000; // interval to wait for Wi-Fi connection (milliseconds)

unsigned long prevMillis; // Vaariabel untuk menyimpan data waktu layar OLED ditampilkan
unsigned long prevMillisQR;
const long intervalQr = 3000; // Interval untuk reset dispkay setelah menampilkan hasil scan QR

// Fungsi untuk membaca nilai temperatur dari sensor DHT
String readDHTTemperature()
{
  float t = round(dhtSensor.readTemperature());
  if (isnan(t))
  {
    Serial.println("Failed to read from DHT sensor!");
    return "--";
  }
  else
  {
    // Serial.println(t);
    return String(t);
  }
}

// Fungsi untuk membaca nilai kelembaban dari sensor DHT
String readDHTHumidity()
{
  float h = dhtSensor.readHumidity();
  if (isnan(h))
  {
    Serial.println("Failed to read from DHT sensor!");
    return "--";
  }
  else
  {
    // Serial.println(h);
    return String(h);
  }
}

// Initialize SPIFFS
void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available())
  {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

String processor(const String &var)
{
  // Serial.println(var);
  if (var == "TEMPERATURE")
  {
    return readDHTTemperature();
  }
  else if (var == "HUMIDITY")
  {
    return readDHTHumidity();
  }
  return String();
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- file written");
  }
  else
  {
    Serial.println("- write failed");
  }
}

// Initialize WiFi
bool initWiFi()
{
  if (ssid == "" || ip == "")
  {
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());

  if (!WiFi.config(localIP, localGateway, subnet))
  {
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - previousMillis >= intervalWifi)
    {
      Serial.println("Failed to connect.");
      previousMillis = millis();
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

// Fungsi untuk menghubungkan kembali ke WiFi ketika terputus
void reconnect()
{
  // Loop until we're reconnected
  WiFi.begin(ssid.c_str(), pass.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 100)
  {
    Serial.println(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnected to WiFi");
  }
  else
  {
    Serial.println("\nFailed to connect to WiFi. Please check your credentials");
  }
}

// Fungsi untuk menampilkan String ke layar OLED
void drawText(int x, int y, OLEDDISPLAY_TEXT_ALIGNMENT textAlign, String text, bool clearOled = false)
{
  if (clearOled)
  {
    display.clear();
  }
  //  display.clear();
  display.setTextAlignment(textAlign);
  display.drawString(x, y, text);
  display.display();
}

void setup()
{
  // Serial port for debugging purposes
  Serial.begin(115200);

  dhtSensor.begin();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  drawText(64, 22, TEXT_ALIGN_CENTER, "QRScanner", true);
  delay(2000);
  display.clear();
  qrcode.init();

  initSPIFFS();
  reader.setup();
  reader.begin();

  config = readFile(SPIFFS, configPath);

  JsonDocument configVar;

  deserializeJson(configVar, config);

  ssid = configVar["ssid"].as<String>();
  pass = configVar["pass"].as<String>();
  ip = configVar["ip"].as<String>();
  gateway = configVar["gateway"].as<String>();
  tbserver = configVar["tbserver"].as<String>();
  tbtoken = configVar["tbtoken"].as<String>();
  apiaddress = configVar["apiaddress"].as<String>();

  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);
  Serial.println(apiaddress);

  if (initWiFi())
  {
    // Routes for web service
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });

    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send_P(200, "text/plain", readDHTTemperature().c_str());
    });
    server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send_P(200, "text/plain", readDHTHumidity().c_str());
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/config.html", "text/html");
    });

    server.on("/config", HTTP_POST, [](AsyncWebServerRequest * request)
    {
      int params = request->params();

      // JSON Buffer for temporary data
      JsonDocument tempData;

      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == AP_SSID) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Add data to json buffer
            tempData["ssid"] = ssid;
          }
          // HTTP POST pass value
          if (p->name() == AP_PASS) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Add data to json buffer
            tempData["pass"] = pass;
          }
          // HTTP POST ip value
          if (p->name() == AP_IP) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Add data to json buffer
            tempData["ip"] = ip;
          }
          // HTTP POST gateway value
          if (p->name() == AP_GATEWAY) {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Add data to json buffer
            tempData["gateway"] = gateway;
          }
          // HTTP POST tbserver value
          if (p->name() == TB_SERVER) {
            tbserver = p->value().c_str();
            Serial.print("Thingsboard Server set to: ");
            Serial.println(tbserver);
            // Add data to json buffer
            tempData["tbserver"] = tbserver;
          }
          // HTTP POST tbtoken value
          if (p->name() == TB_TOKEN) {
            tbtoken = p->value().c_str();
            Serial.print("Token Device set to: ");
            Serial.println(tbtoken);
            // Add data to json buffer
            tempData["tbtoken"] = tbtoken;
          }
          // HTTP POST api address value
          if (p->name() == API_ADDRESS) {
            apiaddress = p->value().c_str();
            Serial.print("API Address set to: ");
            Serial.println(apiaddress);
            // Add data to json buffer
            tempData["apiaddress"] = apiaddress;
          }
        }
        char tempConfig[200];
        serializeJson(tempData, tempConfig);
        writeFile(SPIFFS, configPath, tempConfig);
      }

      request->send(200, "text/plain", "DONE!!!. ESP akan restart, koneksikan ke router " + ssid + " dan jalankan pada browser IP Address: " + ip);
      delay(3000);
      ESP.restart();
    });

    server.on("/qr", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/qrgenerator.html", "text/html");
    });

    server.on("/qr", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/qrgenerator.html", "text/html");
    });

    server.on("/qr", HTTP_POST, [](AsyncWebServerRequest * request)
    {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST text value for qr
          if (p->name() == QR_TEXT) {
            qrText = p->value().c_str();
          }
        }
      }
      prevMillisQR = millis();
      qrGenerate = true;
      request->send(SPIFFS, "/qrgenerator.html", "text/html");
    });

    server.begin();
  }
  else
  {
    // Mode AP
    Serial.println("Setting AP (Access Point)");

    WiFi.softAP("WIFI ESP32", NULL); // → tanpa password

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/config.html", "text/html");
    });

    server.on("/config", HTTP_POST, [](AsyncWebServerRequest * request)
    {
      // JSON Buffer for temporary data
      JsonDocument tempData;

      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == AP_SSID) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Add data to json buffer
            tempData["ssid"] = ssid;
          }
          // HTTP POST pass value
          if (p->name() == AP_PASS) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Add data to json buffer
            tempData["pass"] = pass;
          }
          // HTTP POST ip value
          if (p->name() == AP_IP) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Add data to json buffer
            tempData["ip"] = ip;
          }
          // HTTP POST gateway value
          if (p->name() == AP_GATEWAY) {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Add data to json buffer
            tempData["gateway"] = gateway;
          }
          // HTTP POST tbserver value
          if (p->name() == TB_SERVER) {
            tbserver = p->value().c_str();
            Serial.print("Thingsboard Server set to: ");
            Serial.println(tbserver);
            // Add data to json buffer
            tempData["tbserver"] = tbserver;
          }
          // HTTP POST tbtoken value
          if (p->name() == TB_TOKEN) {
            tbtoken = p->value().c_str();
            Serial.print("Token Device set to: ");
            Serial.println(tbtoken);
            // Add data to json buffer
            tempData["tbtoken"] = tbtoken;
          }
          // HTTP POST api address value
          if (p->name() == API_ADDRESS) {
            apiaddress = p->value().c_str();
            Serial.print("API Address set to: ");
            Serial.println(apiaddress);
            // Add data to json buffer
            tempData["apiaddress"] = apiaddress;
          }
        }
        char tempConfig[200];
        serializeJson(tempData, tempConfig);
        writeFile(SPIFFS, configPath, tempConfig);
      }
      request->send(200, "text/plain", "DONE!!!. ESP akan restart, koneksikan ke router " + ssid + " dan jalankan pada browser IP Address: " + ip);
      delay(3000);
      ESP.restart();
    });

    server.on("/qr", HTTP_GET, [](AsyncWebServerRequest * request)
    {
      request->send(SPIFFS, "/qrgenerator.html", "text/html");
    });

    server.on("/qr", HTTP_POST, [](AsyncWebServerRequest * request)
    {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST text value for qr
          if (p->name() == QR_TEXT) {
            qrText = p->value().c_str();
          }
        }
      }
      prevMillisQR = millis();
      qrGenerate = true;
      request->send(SPIFFS, "/qrgenerator.html", "text/html");
    });

    server.begin();
  }
}

void loop()
{

  if (WiFi.status() != WL_CONNECTED && ssid)
  {
    wifiStatus = "Disconnected";
    if (millis() - previousMillis >= intervalWifi)
    {
      reconnect();
      previousMillis = millis();
    }
  }

  // Read temperature as Celsius (the default)
  String t = readDHTTemperature();
  String h = readDHTHumidity();

  // Check if it is a time to send DHT11 temperature and humidity
  if (millis() - millis_counter > send_delay)
  {
    // Check if temperature and humidity is reaching the limit.
    if (t.toFloat() > tempLimit || h.toFloat() > humidLimit ) {
      Serial.println("Temperature and Humidity limit exceeded");
      isExceeded = true;
    } else {
      isExceeded = false;
    }

    if (!tb.connected())
    {
      // Connect to the ThingsBoard
      Serial.print("Connecting to: ");
      Serial.print(tbserver);
      Serial.print(" with token ");
      Serial.println(tbtoken);

      if (!tb.connect(tbserver.c_str(), tbtoken.c_str(), TB_PORT))
      {
        Serial.println("Failed to connect");
      }
    }
    else
    { // Kirim telemetry jika terkoneksi ke server
      wifiStatus = "Connected";
      Serial.println("Sending data...");

      // JSON Buffer for Temperature
      JsonDocument tempData;
      tempData["temperature"] = t.toFloat();
      tempData["humidity"] = h.toFloat();

      char jsonData[200];
      serializeJson(tempData, jsonData);

      Serial.print("Temperature:");
      Serial.print(t);
      Serial.print(" Humidity ");
      Serial.println(h);
      //      Serial.println(jsonData);

      // Sending telemetry to Thingsboard server
      tb.sendTelemetryJson(jsonData);

      // Add data after sending to Thingsboard server cuz it's getting error while doing that
      tempData["isExceeded"] = true;
      serializeJson(tempData, jsonData);

      HTTPClient http;
      http.begin(wifiClient, apiaddress);

      http.addHeader("Content-Type", "application/json");
      int httpResponseCode = http.POST(jsonData);

      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
    }
    millis_counter = millis(); // reset millis counter
  }

  struct QRCodeData qrCodeData;
  if (reader.receiveQrCode(&qrCodeData, 100))
  {
    Serial.println("Found QRCode");
    if (qrCodeData.valid)
    {
      qrCodePayload = (const char *)qrCodeData.payload;
      Serial.print("Payload: ");
      Serial.println((const char *)qrCodeData.payload);

      drawText(64, 22, TEXT_ALIGN_CENTER, "Scan berhasil", true);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawStringMaxWidth(64, 30, 128, qrCodePayload);
      display.display();
      prevMillis = millis();
    }
    else
    {
      Serial.print("Invalid: ");
      Serial.println((const char *)qrCodeData.payload);
    }
  }

  if (qrGenerate)
  {
    qrcode.create(qrText);
    while (true)
    {
      // Interval untuk menampilkan kode QR adalah 10 detik (10.000 ms)
      if (millis() - prevMillisQR > 10000)
      {
        prevMillisQR = millis();
        qrGenerate = false;
        break;
      }
    }
  }

  // Reset layar OLED sesuai dengan nilai interval 3 detik
  if (millis() - prevMillis > intervalQr)
  {
    prevMillis = millis();
    display.clear();
    drawText(5, 0, TEXT_ALIGN_LEFT, "T : " + t);
    drawText(115, 0, TEXT_ALIGN_RIGHT, "H : " + h);
    drawText(64, 22, TEXT_ALIGN_CENTER, "Arahkan QR ke kamera!!!");
    drawText(5, 52, TEXT_ALIGN_LEFT, "Status : " + wifiStatus);
  }

  // Looping Thingsboard
  tb.loop();
}
