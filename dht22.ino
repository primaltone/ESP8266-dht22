#include <ESP8266WiFi.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include "Timer.h"

// For a connection via I2C using Wire include
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "dht.h"
#include "SSD1306.h"

/* compile config */
#define ENABLE_LIGHT_SENSOR 1
#define ENABLE_WIFI 1

/* pin defines */
#define DHT22_PIN 5
const int lightPin = A0;

/* timer intervals */
#define DISPLAY_UPDATE_INTERVAL_MS	1000
#define TEMPERATURE_UPDATE_INTERVAL_MS	2000
#define LDR_UPDATE_INTERVAL_MS		2100

#define ADC_MAX_VALUE		1023
#define DISPLAY_ON_LIGHT_THRESHOLD 250 
#define DISPLAY_ON_LIGHT_INC	 50
#define SENSOR_MIN_TEMP_C	-40
#define SENSOR_MAX_TEMP_C	80
/* prototypes */
int getTempHumid(void);
int displaytemphumid(int updatedot);

/* enums */
enum DIRECTION {
	L_TO_R,
	R_TO_L
};

enum OLED_MODE {
	OFF,
	ON,
	LIGHT_SENSOR
};

/* globals */
Timer timer;
bool last_good_read = true;
enum DIRECTION dotDir=L_TO_R;
enum OLED_MODE oledMode=ON;
static char fahrenheitTemp[7];
static char humidityTemp[7];
#if ENABLE_LIGHT_SENSOR 
int lightLevel = 0;
int lightThreshold = 0;
int displayEnableThreshold=DISPLAY_ON_LIGHT_THRESHOLD;
#endif
#if ENABLE_WIFI
// Web Server on port 80
WiFiServer server(80);
#endif
// Replace with your network details
const char* HostName = "ESP LR ";
// Initialize the OLED display using Wire library
SSD1306  display(0x3c, 0, 14);

/******************************************************************/
void tempUpdate (void) {
	last_good_read = (getTempHumid() >= 0)?true:false;
}

#if ENABLE_LIGHT_SENSOR 
void LDRUpdate (void) {
	lightLevel = analogRead(lightPin);
	//	Serial.print("light level: ");Serial.println(lightLevel);
}
#endif

int getTempHumid(void) {
#define MAX_TEMP_READ_RETRY 5
	int failCount=0;
	int rc;
	do {
		dht DHT; 
		rc = DHT.read22(DHT22_PIN);

		if (rc == DHTLIB_ERROR_CHECKSUM) {
			Serial.print("Checksum error,\t");
		} 
		else if (rc == DHTLIB_ERROR_TIMEOUT) {
			Serial.print("Time out error,\t");
		}
		else if (rc != DHTLIB_OK ) {
			Serial.print("Unknown error,\t");
		}
		else if ((DHT.humidity < 0) || (DHT.humidity > 100)) {
			Serial.println("Humidity value invalid");
		}
		else if ((DHT.temperature < SENSOR_MIN_TEMP_C) || (DHT.temperature > SENSOR_MAX_TEMP_C)) {
			Serial.println("Temperature outside of sensor operating range");
		}
		else {
			float temp_f = DHT.convertCtoF(DHT.temperature);
			float hif = DHT.computeHeatIndex( temp_f, DHT.humidity, true);

			//		Serial.print("humidity: ");		Serial.println(humidity);
			//		Serial.print("temperature w/ heat indexf: " );    Serial.println(hif);

			dtostrf(hif, 6, 2, fahrenheitTemp);         
			dtostrf(DHT.humidity, 6, 2, humidityTemp);
		}
	} while ((rc < 0) && (++failCount < MAX_TEMP_READ_RETRY));
	return rc;
}

int displayTempHumid(bool updateDot){
	// Reading temperature or humidity takes about 250 milliseconds!
	// Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
	int rc=0;
	char dotString[]="                                ";
	int numDots=sizeof(dotString)/sizeof(dotString[0]);
	static int dotPos=0;

	// Computes temperature values in Celsius + Fahrenheit and Humidity
	display.clear();
	display.drawString(0, 0, "Humidity: " + String(humidityTemp) + "%\t"); 
	display.drawString(0, 16, "Temp: " + String(fahrenheitTemp) + "F"); 

	if (updateDot == true) {
		if ((dotDir == L_TO_R ) && (dotPos++ > numDots)) {
			dotPos=numDots - 2; // want it to be zero-index and one less than last element
			dotDir = R_TO_L;    
		}
		else if ((dotDir == R_TO_L) && (dotPos-- == 0)){
			dotDir = L_TO_R;    
		}
	}
	dotString[dotPos]= '.';

	display.drawString(0, 32, dotString ); 
	display.drawString(0, 48, "IP: " + WiFi.localIP().toString()); 
	display.display();

	return rc;
}

