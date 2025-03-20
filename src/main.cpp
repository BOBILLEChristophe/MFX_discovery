/*
   rocrail_mfx_discovery

    Programme permettant la reconnaissance MFX
    et la mise à jour de la liste des locomotives dans Rocrail©
*/

#ifndef ARDUINO_ARCH_ESP32
#error "Select an ESP32 board"
#endif

#define PROJECT "MFX_discovery"
#define VERSION "0.3"
#define AUTHOR "Christophe BOBILLE"

#include <Arduino.h>
#include <ACAN_ESP32.h>  // https://github.com/pierremolinaro/acan-esp32.git
#include <Ethernet.h>

CANMessage frameIn;
CANMessage frameOut;

/* ----- CAN Configuration ------ */
// #define CAN_RX GPIO_NUM_21
// #define CAN_TX GPIO_NUM_22
#define CAN_RX GPIO_NUM_22 // Optional, default Rx pin is GPIO_NUM_5
#define CAN_TX GPIO_NUM_23 // Optional, default Tx pin is GPIO_NUM_4
#define CAN_BITRATE 250UL * 1000UL  // Marklin CAN baudrate = 250Kbit/s

class CanMsg {
public:
  uint32_t id;
  uint8_t len;
  uint8_t command;
  uint8_t data[8];
  uint16_t hash;
};

CanMsg canMsg;

const byte nbLocos = 40;  // Le nombre de locos est fixé arbitrairement à 40. Peut être modifié en + ou en -

class Loco {
public:
  Loco();
  uint8_t index;
  uint8_t locID[2];
  uint8_t UID[4];
  static uint8_t comptLoco;  // compteur de locos "static" (indépendant des instances)
};

uint8_t Loco::comptLoco = 4;  // début du compteur
Loco::Loco()
  : index(255){};  // constructeur

Loco* loco = new Loco[nbLocos];  // instance loco de la classe Loco -> loco est un tableau de 40 (nbLocos) locomotives

bool err;

String decToHex(uint8_t dec) {
  auto convert = [](uint8_t x) -> char {
    return (x >= 10) ? (char)(x + 55) : (char)(x + 48);
  };
  String hex = "";
  hex += convert(dec / 16);
  hex += convert(dec % 16);
  return hex;
}

bool sendMsgCan(uint8_t CAN_Command, uint8_t length, uint8_t* canSendData) {
  canMsg.id = CAN_Command << 17 | canMsg.hash;
  frameOut.id = canMsg.id;
  frameOut.ext = 1;
  frameOut.len = length;
  memcpy(frameOut.data, canSendData, length);
  err = ACAN_ESP32::can.tryToSend(frameOut);
  if (err) {
    Serial.print("[ ]=> command 0x");
    Serial.println(decToHex(CAN_Command));
  }
  return err;
}

void sendMFXBind(Loco& loco);
void sendMFXVerify(Loco& loco);
void sendInitialCANCommands();
void processCANFrame();

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.printf("\nProject   :    %s", PROJECT);
  Serial.printf("\nVersion   :    %s", VERSION);
  Serial.printf("\nAuteur    :    %s", AUTHOR);
  Serial.printf("\nFichier   :    %s", __FILE__);
  Serial.printf("\nCompiled  :    %s - %s\n\n", __DATE__, __TIME__);

  //--- Configure ESP32 CAN
  Serial.println("Configure ESP32 CAN");
  ACAN_ESP32_Settings settings(CAN_BITRATE);
  settings.mRxPin = CAN_RX;
  settings.mTxPin = CAN_TX;
  const uint32_t errorCode = ACAN_ESP32::can.begin(settings);
  if (errorCode == 0) {
    Serial.println("CAN configuration OK!\n");
  } else {
    Serial.printf("CAN configuration error 0x%x\n", errorCode);
    delay(1000);
    return;
  }

  canMsg.hash = 0x2f39;

  // Send initial CAN commands
  sendInitialCANCommands();
}

void loop() {
  if (ACAN_ESP32::can.receive(frameIn)) {
    processCANFrame();
  }
}

