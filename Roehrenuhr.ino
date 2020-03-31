#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <Timelib.h> 
#include <WiFiUdp.h>
#include <FS.h>

const char* version = "0.0.1.0_alpha";
const char* project = "Roehrenuhr";

const unsigned int localPort = 2390;
IPAddress timeServerIP;
const char* ntpServerName = "de.pool.ntp.org";
WiFiUDP udp;
WiFiManager wifiManager;
// Webserver an Port binden  
ESP8266WebServer server(80);

// Optionen
char timeZone[2] = "1"; // Central European Time							
char twoSegSwitchInterval[6] = "2000"; // in prevTwoSegZwitch
char timeToDateSwitchAktiv[4] = "on"; // 
char timeToDateSwitchInterval[6] = "10000"; // in prevtimeToDateSwitch
char timeToDateSwitchFade[6] = "100"; // in prevtimeToDateSwitch
char displayMode[2] = "6"; // 6 = 6 Elemente / 4 = 4 Elemente / 2 = 2 Elemente
bool timeDisplaying = true;

#define strobePin D0
#define dataPin D5
#define clockPin D6

byte Numer_WithoutDotBytes[] =
{
	// 0
	B00100001,
	// 1
	B11111001,
	// 2
	B00010101,
	// 3 
	B10010001,
	// 4
	B11001001,
	// 5
	B10000011,
	// 6
	B00000011,
	// 7
	B11110001,
	// 8
	B00000001,
	// 9
	B10000001,
	// 10 Blank
	B11111111
};

byte Numer_WithDotBytes[] =
{
	// 0.
	B00100000,
	// 1.
	B11111000,
	// 2.
	B00010100,
	// 3.
	B10010000,
	// 4.
	B11001000,
	// 5.
	B10000010,
	// 6.
	B00000010,
	// 7.
	B11110000,
	// 8.
	B00000000,
	// 9.
	B10000000,
};


// Interne Hilfs-Variablen
time_t prevDisplay = 0;
unsigned long prevTwoSegSwitch = 0;
unsigned long prevtimeToDateSwitch = 0;
byte activSeg = 0; // 0 = Stunden / 1 = Minuten / 2 = Sekunden 
bool shouldSaveConfig = false;
String webString = "";


