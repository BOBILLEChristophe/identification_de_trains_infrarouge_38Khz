/*

    Mettre plusieurs broches sous interruption en utilisant une seule fonction callback

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

// Configuration Wi-Fi
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Adresse de Rocrail
const char* serverIP = "192.168.1.15";  // IP du PC avec Rocrail
const uint16_t serverPort = 8051;       // Port LAN configuré dans Rocrail

WiFiClient client;  // Instance de Wifi

typedef struct {  // Structure pour envoi des messages par TCP
  char text[64];
} TcpMessage;

QueueHandle_t tcpMsgQueue;  // sizeof(TcpMessage)

const uint8_t sizeOfID = 8;  // Taille en nombre de bits des données attendues

// Création d'un tableau pour les broches sous interruption
const byte nbInterPin = 16;                                                                // Choisir le nombre de broches
uint interPin[nbInterPin] = { 3, 4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 19, 20, 21, 27, 28 };  // Identification des broches du Pico                                                              // Important, renseigner le nombre de bits attendus pour les identifiants : 8, 16, 24 ou +

// ID des capteurs dans Rocrail
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

QueueHandle_t eventQueue;            // Queue pour stockage des données reçues
constexpr byte eventQueueSize = 32;  // Taille pour la file

typedef struct {      // Une structure pour le stockage des information suite à interruption
  uint gpio;          // De quelle pin s'agit - il ?
  uint32_t duration;  // Durée entre deux fronts montants
} GpioEvent;

typedef struct {  // Une structure pour stockage des données après traitement
  uint gpio;
  int8_t bitIndex;
  uint8_t currentState;
  uint32_t currentByte;
  uint32_t duration;
  const char* rrSensor;
} Msg;

// Routine d'interruption
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


  enum { IDLE,
         RECEIVING };  // Etats pendant le traitement

  GpioEvent evt;  // Instance de la structure GpioEvent

  const byte dernPin = interPin[nbInterPin - 1];  // Numéro de la dernière pin utilisée
  Msg** msg = new Msg*[dernPin];                  // Tableau de pointeurs vers Msg

  // Initialiser tout le pointeurs à NULL
  for (byte i = 0; i <= dernPin; i++) {
    msg[i] = nullptr;
  }

  // Pointeurs utilisés uniquement
  for (byte i = 0; i < nbInterPin; i++) {
    byte pin = interPin[i];
    msg[pin] = new Msg;
    msg[pin]->gpio = pin;
    msg[pin]->bitIndex = -1;
    msg[pin]->currentState = 0;
    msg[pin]->currentByte = 0;
    msg[pin]->duration = 0;
    msg[pin]->rrSensor = rrSensor[i];

    Serial.print("Initialisation msg[");
    Serial.print(pin);
    Serial.print("] -> sensor : ");
    Serial.println(msg[pin]->rrSensor);
  }

  for (;;) {
    while (xQueueReceive(eventQueue, &evt, portMAX_DELAY)) {
      printf("GPIO %d déclenché,  durée %d\n", evt.gpio, evt.duration);
      const byte pin = evt.gpio;

      switch (msg[pin]->currentState) {

        case IDLE:                                         // Recherche du bit de synchro
          if (evt.duration > 1600 && evt.duration < 2400)  // Début de trame détecté
          {
            msg[pin]->currentByte = 0;
            msg[pin]->bitIndex = sizeOfID - 1;
            msg[pin]->currentState = RECEIVING;
          }
          break;

        case RECEIVING:                                    // Réception des données
          if (evt.duration >= 400 && evt.duration <= 700)  // Bit 1
          {
            msg[pin]->currentByte |= (1 << msg[pin]->bitIndex);
            msg[pin]->bitIndex--;
          } else if (evt.duration >= 800 && evt.duration <= 1200)  // Bit 0
          {
            msg[pin]->currentByte &= ~(1 << msg[pin]->bitIndex);
            msg[pin]->bitIndex--;
          } else {
            // Durée invalide
            msg[pin]->currentState = IDLE;  // On arrete le processus en cours et on se met en attente d'un bit de synchro
            break;
          }

          if (msg[pin]->bitIndex < 0) {  // Tous les bits sont reçus
            Serial.printf("Octet reçu capteur %s : 0x%02X\n", msg[pin]->rrSensor, msg[pin]->currentByte);
            // On prépare le message et on le pplace dans la file d'attente pour envoi
            TcpMessage rrMsg;
            snprintf(rrMsg.text, sizeof(rrMsg.text),
                     "<fb id=\"%s\" identifier=\"%d\" state=\"true\" bididir=\"1\" actor=\"user\"/>", msg[pin]->rrSensor, msg[pin]->currentByte);
            xQueueSend(tcpMsgQueue, &rrMsg, 0);
            msg[pin]->currentState = IDLE;
          }
          break;
      }
      vTaskDelay(1);
    }
  }
}

void tcpSend(void* param) {
  TcpMessage msg;
  while (true) {
    if (xQueueReceive(tcpMsgQueue, &msg, portMAX_DELAY)) {
      if (client.connect(serverIP, serverPort)) {
        Serial.println("Connexion TCP OK");

        // Envoi du message reçu
        client.print(msg.text);
        delay(50);
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

  eventQueue = xQueueCreate(eventQueueSize, sizeof(GpioEvent));  // Une file d'attente entre l'interruption et le traitement
  tcpMsgQueue = xQueueCreate(10, sizeof(char[64]));              // Une file d'attente après traitement pour envoi à Rocrail


  //init des broches des capteurs et création des interruptions
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
  // Création de la tâches FreeRTOS pour l'envoi à Rocrail
  xTaskCreate(tcpSend, "tcpSend", 2048, NULL, 1, NULL);
}

void loop() {}