void sendInitialCANCommands() {
  // 2.1 Commande : Système Arrêt
  delay(1000);
  memset(canMsg.data, 0x00, 8);
  sendMsgCan(0x00, 4, canMsg.data);

  // Bootloader CAN
  delay(1000);
  memset(canMsg.data, 0x00, 8);
  canMsg.data[4] = 0x11;
  sendMsgCan(0x1B, 5, canMsg.data);

  // 2.10 Commande : Système MFX Définir le compteur de nouvelles inscriptions
  delay(1000);
  memset(canMsg.data, 0x00, 8);
  canMsg.data[4] = 0x09;  // Sous-CMD MFX
  canMsg.data[5] = Loco::comptLoco & 0xFF00;
  canMsg.data[6] = Loco::comptLoco & 0x00FF;
  sendMsgCan(0x00, 7, canMsg.data);

  // 3.3 Commande : MFX Verify (pour provoquer un UnBIND)
  delay(1000);
  memset(canMsg.data, 0x00, 8);
  sendMsgCan(0x03, 6, canMsg.data);

  // 2.2 Commande : Système Go
  delay(1000);
  memset(canMsg.data, 0x00, 8);
  canMsg.data[4] = 0x01;  // Go à tous
  sendMsgCan(0x00, 5, canMsg.data);

  // 3.1 Ordre de mission : Locomotive Discovery
  delay(1000);
  memset(canMsg.data, 0x00, 8);
  canMsg.data[0] = 0x20;  // MFX protocole
  sendMsgCan(0x00, 1, canMsg.data);
}

void processCANFrame() {
  if (frameIn.id >> 16 == 0x03 && frameIn.len == 5) {
    Serial.printf("Frame ID : 0x%X\n", frameIn.id);
    Serial.printf("Frame length : %d\n", frameIn.len);
    Serial.printf("Frame response : %s\n", (frameIn.id & 0x10000) >> 16 ? "true" : "false");
    Serial.printf("Frame command : 0x%X\n", frameIn.id >> 17);
    
    for (byte i = 0; i < frameIn.len; i++) {
      Serial.printf("  Data [%d] = 0x%X\n", i, frameIn.data[i]);
    }

    bool found = false;
    for (byte i = 0; i < nbLocos; i++) {
      if (memcmp(loco[i].UID, frameIn.data, 4) == 0) {
        sendMFXBind(loco[i]);

        Serial.printf("Mise a jour d'une loco dans la liste Loc-ID = %d\n", Loco::comptLoco);
        for (uint8_t el : loco[i].UID) {
          Serial.printf("MFX-UID : \n");
          Serial.print("0x");
          Serial.println(el, HEX);
        }
        found = true;
        break;
      }
    }

    if (!found) {
      for (byte i = 0; i < nbLocos; i++) {
        if (loco[i].index == 255) {  // Cherche la première loco non renseignée dans la liste
          Loco::comptLoco++;         // affecte le n° du compteur +1
          loco[i].index = i;
          memcpy(loco[i].UID, frameIn.data, 4);
          loco[i].locID[0] = (Loco::comptLoco & 0xFF00) >> 8;
          loco[i].locID[1] = Loco::comptLoco & 0x00FF;
          Serial.printf("Creation d'une nouvelle loco dans la liste Loc-ID = %d\n", Loco::comptLoco);
          for (uint8_t el : loco[i].UID) {
            Serial.printf("MFX-UID : \n");
            Serial.print("0x");
            Serial.println(el, HEX);
          }
          sendMFXBind(loco[i]);
          sendMFXVerify(loco[i]);
          break;
        }
      }
    }
  }
}

void sendMFXBind(Loco& loco) {
  delay(10);
  memset(canMsg.data, 0x00, 8);
  memcpy(canMsg.data, loco.UID, 4);
  canMsg.data[4] = loco.locID[0];
  canMsg.data[5] = loco.locID[1];
  sendMsgCan(0x02, 6, canMsg.data);
}

void sendMFXVerify(Loco& loco) {
  delay(10);
  memset(canMsg.data, 0x00, 8);
  memcpy(canMsg.data, loco.UID, 4);
  canMsg.data[4] = loco.locID[0];
  canMsg.data[5] = loco.locID[1];
  sendMsgCan(0x03, 6, canMsg.data);
}

