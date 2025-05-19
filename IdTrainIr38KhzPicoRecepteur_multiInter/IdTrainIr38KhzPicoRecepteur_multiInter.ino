/*

    Mettre plusieurs broches sous interuption en utilisant une seule fonction callback

    Le RP2040 du Pico dispose de 30 broches GPIO, numérotées de 0 à 29. Parmi elles :

    - Presque toutes peuvent être configurées pour déclencher une interruption sur front montant, descendant ou les deux.
    - Il est donc possible d'attacher une routine à chaque GPIO, mais elles doivent partager la même fonction de callback


    RP Pi Pico pinout : https://www.raspberrypi.com/documentation/microcontrollers/images/pico-2-r4-pinout.svg
*/

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <WiFi.h>
#include <WiFiClient.h>

// === Configuration Wi-Fi ===
const char* ssid = "Livebox-BC90";
const char* password = "V9b7qzKFxdQfbMT4Pa";

// Adresse de Rocrail
const char* serverIP = "192.168.1.15";  // IP du PC avec Rocrail
const uint16_t serverPort = 8051;       // Port LAN configuré dans Rocrail

WiFiClient client;

typedef struct {
  char text[64];
} TcpMessage;

QueueHandle_t tcpMsgQueue;  // sizeof(TcpMessage)

// Création d'un tableau pour les broches sous interruption
constexpr byte nbInterPin = 16;  // Choisir le nombre de broches
uint interPin[nbInterPin] = { 3, 4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 19, 20, 21, 27, 28 };
constexpr byte nbBits = 8;  // Important, renseigner le nombre de bits attendus 8, 16, 24...

const char* rrSensor[nbInterPin] = {
  "sb1e", "sb1i",
  "sb2e", "sb2i",
  "sb3e", "sb3i",
  "sb4e", "sb4i",
  "sb5e", "sb5i",
  "sb6e", "sb6i",
  "sb7e", "sb7i",
  "sb8e", "sb8i"
};

QueueHandle_t eventQueue;  // Queue pour stockage des données reçues
constexpr byte eventQueueSize = 32;

typedef struct {
  uint gpio;
  uint32_t duration;
} GpioEvent;

typedef struct {
  uint gpio;
  int8_t bitIndex;
  uint8_t currentState;
  uint32_t currentByte;
  uint32_t duration;
} Msg;


void handleIR(uint gpio, uint32_t duration) {
  static uint32_t lastTime = 0;
  uint32_t now = micros();
  duration = now - lastTime;
  //uint32_t duration = now - lastTime;
  lastTime = now;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  GpioEvent evt = { gpio, duration };
  xQueueSendFromISR(eventQueue, &evt, &xHigherPriorityTaskWoken);
  // demande un changement de contexte si nécessaire
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Traitement des données
void taskTraitementData(void* pvParameters) {

  GpioEvent evt;
  enum { IDLE,
         RECEIVING };

  Msg msg[nbInterPin];
  for (byte i = 0; i < nbInterPin; i++) {
    msg[i].gpio = interPin[i];
    msg[i].currentState = IDLE;
  }


  for (;;) {
    while (xQueueReceive(eventQueue, &evt, portMAX_DELAY)) {
      printf("GPIO %d déclenché,  durée %d\n", evt.gpio, evt.duration);

      for (byte i = 0; i < nbInterPin; i++) {
        if (msg[i].gpio == evt.gpio) {
          switch (msg[i].currentState) {

            case IDLE:
              if (evt.duration > 1600 && evt.duration < 2400) {
                // Début de trame détecté
                msg[i].currentByte = 0;
                msg[i].bitIndex = nbBits - 1;
                msg[i].currentState = RECEIVING;
              }
              break;

            case RECEIVING:
              if (evt.duration >= 400 && evt.duration <= 700) {
                // Bit 1
                msg[i].currentByte |= (1 << msg[i].bitIndex);
                msg[i].bitIndex--;
              } else if (evt.duration >= 800 && evt.duration <= 1200) {
                // Bit 0
                msg[i].currentByte &= ~(1 << msg[i].bitIndex);
                msg[i].bitIndex--;
              } else {
                // Durée invalide
                msg[i].currentState = IDLE;
                break;
              }

              if (msg[i].bitIndex < 0) {
                Serial.printf("Octet reçu capteur %s : 0x%02X\n", rrSensor[i], msg[i].currentByte);

                TcpMessage rrMsg;
                snprintf(rrMsg.text, sizeof(rrMsg.text),
                         "<fb id=\"%s\" identifier=\"%d\" state=\"true\" bididir=\"1\" actor=\"user\"/>", rrSensor[i], msg[i].currentByte);
                xQueueSend(tcpMsgQueue, &rrMsg, 0);
                msg[i].currentState = IDLE;
              }
              break;
          }
        }
      }
      vTaskDelay(1);
    }
  }
}

void sendFB(const char* fbMsg) {
  String header = "<xmlh><xml size='" + String(strlen(fbMsg)) + "'/></xmlh>";
  String fullMessage = header + String(fbMsg);
  client.print(fullMessage);
  Serial.println("Message envoyé :");
  Serial.println(fullMessage);
}

void tcpSend(void* param) {
  char msg[64];
  while (true) {
    if (xQueueReceive(tcpMsgQueue, &msg, portMAX_DELAY)) {
      if (client.connect(serverIP, serverPort)) {
        Serial.println("Connexion TCP OK");

        // Envoi du message reçu
        client.print(msg);

        delay(100);

        client.stop();
        Serial.println("Connexion fermée");
      } else {
        Serial.println("Connexion échouée");
      }
    }
  }
}



/*---------------------------------------------------------------------------------------------
setup
----------------------------------------------------------------------------------------------*/

void setup() {

  Serial.begin(115200);
  delay(500);

  // === Connexion Wi-Fi ===
  Serial.print("Connexion à : ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connecté !");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());

  // === Connexion à Rocrail ===
  Serial.print("Connexion à Rocrail : ");
  Serial.print(serverIP);
  Serial.print(":");
  Serial.println(serverPort);

  eventQueue = xQueueCreate(eventQueueSize, sizeof(GpioEvent));
  tcpMsgQueue = xQueueCreate(10, sizeof(char[64]));


  //--- init des broches des capteurs et création des interruptions
  for (byte i = 0; i < nbInterPin; i++) {
    // Initialisation des GPIO en entrée avec pull-up
    gpio_init(interPin[i]);
    gpio_set_dir(interPin[i], GPIO_IN);
    gpio_pull_up(interPin[i]);

    // création des interruptions
    if (i == 0)
      gpio_set_irq_enabled_with_callback(interPin[i], GPIO_IRQ_EDGE_RISE, true, &handleIR);  // La foncion de callback n'est déclarée qu'une seule fois
    else
      gpio_set_irq_enabled(interPin[i], GPIO_IRQ_EDGE_RISE, true);
  }

  // Création de la tâches FreeRTOS pour le traitement des données
  xTaskCreate(taskTraitementData, "Traitement data", 1024, nullptr, 1, nullptr);
  xTaskCreate(tcpSend, "tcpSend", 2048, NULL, 1, NULL);
}

void loop() {}