void UpdateDisplayEnable(WiFiClient *client) {
	String request = client->readStringUntil('\r');
	if (request.indexOf("/OLED=ON") != -1)  {
		Serial.println("Enable display");
		oledMode = ON;
	}
	else if (request.indexOf("/OLED=OFF") != -1)  {
		Serial.println("Disable display");
		oledMode = OFF;
	}
	else if (request.indexOf("/OLED=LIGHT_SENSOR") != -1)  {
		Serial.println("Display follows light");
		oledMode = LIGHT_SENSOR;
	} 
	else if (request.indexOf("/OLED=TH_DN") != -1)  {
		if(displayEnableThreshold >= DISPLAY_ON_LIGHT_INC) {
			displayEnableThreshold -= DISPLAY_ON_LIGHT_INC;
		}
		Serial.println("Decrement Threshold");
	} 
	else if (request.indexOf("/OLED=TH_UP") != -1)  {
		if((displayEnableThreshold +  DISPLAY_ON_LIGHT_INC) <= ADC_MAX_VALUE) {
			displayEnableThreshold += DISPLAY_ON_LIGHT_INC;
		}
		else {
			displayEnableThreshold = ADC_MAX_VALUE;
		}
		Serial.println("Increment Threshold");
	} 
}

void ShowDisplayEnable(WiFiClient *client) {
	client->print("<h3>light level: "); client->println(lightLevel);
	client->println("<br><br>");
	client->println("<a href=\"/OLED=ON\"\"><button>Turn On </button></a>");
	client->println("<a href=\"/OLED=OFF\"\"><button>Turn Off </button></a>");  
	client->println("<a href=\"/OLED=LIGHT_SENSOR\"\"><button>Follow Light</button></a><br />");      
	switch (oledMode) {
		case ON:
			client->println("Display Enabled");
			break;
		case OFF:
			client->println("Display Disabled");
			break;
		case LIGHT_SENSOR:
			client->println("<a href=\"/OLED=TH_DN\"\"><button>Decrease Threshold</button></a>");      
			client->println("<a href=\"/OLED=TH_UP\"\"><button>Increase Threshold</button></a><br />");      
			client->print("Display follows light sensor, currently ");
			client->println(lightThreshold?"ON":"OFF");
			client->print("<br />Display Enable Threshold: ");
			client->println(displayEnableThreshold);
			break;
		default:
			break;
	}
}
#if ENABLE_WIFI
void process_web_request(WiFiClient *client){
	Serial.println("New client");
	// bolean to locate when the http request ends
	boolean blank_line = true;

#if ENABLE_LIGHT_SENSOR 
	UpdateDisplayEnable(client);
#endif

	while (client->connected()) {
		if (client->available()) {
			char c = client->read();

			if (c == '\n' && blank_line) {         
				client->println("HTTP/1.1 200 OK");
				client->println("Content-Type: text/html");
				client->println("Connection: close");
				client->println();
				// your actual web page that displays temperature and humidity
				client->println("<!DOCTYPE HTML>");
				client->println("<html>");
				client->print("<head></head><body><h1>");client->print(HostName);client->println(" Temperature and Humidity</h1>");
				client->print("<h3>temperature: "); client->print(fahrenheitTemp);client->print("F humidity: "); client->print(humidityTemp);client->println("%");
#if ENABLE_LIGHT_SENSOR 
				ShowDisplayEnable(client);
#endif            
				client->println("<h3>");
				client->println("</body></html>");     
				break;
			}
			if (c == '\n') {
				// when starts reading a new line
				blank_line = true;
			}
			else if (c != '\r') {
				// when finds a character on the current line
				blank_line = false;
			}
		}
	}  
}

#endif

void displayUpdate(void) {
#if ENABLE_LIGHT_SENSOR 
	lightThreshold=	(lightLevel > displayEnableThreshold)? 1:0;

	if ((oledMode == ON) || ((oledMode == LIGHT_SENSOR) && lightThreshold)) {
		displayTempHumid(last_good_read);
	}
	else { /* if (oledMode == OFF) */
		Serial.println("update display");
		display.clear();
		display.display();
	}
#else
	displayTempHumid(last_good_read);
#endif
}

void loop() {
	static int server_started = 0;
	timer.update();
#if ENABLE_WIFI
	if (!server_started && (WiFi.status() == WL_CONNECTED)) {
		// Starting the web server
		server.begin();
		delay(10000);
		server_started = 1;
	}
	else {
		// Listening for new clients
		WiFiClient client = server.available();
		if (client) {
			process_web_request(&client);
			client.stop();
		}
	}
#endif
	delay(1);
}

void setup() {
	// Initializing serial port for debugging purposes
	Serial.begin(115200);

	// let voltages stabilize after plugging in
	delay(2000);
	// Initialising the UI will init the display too.
	display.init();
	display.flipScreenVertically();
	display.setFont(ArialMT_Plain_16);
	display.setTextAlignment(TEXT_ALIGN_LEFT);

	/* show valid temperature while wating for connection*/
	if (getTempHumid() >= 0) {
		displayTempHumid(true);
	}
#if ENABLE_WIFI
	// Connecting to WifiNetwork
	WiFi.hostname(HostName);
	WiFiManager wifiManager;
	wifiManager.setConfigPortalTimeout(180);
	//first parameter is name of access point, second is the password
	wifiManager.autoConnect("ESP_Setup", "AlphaTango");
#endif
	timer.every(DISPLAY_UPDATE_INTERVAL_MS, displayUpdate);
	timer.every(TEMPERATURE_UPDATE_INTERVAL_MS, tempUpdate);
#if ENABLE_LIGHT_SENSOR 
	timer.every(LDR_UPDATE_INTERVAL_MS, LDRUpdate);
#endif
}
