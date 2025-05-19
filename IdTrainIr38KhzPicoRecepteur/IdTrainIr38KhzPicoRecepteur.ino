/*
    IdTrainIr38KhzPicoRecepteur

    For Raspberry Pi Pico and Raspberry Pi Pico W

    Programe pour générer un identifiant en utilisant un émetteur et un récepteur infrarouge

    Christophe Bobille pour Locoduino mai 2025

    version 1.0

*/


#include <Arduino.h>
#include "pico/multicore.h"
#include "pico/util/queue.h"

volatile uint32_t duration = 0;

constexpr byte pinIn = 15;  // Broche du TSOP

bool receiving = false;
uint8_t currentByte = 0;
int8_t bitIndex = 7;

// File pour les bits détectés
queue_t durationQueue;

enum DecodeState {
  IDLE,
  RECEIVING
};
DecodeState currentState = IDLE;

void handleIR() {
  static uint32_t lastTime = 0;
  uint32_t now = micros();
  uint32_t duration = now - lastTime;
  lastTime = now;
  queue_try_add(&durationQueue, &duration);
}

void setup() {
  Serial.begin(115200);
  pinMode(pinIn, INPUT);
  // Initialisation de la queue pour 16 durées
  queue_init(&durationQueue, sizeof(duration), 16);
  attachInterrupt(digitalPinToInterrupt(pinIn), handleIR, RISING);
}

void loop() {
  uint32_t duration = 0;
  if (queue_try_remove(&durationQueue, &duration)) {

    switch (currentState) {

      case IDLE:
        if (duration > 1600 && duration < 2400) {
          // Début de trame détecté
          currentByte = 0;
          bitIndex = 7;
          currentState = RECEIVING;
        }
        break;

      case RECEIVING:

        if (duration >= 400 && duration <= 700) {
          // Bit 1
          currentByte |= (1 << bitIndex);
          bitIndex--;
        } else if (duration >= 800 && duration <= 1200) {
          // Bit 0
          currentByte &= ~(1 << bitIndex);
          bitIndex--;
        } else {
          // Durée invalide
          currentState = IDLE;
          break;
        }

        if (bitIndex < 0) {
          Serial.printf("Octet reçu : 0x%02X\n", currentByte);
          currentState = IDLE;
        }
        break;
    }
  }
}
