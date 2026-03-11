#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// --- PINOUT SPECIFICA ---
const int RELAY_POWER = 1;  // Accende alimentatore 12V
const int RELAY_DOWN  = 5;  // Impulso discesa
const int RELAY_UP    = 6;  // Impulso salita
const int BTN_DOWN    = 7;  // Pulsante fisico muro
const int BTN_UP      = 10; // Pulsante fisico muro

// --- PARAMETRI TEMPORALI ---
const unsigned long BOOT_WAIT_MS    = 2000;  // Attesa avvio centralina 12V
const unsigned long IMPULSE_MS      = 500;   // Durata pressione tasto relè
const unsigned long INVERSION_DELAY = 1000;  // Pausa tra stop e inversione
const unsigned long POWER_OFF_DELAY = 60000; // Timeout spegnimento (1 min)

// --- STATI E VARIABILI ---
enum ScreenCommand { CMD_STOP = 0, CMD_DOWN = 1, CMD_UP = 2 };
ScreenCommand lastDirection = CMD_STOP;

unsigned long powerStartedAt = 0;
bool isPowerOn = false;

// --- FUNZIONE ESECUZIONE IMPULSO ---
void triggerRelay(int pin) {
    Serial.printf("[Action] Attivazione impulso su GPIO %d\n", pin);
    digitalWrite(pin, HIGH);
    delay(IMPULSE_MS);
    digitalWrite(pin, LOW);
}

// --- LOGICA DI MOVIMENTO (La "Mente" dello Slave) ---
void executeMovement(ScreenCommand cmd) {
    if (cmd == CMD_STOP) {
        digitalWrite(RELAY_POWER, LOW);
        isPowerOn = false;
        lastDirection = CMD_STOP;
        Serial.println("[System] STOP forzato: Alimentazione OFF");
        return;
    }

    // Alimentatore
    if (!isPowerOn) {
        Serial.println("Accensione alimentatore 12V...");
        digitalWrite(RELAY_POWER, HIGH);
        delay(BOOT_WAIT_MS); // Attesa boot 12V
        isPowerOn = true;
    }

    // Inversione (Se in direzione opposta, stoppa il telo, aspetta INVERSION_DELAY, e poi il comando lo attiva giusto)
    if (lastDirection != CMD_STOP && lastDirection != cmd) {
        Serial.println("[Logic] Rilevata inversione: invio impulso di STOP...");
        int stopPin = (lastDirection == CMD_UP) ? RELAY_UP : RELAY_DOWN;
        triggerRelay(stopPin);
        delay(INVERSION_DELAY);
    }

    // Comando
    int targetPin = (cmd == CMD_UP) ? RELAY_UP : RELAY_DOWN;
    triggerRelay(targetPin);
    
    lastDirection = cmd;
    powerStartedAt = millis(); // Reset timer
    Serial.println("[System] Movimento avviato. Timer 60s partito.");
}

// Callback ESP-NOW
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    ScreenCommand receivedCmd;
    memcpy(&receivedCmd, incomingData, sizeof(receivedCmd));
    Serial.printf("[Radio] Ricevuto comando: %d\n", receivedCmd);
    executeMovement(receivedCmd);
}

void setup() {
    Serial.begin(115200);
    
    pinMode(RELAY_POWER, OUTPUT);
    pinMode(RELAY_DOWN, OUTPUT);
    pinMode(RELAY_UP, OUTPUT);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_UP, INPUT_PULLUP);

    digitalWrite(RELAY_POWER, LOW);
    digitalWrite(RELAY_DOWN, LOW);
    digitalWrite(RELAY_UP, LOW);

    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_register_recv_cb(OnDataRecv);

    Serial.println("[OK] Slave Pronto con Logica Inversione.");
}

void loop() {
    // A. Gestione Pulsanti Fisici (Debounce semplificato)
    if (digitalRead(BTN_DOWN) == LOW) {
        delay(50); // Debounce
        if (digitalRead(BTN_DOWN) == LOW) {
            Serial.println("[Button] Premuto tasto GIÙ a muro");
            executeMovement(CMD_DOWN);
            while(digitalRead(BTN_DOWN) == LOW); // Attesa rilascio
        }
    }

    if (digitalRead(BTN_UP) == LOW) {
        delay(50); // Debounce
        if (digitalRead(BTN_UP) == LOW) {
            Serial.println("[Button] Premuto tasto SU a muro");
            executeMovement(CMD_UP);
            while(digitalRead(BTN_UP) == LOW); // Attesa rilascio
        }
    }

    // B. Timeout di sicurezza (Spegnimento 12V dopo 60s)
    if (isPowerOn && (millis() - powerStartedAt > POWER_OFF_DELAY)) {
        Serial.println("[Safety] Timeout 60s raggiunto. Spegnimento alimentatore.");
        digitalWrite(RELAY_POWER, LOW);
        isPowerOn = false;
        lastDirection = CMD_STOP;
    }
}