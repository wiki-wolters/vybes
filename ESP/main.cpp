#include <ESP8266WiFi.h>

// TODO: Add any other necessary headers

void setup() {
  // Initialize Serial communication
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Serial initialized");

  // TODO: Add Wi-Fi connection logic
  // WiFi.begin("SSID", "password");
  // Serial.println("Connecting to WiFi...");
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   Serial.print(".");
  // }
  // Serial.println("");
  // Serial.println("WiFi connected");
  // Serial.print("IP address: ");
  // Serial.println(WiFi.localIP());

  // TODO: Initialize Web Server
  // For example, using ESP8266WebServer
  // ESP8266WebServer server(80);
  // server.on("/", handleRoot);
  // server.begin();
  // Serial.println("HTTP server started");

  // TODO: Add other setup code here, e.g., initializing sensors or other communication interfaces
}

void loop() {
  // TODO: Handle client requests (if a web server is used)
  // server.handleClient();

  // TODO: Read data from Teensy via Serial or other interface
  // if (Serial.available() > 0) {
  //   String dataFromTeensy = Serial.readStringUntil('\n');
  //   Serial.print("Received from Teensy: ");
  //   Serial.println(dataFromTeensy);
  // }

  // TODO: Send data to Web UI (e.g., via WebSockets or server-sent events)

  // TODO: Add other recurring tasks here
  delay(100); // Small delay to prevent watchdog timeouts, adjust as needed
}

// TODO: Implement any handler functions for the web server or other callbacks here
// void handleRoot() {
//   server.send(200, "text/plain", "Hello from ESP8266!");
// }
