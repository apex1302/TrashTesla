#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>

//-------------------------------------------------------------------------------
// WiFi credentials
//-------------------------------------------------------------------------------
const char* ssid = "SSID";
const char* password = "KEY";

//-------------------------------------------------------------------------------
// NTP Configuration
//-------------------------------------------------------------------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); // Offset is 3600 seconds for Berlin time (UTC+1)

//-------------------------------------------------------------------------------
// CC1101 Pins
//-------------------------------------------------------------------------------
#define LED_PIN 17
#define CC1101_GDO0_PIN 16
#define CC1101_CS_PIN 5
#define CC1101_SCK_PIN 18
#define CC1101_MOSI_PIN 23
#define CC1101_MISO_PIN 19

//-------------------------------------------------------------------------------
// 433 MHz Transmission Settings
//-------------------------------------------------------------------------------
const uint16_t pulseWidth = 400; // Microseconds
const uint8_t transmissions = 5; // Number of times to repeat the signal
const uint8_t sequence[] = {
    0x02, 0xAA, 0xAA, 0xAA, 0x2B, 0x2C, 0xCB, 0x33, 0x33, 0x2D, 0x34, 0xB5,
    0x2B, 0x4D, 0x32, 0xAD, 0x2C, 0x56, 0x59, 0x96, 0x66, 0x66, 0x5A, 0x69,
    0x6A, 0x56, 0x9A, 0x65, 0x5A, 0x58, 0xAC, 0xB3, 0x2C, 0xCC, 0xCC, 0xB4,
    0xD2, 0xD4, 0xAD, 0x34, 0xCA, 0xB4, 0xA0
};
const uint8_t messageLength = sizeof(sequence);

//-------------------------------------------------------------------------------
// Appointment Dates
//-------------------------------------------------------------------------------
String* appointmentDates = nullptr; // Dynamically allocated array for appointments
uint8_t appointmentCount = 0; // Counter to track number of appointments

//-------------------------------------------------------------------------------
// Function to download and parse the ICS calendar
//-------------------------------------------------------------------------------
void fetchAppointments() {
    Serial.println("[+] Fetching appointments from the server...");
    HTTPClient http;
    
    // Link to Calendar
    http.begin("https://api.abfall.io/?kh=DaA02103019b46345f1998698563DaAd&t=ics&s=3700");

    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        appointmentDates = parseICSCalendar(payload);
        Serial.println("[+] Appointments updated successfully.");
    } else {
        Serial.printf("[!] HTTP request failed, code: %d\n", httpResponseCode);
    }

    http.end();
}

//-------------------------------------------------------------------------------
// Function to parse ICS calendar
//-------------------------------------------------------------------------------
String* parseICSCalendar(String payload) {
    static String dates[100]; // Assuming maximum of 100 dates for simplicity
    int index = 0;
    int startPos = 0;

    while ((startPos = payload.indexOf("DTSTART:", startPos)) != -1) {
        String date = payload.substring(startPos + 8, startPos + 16); // YYYYMMDD
        dates[index++] = date.substring(0, 4) + "-" + date.substring(4, 6) + "-" + date.substring(6, 8); // Convert to YYYY-MM-DD
        startPos += 8;
    }

    Serial.printf("[+] Found %d appointments\n", index);
    appointmentCount = index;  // Update appointment count
    return dates;
}

//-------------------------------------------------------------------------------
// Function to check if there's an appointment today
//-------------------------------------------------------------------------------
bool checkAppointmentToday() {
    time_t rawTime = timeClient.getEpochTime(); // Get epoch time
    struct tm* timeInfo = gmtime(&rawTime);    // Convert to UTC structure

    // Format the date as YYYY-MM-DD
    char dateBuffer[11];
    strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", timeInfo);
    String currentDate = String(dateBuffer);

    Serial.print("Current Date: ");
    Serial.println(currentDate);

    // Check if the current date matches an appointment
    for (uint8_t i = 0; i < appointmentCount; i++) {
        if (appointmentDates[i] == currentDate) {
            Serial.println("[+] Appointment found today.");
            return true; // Appointment found for today
        }
    }
    Serial.println("[-] No appointment for today.");
    return false; // No appointment for today
}

//-------------------------------------------------------------------------------
// Function to send the sequence via 433 MHz
//-------------------------------------------------------------------------------
void sendSequence() {
    Serial.println("[+] Sending signal...");
    for (uint8_t i = 0; i < transmissions; i++) {
        Serial.printf("[+] Transmission #%d\n", i + 1);
        for (uint8_t j = 0; j < messageLength; j++) {
            sendByte(sequence[j]);
        }
        delay(23); // Message distance in milliseconds
    }
}

void sendByte(uint8_t dataByte) {
    for (int8_t bit = 7; bit >= 0; bit--) { // MSB first
        digitalWrite(CC1101_GDO0_PIN, (dataByte & (1 << bit)) ? HIGH : LOW);
        delayMicroseconds(pulseWidth);
    }
}

//-------------------------------------------------------------------------------
// Setup
//-------------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("[+] Initializing system...");

    // Connect to WiFi using DHCP (no need for IP config, just DNS)
    WiFi.begin(ssid, password);
    Serial.print("[+] Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[+] WiFi connected");
    Serial.print("[+] IP Address: ");
    Serial.println(WiFi.localIP());

    // Set Google's DNS (8.8.8.8)
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, IPAddress(8, 8, 8, 8));
    Serial.println("[+] DNS set to Google's DNS (8.8.8.8)");

    // Initialize NTP
    timeClient.begin();
    Serial.println("[+] NTP client initialized");

    // Wait for time to sync
    Serial.println("[+] Waiting for NTP time...");
    while (!timeClient.update()) {
        Serial.print(".");
        delay(1000);
    }

    // Display the time
    Serial.print("[+] NTP Time: ");
    Serial.println(timeClient.getFormattedTime());

    // Initialize CC1101
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(CC1101_GDO0_PIN, OUTPUT);
    digitalWrite(CC1101_GDO0_PIN, LOW);

    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK_PIN, CC1101_MISO_PIN, CC1101_MOSI_PIN, CC1101_CS_PIN);
    if (ELECHOUSE_cc1101.getCC1101()) {
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
        ELECHOUSE_cc1101.setMHZ(433.92);   // Frequency
        ELECHOUSE_cc1101.setPA(10);        // Max Tx Power
        Serial.println("[+] CC1101 initialized and configured");
    } else {
        Serial.println("[!] CC1101 initialization failed");
        while (true) yield();
    }

    // Fetch appointments
    fetchAppointments();
}

//-------------------------------------------------------------------------------
// Loop
//-------------------------------------------------------------------------------
void loop() {
    static unsigned long lastCheckTime = 0;
    static unsigned long lastNoAppointmentCheckTime = 0;
    unsigned long currentTime = millis();

    if (checkAppointmentToday()) {
        // If there is an appointment today, check every minute
        if (currentTime - lastCheckTime > 60000) {
            lastCheckTime = currentTime;
            Serial.println("[+] Checking for appointment today...");
            sendSequence();
        }
    } else {
        // If there is no appointment today, check every 15 minutes
        if (currentTime - lastNoAppointmentCheckTime > 900000) { // 15 minutes
            lastNoAppointmentCheckTime = currentTime;
            Serial.println("[-] No appointment found. Checking again in 15 minutes.");
            fetchAppointments(); // Re-fetch appointments from the server
        }
    }
}
