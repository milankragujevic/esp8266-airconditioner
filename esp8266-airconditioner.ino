/* 
  
  esp8266-airconditioner
  https://github.com/milankragujevic/esp8266-airconditioner

  Copyright 2019 Motea Marius, 2020 Milan Kragujević

  This example code will create a webserver that will provide basic control to AC units using the web application
  build with javascript/css. User config zone need to be updated if a different class than Collix need to be used.
  Javasctipt file may also require minor changes as in current version it will not allow to set fan speed if Auto mode
  is selected (required for Coolix protocol).

*/

#include <FS.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#endif

#if defined(ESP32)
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <Update.h>
#endif

#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

/// ###### Begin user configuration ##########

// replace library based on your AC unit model
// for more info, check https://github.com/crankyoldgit/IRremoteESP8266
#include <ir_Coolix.h>

// generic to vendor-specific mappings
#define AUTO_MODE kCoolixAuto
#define COOL_MODE kCoolixCool
#define DRY_MODE kCoolixDry
#define HEAT_MODE kCoolixHeat
#define FAN_MODE kCoolixFan

#define FAN_AUTO kCoolixFanAuto
#define FAN_MIN kCoolixFanMin
#define FAN_MED kCoolixFanMed
#define FAN_HI kCoolixFanMax

// GPIO pin to use for IR LED
const uint16_t kIrLed = 4; // D2

// Library initialization
// change it according to the AC model class
IRCoolixAC ac(kIrLed);

// WiFi SSID during configuration
char deviceName[] = "AC Remote Control";

// default configuration
struct state {
	uint8_t temperature = 22, fan = 0, operation = 0;
	bool powerStatus;
};

/// ##### End user configuration ######

File fsUploadFile;

state acState;

#if defined(ESP8266)
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;
#endif

#if defined(ESP32)
WebServer server(80);
#endif

bool handleFileRead(String path) {
	if (path.endsWith("/")) path += "index.html";
	String contentType = getContentType(path);
	String pathWithGz = path + ".gz";
	if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
		if (SPIFFS.exists(pathWithGz))
			path += ".gz";
		File file = SPIFFS.open(path, "r");
		server.streamFile(file, contentType);
		file.close();
		return true;
	}
	return false;
}

String getContentType(String filename) {
	if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
	return "text/plain";
}

void handleFileUpload() {
	HTTPUpload& upload = server.upload();
	if (upload.status == UPLOAD_FILE_START) {
		String filename = upload.filename;
		if (!filename.startsWith("/")) filename = "/" + filename;
		fsUploadFile = SPIFFS.open(filename, "w");
		filename = String();
	} else if (upload.status == UPLOAD_FILE_WRITE) {
		if (fsUploadFile)
			fsUploadFile.write(upload.buf, upload.currentSize);
	} else if (upload.status == UPLOAD_FILE_END) {
		if (fsUploadFile) {
			fsUploadFile.close();
			server.sendHeader("Location", "/success.html");
			server.send(303);
		} else {
			server.send(500, "text/plain", "500: couldn't create file");
		}
	}
}


void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += (server.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";
	for (uint8_t i = 0; i < server.args(); i++) {
		message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}
	server.send(404, "text/plain", message);
}

void setup() {
	ac.begin();

	delay(1000);
	
	WiFi.disconnect();
	WiFi.softAPdisconnect(true);

	delay(1000);

	if (!SPIFFS.begin()) {
		return;
	}

	WiFiManager wifiManager;

	if (!wifiManager.autoConnect(deviceName)) {
		delay(3000);
		ESP.restart();
		delay(5000);
	}


	#if defined(ESP8266)
		httpUpdateServer.setup(&server);
	#endif

	server.on("/state", HTTP_PUT, []() {
		DynamicJsonDocument root(1024);
		DeserializationError error = deserializeJson(root, server.arg("plain"));
		if (error) {
			server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
		} else {
			if (root.containsKey("temp")) {
				acState.temperature = (uint8_t) root["temp"];
			}

			if (root.containsKey("fan")) {
				acState.fan = (uint8_t) root["fan"];
			}

			if (root.containsKey("power")) {
				acState.powerStatus = root["power"];
			}

			if (root.containsKey("mode")) {
				acState.operation = root["mode"];
			}

			String output;
			serializeJson(root, output);
			server.send(200, "text/plain", output);

			delay(200);

			if (acState.powerStatus) {
				ac.on();
				ac.setTemp(acState.temperature);
				if (acState.operation == 0) {
					ac.setMode(AUTO_MODE);
					ac.setFan(FAN_AUTO);
					acState.fan = 0;
				} else if (acState.operation == 1) {
					ac.setMode(COOL_MODE);
				} else if (acState.operation == 2) {
					ac.setMode(DRY_MODE);
				} else if (acState.operation == 3) {
					ac.setMode(HEAT_MODE);
				} else if (acState.operation == 4) {
					ac.setMode(FAN_MODE);
				}

				if (acState.operation != 0) {
					if (acState.fan == 0) {
						ac.setFan(FAN_AUTO);
					} else if (acState.fan == 1) {
						ac.setFan(FAN_MIN);
					} else if (acState.fan == 2) {
						ac.setFan(FAN_MED);
					} else if (acState.fan == 3) {
						ac.setFan(FAN_HI);
					}
				}
			} else {
				ac.off();
			}
			ac.send();
		}
	});

	server.on("/file-upload", HTTP_POST, []() {
		server.send(200);
	}, handleFileUpload);

	server.on("/file-upload", HTTP_GET, []() {
		String html = "<form method=\"post\" enctype=\"multipart/form-data\">";
		html += "<input type=\"file\" name=\"name\">";
		html += "<input class=\"button\" type=\"submit\" value=\"Upload\">";
		html += "</form>";
		server.send(200, "text/html", html);
	});

	server.on("/", []() {
		server.sendHeader("Location", String("ui.html"), true);
		server.send(302, "text/plain", "");
	});

	server.on("/state", HTTP_GET, []() {
		DynamicJsonDocument root(1024);
		root["mode"] = acState.operation;
		root["fan"] = acState.fan;
		root["temp"] = acState.temperature;
		root["power"] = acState.powerStatus;
		String output;
		serializeJson(root, output);
		server.send(200, "text/plain", output);
	});

	server.on("/reset", []() {
		server.send(200, "text/html", "reset");
		delay(100);
		ESP.restart();
	});

	server.serveStatic("/", SPIFFS, "/", "max-age=86400");

	server.onNotFound(handleNotFound);

	server.begin();
}


void loop() {
	server.handleClient();
}
