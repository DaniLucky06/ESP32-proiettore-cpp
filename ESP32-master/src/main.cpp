#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// Pin
const int SENSE_USB  = 4; 
const int RELAY_MAIN = 5; 

// Timer
const unsigned long GRACE_PERIOD_MS   = 1000;
const unsigned long PRE_COOLING_PERIOD_MS = 1000;
const unsigned long COOLING_PERIOD_MS = 5000;

enum ScreenCommand { CMD_STOP = 0, CMD_DOWN = 1, CMD_UP = 2 };
enum SystemState { STANDBY, PROJECTOR_ON, GRACE_PERIOD, PRE_COOLING, COOLING, SHUTDOWN };

SystemState currentState = STANDBY;
unsigned long startTimeMillis = 0; 
int lastUsbStatus = -1;

// Slave MAC
uint8_t slaveAddress[] = {0xCC, 0x8D, 0xA2, 0xC0, 0x94, 0xFC}; 

void sendCommand(ScreenCommand cmd) {
    Serial.printf(">>> Invio comando radio: %d\n", cmd);
    esp_now_send(slaveAddress, (uint8_t *) &cmd, sizeof(cmd));
}

void setup() {
    Serial.begin(115200);

    pinMode(SENSE_USB, INPUT_PULLUP);
    pinMode(RELAY_MAIN, OUTPUT);
    digitalWrite(RELAY_MAIN, HIGH); // 220V

    WiFi.mode(WIFI_STA); // init Wifi per EspNow
    if (esp_now_init() != ESP_OK) {
        Serial.println("Errore Init ESP-NOW");
        return;
    }

    // Assegnazione MAC dello slave
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, slaveAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("Setup finito");
}

void loop() {
    int usbStatus = digitalRead(SENSE_USB); // Proiettore acceso/spento -> usbStatus 0/1

    // Stampa stato USB solo quando cambia
    if (usbStatus != lastUsbStatus) {
        Serial.printf("[USB] Stato: %s\n", (usbStatus == LOW ? "ACCESO" : "SPENTO"));
        lastUsbStatus = usbStatus;
    }

    switch (currentState) {
        case STANDBY:
        case SHUTDOWN:
            if (usbStatus == LOW) { // Rileva proiettore acceso
                digitalWrite(RELAY_MAIN, HIGH);
                sendCommand(CMD_DOWN);
                currentState = PROJECTOR_ON;
            }
            break;

        case PROJECTOR_ON:
            if (usbStatus == HIGH) { // Rileva proiettore spento
                startTimeMillis = millis();
                currentState = GRACE_PERIOD;
                Serial.println("Inizio Grace...");
            }
            break;

        case GRACE_PERIOD:
            if (usbStatus == LOW) { // ignora grace period e torna ad acceso
                currentState = PROJECTOR_ON;
            } else if (millis() - startTimeMillis >= GRACE_PERIOD_MS) { // finito il grace -> alza proiettore e passa a cooling
                sendCommand(CMD_UP);
                startTimeMillis = millis(); 
                currentState = PRE_COOLING;
                Serial.println("Inizio Pre-Cooling...");
            }
            break;

        case PRE_COOLING:
            if (millis() - startTimeMillis >= PRE_COOLING_PERIOD_MS) { // aspetta {PRE_COOLING_PERIOD_MS} per mandare un altro comando per non incasinare il telo
                startTimeMillis = millis();
                currentState = COOLING;
                Serial.println("Inizio Cooling...");
            }
            break;

        case COOLING:
            if (usbStatus == LOW) { // stoppa cooling se si riaccende il proiettore
                sendCommand(CMD_DOWN);
                currentState = PROJECTOR_ON;
            } else if (millis() - startTimeMillis >= COOLING_PERIOD_MS) { // quando passa COOLING_PERIOD_MS, suicida tutto
                digitalWrite(RELAY_MAIN, LOW); // Spegne 220V
                currentState = SHUTDOWN;
                Serial.println("Sistema in SHUTDOWN (Risparmio energetico)");
            }
            break;
    }
    delay(200);
}