void setup()
{
	pinMode(strobePin, OUTPUT);
	pinMode(clockPin, OUTPUT);
	pinMode(dataPin, OUTPUT);

	Serial.begin(115200);
	Serial.println();
	Serial.println();

	SetDigits(88, true);
	SetDigits(88, true);
	SetDigits(88, true);

	// Mounting FileSystem
	Serial.println("Mounting file system...");
	if (SPIFFS.begin())
	{
		Serial.println("Mounted file system.");
		LoadConfig();
	}
	else
	{
		Serial.println("Failed to mount FS");
	}

	//set config save notify callback
	wifiManager.setSaveConfigCallback(SaveConfigCallback);

	wifiManager.setMinimumSignalQuality();

	if (!wifiManager.autoConnect(project))
	{
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//if you get here you have connected to the WiFi
	Serial.println("connected...yeey :)");

	SaveConfig();

	Serial.println("local ip");
	Serial.println(WiFi.localIP());
	Serial.println(WiFi.gatewayIP());
	Serial.println(WiFi.subnetMask());

	Serial.println("Starting UDP");
	udp.begin(localPort);
	Serial.print("Local port: ");
	Serial.println(udp.localPort());


	server.on("/", HTTP_GET, Handle_root);
	server.on("/upload", Handle_upload);
	server.on("/update", HTTP_POST, Handle_update, FlashESP);
	server.on("/saveoption", HTTP_POST, Handle_saveoption);
	server.on("/wifisetup", Handle_wifisetup);
	server.on("/factoryreset", Handle_factoryreset);
	server.on("/api/jsonWifiInfo", Handle_jsonWifiInfo);
	server.on("/css/style.css", Handle_styleCSS);
	server.onNotFound(HandleNotFound);

	server.begin();

	MDNS.begin(project);
	MDNS.addService("http", "tcp", 80);

	Serial.println(F("Webserver started"));

	delay(100);
	// Setzte Millis
	prevTwoSegSwitch = millis();
	prevtimeToDateSwitch = millis();
}

void loop()
{
	if (timeStatus() != timeNotSet)
	{
		if (now() != prevDisplay)
		{
			prevDisplay = now();

			if (String(timeToDateSwitchInterval) != "0" && String(displayMode) == "6")
			{
				int switchTime = String(timeToDateSwitchInterval).toInt();

				if (!timeDisplaying)
				{
					switchTime = (switchTime / 3);
				}

				if (millis() - prevtimeToDateSwitch > switchTime)
				{
					ChangeTimeOrDate();
					prevtimeToDateSwitch = millis();
				}
				else if (timeDisplaying)
				{
					SetDisplayTime();
				}
				else
				{
					SetDisplayDate();
				}
			}
			else
			{
				SetDisplayTime();
			}
		}
	}
	else
	{
		WiFi.hostByName(ntpServerName, timeServerIP);
		Serial.println("waiting for sync");
		setSyncProvider(getNtpTime);
	}

	server.handleClient();
}

void ChangeTimeOrDate()
{
	FadeOut();

	if (timeDisplaying)
	{
		SetDigits(day(), true);
		SetDigits(-1, false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(day(), true);
		SetDigits(month(), true);
		SetDigits(-1, false);

		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(day(), true);
		SetDigits(month(), true);
		SetDigits(ShortYear(year()), false);

		timeDisplaying = false;
	}
	else
	{
		SetDigits(GetSummOrWinterHour(), false);
		SetDigits(-1, false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(GetSummOrWinterHour(), false);
		SetDigits(minute(), false);
		SetDigits(-1, false);

		delay(String(timeToDateSwitchFade).toInt());
		SetDigits(GetSummOrWinterHour(), false);
		SetDigits(minute(), false);
		SetDigits(second(), false);
		timeDisplaying = true;
	}
}

void FadeOut()
{
	if (timeDisplaying)
	{
		SetDigits(GetSummOrWinterHour(), false);
		SetDigits(minute(), false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(GetSummOrWinterHour(), false);
		SetDigits(-1, false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(-1, false);
		SetDigits(-1, false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());
	}
	else
	{
		SetDigits(day(), true);
		SetDigits(month(), true);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(day(), true);
		SetDigits(-1, false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());

		SetDigits(-1, false);
		SetDigits(-1, false);
		SetDigits(-1, false);
		delay(String(timeToDateSwitchFade).toInt());
	}
}

void SetDisplayDate()
{
	SetDigits(day(), true);
	SetDigits(month(), true);
	SetDigits(ShortYear(year()), false);
}

void SetDisplayTime()
{
	switch (String(displayMode).toInt())
	{
	case 2:
		TwoMode();
		break;
	case 4:
		FourMode();
		break;
	case 6:
		SixMode();
		break;
	}
}

void TwoMode()
{
	switch (activSeg)
	{
	case 0:
		SetDigits(GetSummOrWinterHour(), false);
		break;
	case 1:
		SetDigits(minute(), false);
		break;
	case 2:
		SetDigits(second(), false);
		break;
	}


	if (millis() - prevTwoSegSwitch > String(twoSegSwitchInterval).toInt())
	{
		prevTwoSegSwitch = millis();

		if (activSeg == 2)
		{
			activSeg = 0;
		}
		else
		{
			activSeg++;
		}
	}
}

void FourMode()
{
	SetDigits(GetSummOrWinterHour(), false);
	SetDigits(minute(), false);
}

void SixMode()
{
	FourMode();
	SetDigits(second(), false);
}

String DigitsFormat(int digits)
{

	String result = String(digits);
	if (digits < 10)
	{
		result = "0" + result;
	}
	return result;
}

int ShortYear(int digits)
{
	return String(digits).substring(2, 4).toInt();
}

void SetDigits(int digits, bool dot)
{
	int test[2];
	if (digits != -1)
	{
		test[0] = DigitsFormat(digits).substring(0, 1).toInt();
		test[1] = DigitsFormat(digits).substring(1, 2).toInt();
	}
	else
	{
		test[0] = 10;
		test[1] = 10;
	}

	for (int i = 1; i > -1; i--)
	{
		digitalWrite(strobePin, LOW);

		if (dot && i == 1)
		{
			shiftOut(dataPin, clockPin, LSBFIRST, Numer_WithDotBytes[test[i]]);
		}
		else
		{
			shiftOut(dataPin, clockPin, LSBFIRST, Numer_WithoutDotBytes[test[i]]);
		}

		digitalWrite(strobePin, HIGH);
	}
}

int GetSummOrWinterHour()
{
	bool summerTime;

	if (month() < 3 || month() > 10)
	{
		summerTime = false; // keine Sommerzeit in Jan, Feb, Nov, Dez
	}
	else if (month() > 3 && month() < 10) {
		summerTime = true; // Sommerzeit in Apr, Mai, Jun, Jul, Aug, Sep
	}
	else if (month() == 3 && (hour() + 24 * day()) >= (1 + String(timeZone).toInt() + 24 * (31 - (5 * year() / 4 + 4) % 7)) || month() == 10 && (hour() + 24 * day()) < (1 + String(timeZone).toInt() + 24 * (31 - (5 * year() / 4 + 1) % 7)))
	{
		summerTime = true;
	}
	else
	{
		summerTime = false;
	}

	if (summerTime)
	{
		int temp = hour() + 1;

		if (temp == 24)
		{
			temp = 00;
		}

		return temp;
	}
	else
	{
		int temp = hour();

		if (temp == 24)
		{
			temp = 00;
		}

		return temp;
	}
}

void SaveConfigCallback()
{
	Serial.println("Should save config");
	shouldSaveConfig = true;
}

void SaveConfig()
{
	//save the custom parameters to FS
	if (shouldSaveConfig) {
		Serial.println("saving config");
		DynamicJsonBuffer jsonBuffer;
		JsonObject& json = jsonBuffer.createObject();
		json["timeZone"] = timeZone;
		json["twoSegSwitchInterval"] = twoSegSwitchInterval;
		json["displayMode"] = displayMode;
		json["timeToDateSwitchFade"] = timeToDateSwitchFade;
		json["timeToDateSwitchInterval"] = timeToDateSwitchInterval;
		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile)
		{
			Serial.println("failed to open config file for writing");
		}
		json.prettyPrintTo(Serial);
		json.printTo(configFile);
		configFile.close();
		//end save
	}
}

void LoadConfig()
{
	if (SPIFFS.exists("/config.json"))
	{
		//file exists, reading and loading
		Serial.println("reading config file");
		File configFile = SPIFFS.open("/config.json", "r");

		if (configFile)
		{
			Serial.println("opened config file");
			size_t size = configFile.size();
			// Allocate a buffer to store contents of the file.
			std::unique_ptr<char[]> buf(new char[size]);

			configFile.readBytes(buf.get(), size);
			DynamicJsonBuffer jsonBuffer;
			JsonObject& json = jsonBuffer.parseObject(buf.get());
			json.printTo(Serial);

			if (json.success())
			{
				Serial.println("\nparsed json");

				if (json["timeZone"] != NULL)
				{
					strcpy(timeZone, json["timeZone"]);
				}

				if (json["twoSegSwitchInterval"] != NULL)
				{
					strcpy(twoSegSwitchInterval, json["twoSegSwitchInterval"]);
				}

				if (json["displayMode"] != NULL)
				{
					strcpy(displayMode, json["displayMode"]);
				}

				if (json["timeToDateSwitchInterval"] != NULL)
				{
					strcpy(timeToDateSwitchInterval, json["timeToDateSwitchInterval"]);
				}

				if (json["timeToDateSwitchEnabled"] != NULL)
				{
					strcpy(timeToDateSwitchFade, json["timeToDateSwitchFade"]);
				}
			}
			else
			{
				Serial.println("failed to load json config");
			}
		}
	}
}

void Handle_root()
{
	int rssi = WiFi.RSSI();

	//File html = SPIFFS.open("/root.html", "r");
	webString = GetRootHTML();

	webString.replace("@VERS", version);
	webString.replace("@SSID", WiFi.SSID());
	webString.replace("@Qualiy", String(GetRSSIasQuality(rssi)));
	webString.replace("@SIGNAL", String(rssi));
	webString.replace("@TIMEZONE", timeZone);
	webString.replace("@TWOSEGSWITCHINTERVAL", twoSegSwitchInterval);
	webString.replace("@DISPLAYMODE", displayMode);
	webString.replace("@TIMETODATEINTERVAL", timeToDateSwitchInterval);
	webString.replace("@TIMETODATEFADE", timeToDateSwitchFade);

	server.sendHeader("Connection", "close");
	server.send(200, "text/html", webString);
}

void Handle_upload()
{
	server.sendHeader("Connection", "close");
	server.send(200, "text/html", GetUploadHTML());
}

void Handle_config()
{

}

void Handle_update()
{
	server.sendHeader("Connection", "close");
	server.sendHeader("Access-Control-Allow-Origin", "*");
	bool error = Update.hasError();
	server.send(200, "application/json", (error) ? "{\"success\":false}" : "{\"success\":true}");

	if (!error)
	{
		ESP.restart();
	}
}

void Handle_saveoption()
{

	strcpy(timeZone, server.arg("timeZone").c_str());
	strcpy(twoSegSwitchInterval, server.arg("twoSegSwitchInterval").c_str());
	strcpy(displayMode, server.arg("displayMode").c_str());
	strcpy(timeToDateSwitchFade, server.arg("timeToDateSwitchFade").c_str());
	strcpy(timeToDateSwitchInterval, server.arg("timeToDateSwitchInterval").c_str());

	SaveConfigCallback();
	SaveConfig();
	setSyncProvider(getNtpTime);

	server.sendHeader("Connection", "close");
	server.sendHeader("Location", String("http://roehrenuhr.local/"), true);
	server.send(302, "text/plain", "");
}

void Handle_factoryreset()
{
	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile)
	{
		Serial.println("failed to open config file for reset");
	}
	configFile.println("");
	configFile.close();
	WifiSetup();
	ESP.restart();
}

void Handle_jsonWifiInfo()
{
	server.sendHeader("Connection", "close");
	server.send(200, "application/json", CreateJsonString());
}

void Handle_wifisetup()
{
	WifiSetup();
}

void Handle_styleCSS()
{
	server.sendHeader("Connection", "close");
	server.send(200, "text/css", GetStyleCSS());
}

void HandleNotFound()
{
	server.sendHeader("Connection", "close");
	server.send(404);
	//loadFromSpiffs(server.uri());
}

//void loadFromSpiffs(String path)
//{
//	String dataType = "text/plain";

//	if (path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
//	else if (path.endsWith(".htm")) dataType = "text/html";
//	else if (path.endsWith(".css")) dataType = "text/css";
//	else if (path.endsWith(".js")) dataType = "application/javascript";
//	else if (path.endsWith(".png")) dataType = "image/png";
//	else if (path.endsWith(".gif")) dataType = "image/gif";
//	else if (path.endsWith(".jpg")) dataType = "image/jpeg";
//	else if (path.endsWith(".ico")) dataType = "image/x-icon";
//	else if (path.endsWith(".xml")) dataType = "text/xml";
//	else if (path.endsWith(".pdf")) dataType = "application/pdf";
//	else if (path.endsWith(".zip")) dataType = "application/zip";

//	File dataFile = SPIFFS.open(path, "r");

//	if (server.hasArg("download"))
//	{
//		dataType = "application/octet-stream";
//	}

//	if (server.streamFile(dataFile, dataType) != dataFile.size()) {}
//	dataFile.close();
//}

void WifiSetup()
{
	wifiManager.resetSettings();
	ESP.restart();
	delay(300);
}

int GetRSSIasQuality(int RSSI)
{
	int quality = 0;

	if (RSSI <= -100)
	{
		quality = 0;
	}
	else if (RSSI >= -50)
	{
		quality = 100;
	}
	else
	{
		quality = 2 * (RSSI + 100);
	}
	return quality;
}

void FlashESP()
{
	HTTPUpload& upload = server.upload();
	if (upload.status == UPLOAD_FILE_START)
	{
		Serial.setDebugOutput(true);
		WiFiUDP::stopAll();
		Serial.printf("Update: %s\n", upload.filename.c_str());
		uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
		//start with max available size
		if (!Update.begin(maxSketchSpace))
		{

			Update.printError(Serial);
		}
	}
	else if (upload.status == UPLOAD_FILE_WRITE)
	{
		if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
		{
			Update.printError(Serial);
		}
	}
	else if (upload.status == UPLOAD_FILE_END)
	{
		//true to set the size to the current progress
		if (Update.end(true))
		{
			Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
		}
		else
		{
			Update.printError(Serial);
		}
		Serial.setDebugOutput(true);
	}
	yield();
}

String CreateJsonString()
{
	int rssi = WiFi.RSSI();
	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();

	json["RSSI"] = rssi;
	json["WifiQuality"] = GetRSSIasQuality(rssi);

	String result;
	json.printTo(result);

	return result;
}


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

time_t getNtpTime()
{
	while (udp.parsePacket() > 0);
	Serial.println("Transmit NTP Request");
	sendNTPpacket(timeServerIP);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 1500)
	{
		int size = udp.parsePacket();
		if (size >= NTP_PACKET_SIZE)
		{
			Serial.println("Receive NTP Response");
			udp.read(packetBuffer, NTP_PACKET_SIZE);
			unsigned long secsSince1900;

			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			return secsSince1900 - 2208988800UL + String(timeZone).toInt() * SECS_PER_HOUR;
		}
	}
	Serial.println("No NTP Response :-(");
	return 0;
}

void sendNTPpacket(IPAddress &address)
{
	memset(packetBuffer, 0, NTP_PACKET_SIZE);

	packetBuffer[0] = 0b11100011;
	packetBuffer[1] = 0;
	packetBuffer[2] = 6;
	packetBuffer[3] = 0xEC;

	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;

	udp.beginPacket(address, 123);
	udp.write(packetBuffer, NTP_PACKET_SIZE);
	udp.endPacket();
}

String GetRootHTML()
{
	return F("<!DOCTYPE html>"
		"<html lang=\"de\" class=\"no-js\">"
		"<head>"
		"<meta charset=\"utf-8\">"
		"<title>R&ouml;hrenuhr - Konfiguration</title>"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\" />"
		"<link href=\"/css/style.css\" rel=\"stylesheet\">"
		"</head>"
		"<body>"
		"<div class=\"container\">"
		"<h1><b>R&ouml;hrenuhr - Konfiguration</b></h1>"
		"<form method=\"post\" action=\"/saveoption\" novalidate class=\"boxu\">"
		"<div>"
		"<span class=\"pull-left\">"
		"<b>Version:</b> <span id=\"version\">@VERS</span>"
		"</span>"
		"<span class=\"pull-right\">"
		"<b>SSID:</b> <span id=\"ssid\">@SSID</span>"
		"</span>"
		"</div>"
		"<br />"
		"<div>"
		"<span class=\"pull-right\">"
		"<b>Quality:</b> <span id=\"signalstrength\">@Qualiy % (@SIGNAL)</span>"
		"</span>"
		"</div>"
		"<br />"
		"<hr class=\"dashed\">"
		"<div>"
		"<label for=\"timeZone\">Zeitzone:</label><br />"
		"<select name=\"timeZone\" id=\"timeZone\">"
		"<option value=\"1\">+1 Central European</option>"
		"<option value=\"-5\">-5 Eastern Standard</option>"
		"<option value=\"-4\">-4 Eastern Daylight</option>"
		"<option value=\"-8\">-8 Pacific Standard</option>"
		"<option value=\"-7\">-7 Pacific Daylight</option>"
		"</select>"
		"<br />"
		"<label for=\"displayMode\">Anzahl der Segmente:</label><br />"
		"<select name=\"displayMode\" id=\"displayMode\" onchange=\"twoSegSwitchIntervallActive()\">"
		"<option value=\"2\">2 R&ouml;hren</option>"
		"<option value=\"4\">4 R&ouml;hren</option>"
		"<option value=\"6\">6 R&ouml;hren</option>"
		"</select>"
		"<br />"
		"<label for=\"twoSegSwitchIntervall\">2 Seg Wechselinterv.:</label><br />"
		"<select name=\"twoSegSwitchInterval\" id=\"twoSegSwitchInterval\">"
		"<option value=\"2000\">2 Sekunden</option>"
		"<option value=\"4000\">4 Sekunden</option>"
		"<option value=\"6000\">6 Sekunden</option>"
		"<option value=\"8000\">8 Sekunden</option>"
		"<option value=\"10000\">10 Sekunden</option>"
		"</select>"
		"<br />"
		"<label for=\"timeToDateSwitchInterval\">Datum(⅓)/Zeit Wechselinterv.:</label><br />"
		"<select name=\"timeToDateSwitchInterval\" id=\"timeToDateSwitchInterval\" onchange=\"timeToDateSwitchFadeActive()\">"
		"<option value=\"0\">Aus</option>"
		"<option value=\"2000\">2 Sekunden</option>"
		"<option value=\"4000\">4 Sekunden</option>"
		"<option value=\"6000\">6 Sekunden</option>"
		"<option value=\"8000\">8 Sekunden</option>"
		"<option value=\"10000\">10 Sekunden</option>"
		"<option value=\"20000\">20 Sekunden</option>"
		"<option value=\"30000\">30 Sekunden</option>"
		"<option value=\"40000\">40 Sekunden</option>"
		"<option value=\"50000\">50 Sekunden</option>"
		"<option value=\"60000\">60 Sekunden</option>"
		"</select>"
		"<br />"
		"<label for=\"timeToDateSwitchFade\">Datum/Zeit Fadespeed:</label><br />"
		"<select name=\"timeToDateSwitchFade\" id=\"timeToDateSwitchFade\">"
		"<option value=\"50\">50 Milisek.</option>"
		"<option value=\"100\">100 Millisek.</option>"
		"<option value=\"200\">200 Millisek.</option>"
		"<option value=\"300\">300 Millisek.</option>"
		"<option value=\"400\">400 Millisek.</option>"
		"<option value=\"500\">500 Millisek.</option>"
		"<option value=\"600\">600 Millisek.</option>"
		"<option value=\"700\">700 Millisek.</option>"
		"</select>"
		"</div>"
		"<hr>"
		"<button type=\"submit\" class=\"btn\">Speichern</button>"
		"&emsp;"
		"<button type=\"button\" class=\"btn\" onclick=\"location.href = '/factoryreset'\">Zur&uuml;cksetzten</button>"
		"&emsp;"
		"<button type=\"button\" class=\"btn\" onclick=\"location.href = '/wifisetup'\">Wifi Setup</button>"
		"&emsp;"
		"<button type=\"button\" class=\"btn\" onclick=\"location.href = '/upload'\">Firmware-Update</button>"
		"</form>"
		"</div>"
		"<script>"
		"function twoSegSwitchIntervallActive() {"
		"document.getElementById('twoSegSwitchInterval').disabled = document.getElementById('displayMode').value != '2';"
		"};"
		"function timeToDateSwitchFadeActive() {"
		"document.getElementById('timeToDateSwitchFade').disabled = !document.getElementById('timeToDateSwitchInterval').value != '0';"
		"};"
		"document.addEventListener('DOMContentLoaded', function () {"
		"document.getElementById('displayMode').value = '@DISPLAYMODE';"
		"document.getElementById('twoSegSwitchInterval').value = '@TWOSEGSWITCHINTERVAL';"
		"document.getElementById('timeZone').value = '@TIMEZONE';"
		"document.getElementById('timeToDateSwitchInterval').value = '@TIMETODATEINTERVAL';"
		"document.getElementById('timeToDateSwitchFade').value = '@TIMETODATEFADE';"
		"twoSegSwitchIntervallActive();"
		"timeToDateSwitchActive();"
		"setInterval(function () { loadXMLDoc() }, 3000);"
		"function loadXMLDoc() {"
		"var xmlhttp = new XMLHttpRequest();"
		"xmlhttp.onreadystatechange = function () {"
		"if (xmlhttp.readyState == XMLHttpRequest.DONE) {"
		"if (xmlhttp.status == 200) {"
		"var data = JSON.parse(xmlhttp.responseText);"
		"document.getElementById('signalstrength').innerText = data.WifiQuality + ' %' + ' (' + data.RSSI + ')';"
		"}"
		"}"
		"};"
		"xmlhttp.open(\"GET\", \"/api/jsonWifiInfo\", true);"
		"xmlhttp.send();"
		"}"
		"}, false);"
		"</script>"
		"</body>"
		"</html>");

}

String GetUploadHTML()
{
	return F("<!DOCTYPE html>"
		"<html lang=\"en\" class=\"no-js\">"
		"<head>"
		"<meta charset=\"utf-8\">"
		"<title>R&ouml;hrenuhr - Firmware-Update</title>"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\" />"
		"<link href=\"/css/style.css\" rel=\"stylesheet\">"
		"</head>"
		"<body>"
		"<div class=\"container\" role=\"main\">"
		"<h1><b>R&ouml;hrenuhr - Firmware-Update</b></h1>"
		"<form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\" novalidate class=\"box\">"
		"<div class=\"box__input\">"
		"<svg class=\"box__icon\" xmlns=\"http://www.w3.org/2000/svg\" width=\"50\" height=\"43\" viewBox=\"0 0 50 43\"><path d=\"M48.4 26.5c-.9 0-1.7.7-1.7 1.7v11.6h-43.3v-11.6c0-.9-.7-1.7-1.7-1.7s-1.7.7-1.7 1.7v13.2c0 .9.7 1.7 1.7 1.7h46.7c.9 0 1.7-.7 1.7-1.7v-13.2c0-1-.7-1.7-1.7-1.7zm-24.5 6.1c.3.3.8.5 1.2.5.4 0 .9-.2 1.2-.5l10-11.6c.7-.7.7-1.7 0-2.4s-1.7-.7-2.4 0l-7.1 8.3v-25.3c0-.9-.7-1.7-1.7-1.7s-1.7.7-1.7 1.7v25.3l-7.1-8.3c-.7-.7-1.7-.7-2.4 0s-.7 1.7 0 2.4l10 11.6z\" /></svg>"
		"<input type=\"file\" name=\"update\" id=\"file\" class=\"box__file\" />"
		"<label for=\"file\">"
		"<strong>W&auml;hle eine Firmware Datei aus</strong>"
		"<br /><span class=\"\"> oder lasse sie hier fallen</span>."
		"</label>"
		"<button type=\"submit\" class=\"box__button\">Upload</button>"
		"</div>"
		"<div class=\"box__uploading\">Hochladen&hellip;</div>"
		"<div class=\"box__success\">"
		"Fertig!<br />"
		"R&ouml;hrenuhr startet neu...<br />"
		"<progress value=\"0\" max=\"15\" id=\"progressBar\"></progress>"
		"</div>"
		"<div class=\"box__error\">Error! <span></span>.</div>"
		"</form>"
		"<footer></footer>"
		"</div>"
		"<script>"
		"function reboot() {"
		"var timeleft = 15;"
		"var downloadTimer = setInterval(function () {"
		"document.getElementById(\"progressBar\").value = 15 - --timeleft;"
		"if (timeleft <= 0) {"
		"clearInterval(downloadTimer);"
		"window.location.href = \"/\";"
		"}"
		"}, 1000);"
		"}"
		"'use strict';"
		"; (function (document, window, index) {"
		"var isAdvancedUpload = function () {"
		"var div = document.createElement('div');"
		"return (('draggable' in div) || ('ondragstart' in div && 'ondrop' in div)) && 'FormData' in window && 'FileReader' in window;"
		"}();"
		"var forms = document.querySelectorAll('.box');"
		"Array.prototype.forEach.call(forms, function (form) {"
		"var input = form.querySelector('input[type=\"file\"]'),"
		"label = form.querySelector('label'),"
		"errorMsg = form.querySelector('.box__error span'),"
		"restart = form.querySelectorAll('.box__restart'),"
		"droppedFiles = false,"
		"showFiles = function (files) {"
		"label.textContent = files.length > 1 ? (input.getAttribute('data-multiple-caption') || '').replace('{count}', files.length) : files[0].name;"
		"},"
		"triggerFormSubmit = function () {"
		"var event = document.createEvent('HTMLEvents');"
		"event.initEvent('submit', true, false);"
		"form.dispatchEvent(event);"
		"};"
		"var ajaxFlag = document.createElement('input');"
		"ajaxFlag.setAttribute('type', 'hidden');"
		"ajaxFlag.setAttribute('name', 'ajax');"
		"ajaxFlag.setAttribute('value', 1);"
		"form.appendChild(ajaxFlag);"
		"input.addEventListener('change', function (e) {"
		"showFiles(e.target.files);"
		"triggerFormSubmit();"
		"});"
		"if (isAdvancedUpload) {"
		"form.classList.add('has-advanced-upload');"
		"['drag', 'dragstart', 'dragend', 'dragover', 'dragenter', 'dragleave', 'drop'].forEach(function (event) {"
		"form.addEventListener(event, function (e) {"
		"e.preventDefault();"
		"e.stopPropagation();"
		"});"
		"});"
		"['dragover', 'dragenter'].forEach(function (event) {"
		"form.addEventListener(event, function () {"
		"form.classList.add('is-dragover');"
		"});"
		"});"
		"['dragleave', 'dragend', 'drop'].forEach(function (event) {"
		"form.addEventListener(event, function () {"
		"form.classList.remove('is-dragover');"
		"});"
		"});"
		"form.addEventListener('drop', function (e) {"
		"droppedFiles = e.dataTransfer.files;"
		"showFiles(droppedFiles);"
		"triggerFormSubmit();"
		"});"
		"}"
		"form.addEventListener('submit', function (e) {"
		"if (form.classList.contains('is-uploading')) return false;"
		"form.classList.add('is-uploading');"
		"form.classList.remove('is-error');"
		"if (isAdvancedUpload) {"
		"e.preventDefault();"
		"var ajaxData = new FormData(form);"
		"if (droppedFiles) {"
		"Array.prototype.forEach.call(droppedFiles, function (file) {"
		"ajaxData.append(input.getAttribute('name'), file);"
		"});"
		"}"
		"var ajax = new XMLHttpRequest();"
		"ajax.open(form.getAttribute('method'), form.getAttribute('action'), true);"
		"ajax.onload = function () {"
		"form.classList.remove('is-uploading');"
		"if (ajax.status >= 200 && ajax.status < 400) {"
		"var data = JSON.parse(ajax.responseText);"
		"form.classList.add(data.success == true ? 'is-success' : 'is-error');"
		"if (!data.success) {"
		"errorMsg.textContent = data.error;"
		"}"
		"else {"
		"reboot();"
		"}"
		"}"
		"else alert('Error. Please, contact the webmaster!');"
		"};"
		"ajax.onerror = function () {"
		"form.classList.remove('is-uploading');"
		"alert('Error. Please, try again!');"
		"};"
		"ajax.send(ajaxData);"
		"}"
		"else {"
		"var iframeName = 'uploadiframe' + new Date().getTime(),"
		"iframe = document.createElement('iframe');"
		"$iframe = $('<iframe name=\"' + iframeName + '\" style=\"display: none;\"></iframe>');"
		"iframe.setAttribute('name', iframeName);"
		"iframe.style.display = 'none';"
		"document.body.appendChild(iframe);"
		"form.setAttribute('target', iframeName);"
		"iframe.addEventListener('load', function () {"
		"var data = JSON.parse(iframe.contentDocument.body.innerHTML);"
		"form.classList.remove('is-uploading');"
		"form.classList.add(data.success == true ? 'is-success' : 'is-error');"
		"form.removeAttribute('target');"
		"if (!data.success) {"
		"errorMsg.textContent = data.error;"
		"}"
		"else {"
		"reboot();"
		"}"
		"iframe.parentNode.removeChild(iframe);"
		"});"
		"}"
		"});"
		"Array.prototype.forEach.call(restart, function (entry) {"
		"entry.addEventListener('click', function (e) {"
		"e.preventDefault();"
		"form.classList.remove('is-error', 'is-success');"
		"input.click();"
		"});"
		"});"
		"input.addEventListener('focus', function () { input.classList.add('has-focus'); });"
		"input.addEventListener('blur', function () { input.classList.remove('has-focus'); });"
		"});"
		"}(document, window, 0));"
		"</script>"
		"<script>(function (e, t, n) { var r = e.querySelectorAll(\"html\")[0]; r.className = r.className.replace(/(^|\s)no-js(\s|$)/, \"$1js$2\") })(document, window, 0);</script>"
		"</body>"
		"</html>");
}

String GetStyleCSS()
{
	return F("body {"
		"/*padding-top: 60px;*/"
		"/*padding-bottom: 60px;*/"
		"background: #333;"
		"color: #adadad;"
		"}"
		"tr {"
		"height: 35px !important;"
		"}"
		"table {"
		"min-height: 390px !important;"
		"}"
		"hr {"
		"border: none;"
		"height: 2px;"
		"background-color: #272727; /* Modern Browsers */"
		"}"
		".dashed {"
		"border-bottom: 2px dashed #737373;"
		"background-color: #474747 !important;"
		"}"
		".pull-right {"
		"float: right;"
		"}"
		".pull-left {"
		"float: left;"
		"}"
		".btn {"
		"border-radius: 8px;"
		"font-family: Arial;"
		"color: #adadad;"
		"font-size: 16px;"
		"background: #252525;"
		"padding: 7px 12px 7px 12px;"
		"border: solid #3D3D3D 2px;"
		"text-decoration: none;"
		"}"
		".btn:hover {"
		"background: #797979;"
		"text-decoration: none;"
		"}"
		"h1, .gauge, .nav {"
		"text-align: center;"
		"}"
		".container {"
		"width: 100%;"
		"max-width: 680px; /* 800 */"
		"text-align: center;"
		"margin: 0 auto;"
		"}"
		".boxu {"
		"font-size: 1.25rem; /* 20 */"
		"background-color: #474747;"
		"position: relative;"
		"outline: 2px dashed #737373;"
		"outline-offset: -10px;"
		"padding: 20px 20px;"
		"}"
		".box {"
		"font-size: 1.25rem; /* 20 */"
		"background-color: #474747;"
		"position: relative;"
		"padding: 100px 20px;"
		"}"
		".box.has-advanced-upload {"
		"outline: 2px dashed #737373;"
		"outline-offset: -10px;"
		"}"
		".box.is-dragover {"
		"outline-color: #3D3D3D;"
		"background-color: #737373;"
		"}"
		".box__dragndrop,"
		".box__icon {"
		"display: none;"
		"}"
		".box.has-advanced-upload .box__icon {"
		"width: 100%;"
		"height: 80px;"
		"fill: #737373;"
		"display: block;"
		"margin-bottom: 40px;"
		"}"
		".box.is-uploading .box__input,"
		".box.is-success .box__input,"
		".box.is-error .box__input {"
		"visibility: hidden;"
		"}"
		".box__uploading,"
		".box__success,"
		".box__error {"
		"display: none;"
		"}"
		".box.is-uploading .box__uploading,"
		".box.is-success .box__success,"
		".box.is-error .box__error {"
		"display: block;"
		"position: absolute;"
		"top: 50%;"
		"right: 0;"
		"left: 0;"
		"transform: translateY( -50% );"
		"}"
		".box__uploading {"
		"font-style: italic;"
		"}"
		".box__success {"
		"-webkit-animation: appear-from-inside .25s ease-in-out;"
		"animation: appear-from-inside .25s ease-in-out;"
		"}"
		".box__restart {"
		"font-weight: 700;"
		"}"
		".js .box__file {"
		"width: 0.1px;"
		"height: 0.1px;"
		"opacity: 0;"
		"overflow: hidden;"
		"position: absolute;"
		"z-index: -1;"
		"}"
		".js .box__file + label {"
		"max-width: 80%;"
		"text-overflow: ellipsis;"
		"white-space: nowrap;"
		"cursor: pointer;"
		"display: inline-block;"
		"overflow: hidden;"
		"}"
		".js .box__file + label:hover strong,"
		".box__file:focus + label strong,"
		".box__file.has-focus + label strong {"
		"color: #797979;"
		"}"
		".js .box__file:focus + label,"
		".js .box__file.has-focus + label {"
		"outline: 1px dotted #000;"
		"outline: -webkit-focus-ring-color auto 5px;"
		"}"
		".no-js .box__file + label {"
		"display: none;"
		"}"
		".no-js .box__button {"
		"display: block;"
		"}"
		".box__button {"
		"font-weight: 700;"
		"color: #e5edf1;"
		"background-color: #39bfd3;"
		"display: none;"
		"padding: 8px 16px;"
		"margin: 40px auto 0;"
		"}"
		".box__button:hover,"
		".box__button:focus {"
		"background-color: #0f3c4b;"
		"}"
		"select {"
		"border: 1px solid #d6d8db;"
		"background-color: #ecedee;"
		"font-weight: bold;"
		"color: #47515c;"
		"text-align-last: center;"
		"width: 25%;"
		"cursor: pointer;"
		"margin-bottom: 10px;"
		"}"
		"select > option {"
		"text-align-: center;"
		"}"
		"select:disabled {"
		"background: #858585;"
		"}");
}
