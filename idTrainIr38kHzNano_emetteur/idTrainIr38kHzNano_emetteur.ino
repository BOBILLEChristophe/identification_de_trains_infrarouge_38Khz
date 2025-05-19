/*
    idTrainIr38kHzNano_emetteur

    Programe pour générer un identifiant en utilisant un émetteur et un récepteur infrarouge

    Christophe Bobille pour Locoduino mai 2025

    version 1.0

*/
constexpr uint8_t ID = 0xA5;            // A modifier par l'utilisateur en fonction de l'identifiant souhaité

#include <Arduino.h>

// Configuration du timer pour PWM IR
#  if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega168__)
#define IR_USE_AVR_TIMER2  // Utilise Timer2 pour l'envoi (pin D3)
#define IR_SEND_PIN 3                   // Broche utilisée
#  elif defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
#define IR_SEND_PIN 4                   // Broche utilisée
#define IR_USE_AVR_TIMER_TINY1 // send pin = (pin 4) ATtiny25 / 45 / 85
#  endif

#define SEND_PWM_BY_TIMER  // Active le mode PWM matériel

#include "IRTimer.hpp"  // Part of Arduino-IRremote https://github.com/Arduino-IRremote/Arduino-IRremote.

constexpr uint16_t BIT_DURATION = 250;  // Durée minimale d'un front en µs

// Envoie un octet codé via LED IR
void sendByte(uint8_t value) {
  for (int i = 7; i >= 0; i--) {        // MSB
    bool bit = (value >> i) & 0x01;
    if (bit) {
      // 1
      disableSendPWMByTimer();
      delayMicroseconds(BIT_DURATION);
      enableSendPWMByTimer();
      delayMicroseconds(BIT_DURATION);
    } else {
      // 0
      disableSendPWMByTimer();
      delayMicroseconds(BIT_DURATION * 2);
      enableSendPWMByTimer();
      delayMicroseconds(BIT_DURATION * 2);
    }
  }
  // Fin de trame
    disableSendPWMByTimer();
    delayMicroseconds(BIT_DURATION * 4);
    enableSendPWMByTimer();
    delayMicroseconds(BIT_DURATION * 4);
}

void setup() {
  pinMode(IR_SEND_PIN, OUTPUT);
  timerConfigForSend(38);   // Fréquence IR : 38 kHz
  disableSendPWMByTimer();  // Démarre éteint
}

void loop() {
  sendByte(ID);
}
