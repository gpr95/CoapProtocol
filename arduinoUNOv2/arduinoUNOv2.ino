#include <SPI.h>
#include <RF24Network.h>
#include <RF24.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#define NumberOfSupportedOptions 3
#define MAX_RETRANSMIT 4
#define MESSAGES_TO_ACK 24
#define SERVER_RETRANSMIT true
#define INTERVAL 10
#define SECOND 1000

//RADIO_COMMUNICATION
RF24 radio(6, 7);
RF24Network network(radio);

/** RF24 ID */
const uint16_t deviceNodeId = 01;
const uint16_t serverNodeId = 00;

/** RF24 receive scruct */
struct payload_t {
  unsigned long value;
  unsigned long id;
  unsigned long type;
};

//ETHERNET_COMMUNICATION
byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
unsigned int localPort = 8888;
IPAddress remoteIp(192, 168, 1, 3);
const int remoteP = 8888;
EthernetUDP Udp;

#if SERVER_RETRANSMIT
uint16_t notConfirmedMessagesID[MESSAGES_TO_ACK];
uint8_t notConfirmedMessagesRT[MESSAGES_TO_ACK];
uint16_t notConfirmedMessagesSize[MESSAGES_TO_ACK];
unsigned long notConfirmedMessagesLastSent[MESSAGES_TO_ACK];
byte * notConfirmedMessagesPayload[MESSAGES_TO_ACK];
#endif

unsigned long now = 0;

unsigned long last_sent;
short unsigned int lastSentMessageID = 0;
bool retransmitting = false;
String lastPayload = "";

/** The setup function runs once when you press reset or power the board */
void setup() {

  //Serial setup
  Serial.begin(9600);
  Serial.println(F("Starting..."));

  //Radio setup
  SPI.begin();
  radio.begin();
  network.begin(90, serverNodeId);

  //Ethernet setup
  Ethernet.begin(mac);
  Udp.begin(localPort);
  Serial.print(Ethernet.localIP());
  Serial.print(F(":"));
  Serial.println(localPort);

  //Setup done
  Serial.println(F("Begin"));
#if SERVER_RETRANSMIT
  for (int i = 0; i < MESSAGES_TO_ACK; i++) {
    notConfirmedMessagesID[i] = 0;
  }
#endif
}

unsigned int v = 0;

// the loop function runs over and over again forever
void loop() {
  now = millis();
  if (now - last_sent >= INTERVAL) {
    last_sent = now;
    receiveUDPPacket();
#if SERVER_RETRANSMIT
    retransmit();
#endif
  }
}

//Receives UDP Packets
void receiveUDPPacket() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    int numberOfReadBytes = 0;
    byte bufferArray[packetSize];
    while (numberOfReadBytes != packetSize) {
      bufferArray[numberOfReadBytes++] = char(Udp.read());
    }
    processPacket(bufferArray, numberOfReadBytes);
  }
}

//Locating end of header in a CoAP message
void processPacket(byte *packetBuffer, int packetSize) {
  uint16_t hederSize = 0;
  for (int i = 0; i < packetSize; i++) {
    if (packetBuffer[i] == 255) {
      break;
    }
    hederSize = i + 1;
  }
  if (hederSize == 0) hederSize = packetSize;
  char tmpPayload[packetSize - hederSize];
  memcpy(tmpPayload, &packetBuffer[hederSize + 1], sizeof(tmpPayload));
  tmpPayload[packetSize - hederSize - 1] = '\0';
  lastPayload = String(tmpPayload);
  processIncomingHeader(packetBuffer, hederSize);
}

//Processing the header of a CoAP message
void processIncomingHeader(byte *packetHeader, uint16_t headerSize) {

  //Check if the protocol version is 01
  if (!bitRead(packetHeader[0], 7) && bitRead(packetHeader[0], 6)) {
    Serial.print(F("Ver: 01; "));
  } else {
    //if it's not, do nothing else with the packet
    return;
  }

  //Reading and storing messageID
  short unsigned int messageID = 0;
  messageID |= bitRead(packetHeader[2], 7) << 15;
  messageID |= bitRead(packetHeader[2], 6) << 14;
  messageID |= bitRead(packetHeader[2], 5) << 13;
  messageID |= bitRead(packetHeader[2], 4) << 12;
  messageID |= bitRead(packetHeader[2], 3) << 11;
  messageID |= bitRead(packetHeader[2], 2) << 10;
  messageID |= bitRead(packetHeader[2], 1) << 9;
  messageID |= bitRead(packetHeader[2], 0) << 8;
  messageID |= bitRead(packetHeader[3], 7) << 7;
  messageID |= bitRead(packetHeader[3], 6) << 6;
  messageID |= bitRead(packetHeader[3], 5) << 5;
  messageID |= bitRead(packetHeader[3], 4) << 4;
  messageID |= bitRead(packetHeader[3], 3) << 3;
  messageID |= bitRead(packetHeader[3], 2) << 2;
  messageID |= bitRead(packetHeader[3], 1) << 1;
  messageID |= bitRead(packetHeader[3], 0) << 0;

  //Checks if the message is ACK, NON or CON
  if (!bitRead(packetHeader[0], 5) && !bitRead(packetHeader[0], 4)) {
    Serial.print(F("CON; "));
    retransmitting = true;
    //Retransmition is ON
  } else if (!bitRead(packetHeader[0], 5) && bitRead(packetHeader[0], 4)) {
    Serial.print(F("NON; "));
    retransmitting = false;
    //Retransmition is OFF
  } else if (bitRead(packetHeader[0], 5) && !bitRead(packetHeader[0], 4)) {
    Serial.print(F("ACK; "));
    //Removin message from waiting list
#if SERVER_RETRANSMIT
    bool success = deleteMessageToRetransmit(messageID);
    if (success)
      Serial.print(F("Message removed from waiting list."));
#endif
    return;
  } else if (bitRead(packetHeader[0], 5) && bitRead(packetHeader[0], 4)) {
    Serial.print(F("RESET; "));
#if SERVER_RETRANSMIT
    bool success = deleteMessageToRetransmit(messageID);
    if (success)
      Serial.print(F("Message removed from waiting list."));
#endif
    return;
  }

  //Reading token length
  unsigned short int tokenLenght = bitRead(packetHeader[0], 3) << 3 |
                                   bitRead(packetHeader[0], 2) << 2 |
                                   bitRead(packetHeader[0], 1) << 1 |
                                   bitRead(packetHeader[0], 0) << 0;

  //Storing token
  char token[tokenLenght + 1];
  memcpy(&token[0], &packetHeader[4], tokenLenght);
  token[tokenLenght] = '\0';
  String tokenString = String(token);

  //Reading Options
  uint16_t optionsStart = 4 + tokenLenght;
  uint16_t optionsLenght = headerSize - optionsStart;
  Serial.print(F("Header length: "));
  Serial.println(headerSize);
  Serial.print(F("Token lenght: "));
  Serial.print(tokenLenght);
  Serial.println(F("; "));
  Serial.print(F("Options length: "));
  Serial.println(optionsLenght);

  //When options are not empty
  uint16_t optionDelta[NumberOfSupportedOptions];
  String optionValue[NumberOfSupportedOptions];
  unsigned short int totalLength = 0;
  unsigned short int totalDelta = 0;
  for (int i = 0; i < NumberOfSupportedOptions; i++) {
    optionDelta[i] = 0;
  }
  for (int i = 0; i < NumberOfSupportedOptions; i++)
    if (packetHeader[optionsStart] != -1 && optionsLenght != 0) {
      //Option header
      optionDelta[i] = bitRead(packetHeader[optionsStart + totalLength], 7) << 3 |
                       bitRead(packetHeader[optionsStart + totalLength], 6) << 2 |
                       bitRead(packetHeader[optionsStart + totalLength], 5) << 1 |
                       bitRead(packetHeader[optionsStart + totalLength], 4) << 0;

      int currentLength = bitRead(packetHeader[optionsStart + totalLength], 3) << 3 |
                          bitRead(packetHeader[optionsStart + totalLength], 2) << 2 |
                          bitRead(packetHeader[optionsStart + totalLength], 1) << 1 |
                          bitRead(packetHeader[optionsStart + totalLength], 0) << 0;
      totalLength += 1;

      //jezeli informacje się nie zmieściły w headerze
      if (optionDelta[i] == 13) {
        optionDelta[i] = packetHeader[optionsStart + totalLength] + 13;
        totalLength += 1;
      } else if (optionDelta[i] == 14) {
        optionDelta[i] = packetHeader[optionsStart + totalLength] * 256 + packetHeader[optionsStart + totalLength + 1] + 269;
        totalLength += 2;
      }
      totalDelta += optionDelta[i];
      optionDelta[i] = totalDelta;

      //jezeli rozmiar nie zmiescil sie w headerze
      if (currentLength == 13) {
        currentLength = packetHeader[optionsStart + totalLength] + 13;
        totalLength += 1;
      } else if (currentLength == 14) {
        currentLength = packetHeader[optionsStart + totalLength] * 256 + packetHeader[optionsStart + totalLength + 1] + 269;
        totalLength += 2;
      }

      char tmp[currentLength + 1];
      memcpy(&tmp[0], &packetHeader[optionsStart + totalLength], currentLength);

      tmp[currentLength] = '\0';
      optionValue[i] = String(tmp);
      totalLength += currentLength;
      Serial.print(F("Delta: "));
      Serial.print(optionDelta[i]);
      Serial.print(F("; Length: "));
      Serial.print(currentLength);
      Serial.print(F("; Valure: "));
      if (optionValue[i].length() > 1)
        Serial.println(optionValue[i]);
      else
        Serial.println(optionValue[i].charAt(0), DEC);
      if (totalLength >= optionsLenght) break;
    }

  //CODE
  //Checks the type if message type is REQUEST (code: 000-----)
  if (!bitRead(packetHeader[1], 7) && !bitRead(packetHeader[1], 6) && !bitRead(packetHeader[1], 5)) {
    if (!bitRead(packetHeader[1], 4) && !bitRead(packetHeader[1], 3) && !bitRead(packetHeader[1], 2) &&
        !bitRead(packetHeader[1], 1) && bitRead(packetHeader[1], 0)) {

      //GET
      Serial.print(F("GET REQUEST; "));
      String what = "";
      for (int i = 0; i < sizeof(optionDelta) / sizeof(uint16_t); i++) {
        if (optionDelta[i] == 11) {
          what = optionValue[i];
          break;
        }
      }
      if (isWellKnown(what)) {
        sendWellKnownResponse(retransmitting, messageID, token);
      } else {
        if (retransmitting) {
          sendACKMessage(messageID);
          sendConMessage(tokenString, 2, 3, what + " = " + getResource(what));
        }
        else
          sendNonMessage(tokenString, 2, 3, what + " = " + getResource(what));
      }

    } else if (!bitRead(packetHeader[1], 4) && !bitRead(packetHeader[1], 3) && !bitRead(packetHeader[1], 2) &&
               bitRead(packetHeader[1], 1) && bitRead(packetHeader[1], 0)) {

      //PUT
      Serial.print(F("PUT REQUEST; "));
      if (retransmitting)
        sendACKMessage(messageID);
      for (int i = 0; i < sizeof(optionDelta) / sizeof(uint16_t); i++) {
        if (optionDelta[i] == 11) {
          putResource(optionValue[i]);
          break;
        }
      }
    } else {
      Serial.print(F("OTHER; "));
      //Other
      //Ping
      if (retransmitting)
        sendACKMessage(messageID);
    }
  }
  Serial.print(F("Message ID: "));
  Serial.println(messageID);

  Serial.print(F("Token: "));
  Serial.println(tokenString);
}

//Sends NON messages
void sendNonMessage(String token, short unsigned int code1, short unsigned int code2, String payload) {

  unsigned short int tokenLength = token.length();
  unsigned short int messageLength = 5 + tokenLength + payload.length();
  char header[messageLength];
  if (tokenLength != 0) {
    char tokenChar[tokenLength];
    strcpy(tokenChar, token.c_str());
  }

  char payloadChar[payload.length()];
  strcpy(payloadChar, payload.c_str());

  //Ver, T, TKL
  header[0] = 0 << 7 | 1 << 6;
  header[0] |= 0 << 5 | 1 << 4;
  header[0] |= bitRead(tokenLength, 3) << 3 |
               bitRead(tokenLength, 2) << 2 |
               bitRead(tokenLength, 1) << 1 |
               bitRead(tokenLength, 0) << 0;

  //Code      //Pierwsza liczba kodu
  header[1] = bitRead(code1, 2) << 7 |
              bitRead(code1, 1) << 6 |
              bitRead(code1, 0) << 5 |
              //Druga liczba kodu
              bitRead(code2, 4) << 4 |
              bitRead(code2, 3) << 3 |
              bitRead(code2, 2) << 2 |
              bitRead(code2, 1) << 1 |
              bitRead(code2, 0) << 0;

  //MessageID
  short unsigned int messageID = ++lastSentMessageID;
  header[2] = (unsigned char)(messageID >> 8);
  header[3] = messageID & 0xff;
  header[4 + tokenLength] = B11111111;

  if (tokenLength != 0)
    memcpy(&header[4], token.c_str(), tokenLength);
  if (payload.length() != 0)
    memcpy(&header[5 + tokenLength], payload.c_str(), payload.length());

  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  int r = Udp.write(header, messageLength);
  Udp.endPacket();
  Serial.println(F("NON Sent"));
}

//Sends CON message
void sendConMessage(String token, short unsigned int code1, short unsigned int code2, String payload) {

  uint16_t tokenLength = token.length();
  uint16_t messageLength = 5 +  tokenLength + payload.length();
  byte message[messageLength];

  //Ver, T, TKL
  message[0] = 0 << 7 | 1 << 6;
  message[0] |= 0 << 5 | 0 << 4;
  message[0] |= bitRead(tokenLength, 3) << 3 |
                bitRead(tokenLength, 2) << 2 |
                bitRead(tokenLength, 1) << 1 |
                bitRead(tokenLength, 0) << 0;

  //Code      //Pierwsza liczba kodu
  message[1] = bitRead(code1, 2) << 7 |
               bitRead(code1, 1) << 6 |
               bitRead(code1, 0) << 5 |
               //Druga liczba kodu
               bitRead(code2, 4) << 4 |
               bitRead(code2, 3) << 3 |
               bitRead(code2, 2) << 2 |
               bitRead(code2, 1) << 1 |
               bitRead(code2, 0) << 0;

  //MessageID
  short unsigned int messageID = ++lastSentMessageID;
  message[2] = (unsigned char)(messageID >> 8);
  message[3] = messageID & 0xff;
  message[4 + tokenLength] = B11111111;

  if (tokenLength != 0)
    memcpy(&message[4], token.c_str(), tokenLength);
  if (payload.length() != 0)
    memcpy(&message[5 + tokenLength], payload.c_str(), payload.length());

  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  int r = Udp.write(message, messageLength);
  Udp.endPacket();
  Serial.println(F("CON Sent"));

#if SERVER_RETRANSMIT
  allocateMessageToRetransmit(messageID, message, messageLength);
  Serial.println(F("Message added to waiting list"));
#endif

}

//Sends ACK
void sendACKMessage(short unsigned int messageID) {

  char ackHeader[4];
  //Ver
  ackHeader[0] = 0 << 7 | 1 << 6;
  //Type
  ackHeader[0] |= 1 << 5 | 0 << 4;
  //TKL
  ackHeader[0] |= 0 << 3 | 0 << 2 | 0 << 1 | 0 << 0;
  //Code
  ackHeader[1] = B00000000;
  //MessageID
  ackHeader[2] = (unsigned char)(messageID >> 8);
  ackHeader[3] = messageID & 0xff;
  //Wysylam ACK
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  int r = Udp.write(ackHeader, 4);
  Udp.endPacket();
  Serial.println(F("ACK Sent"));
}

//Displays bits of header - for debug purposes only
void showHeaderBits(char *packetBuffer) {
  int lenght = sizeof(packetBuffer) / sizeof(packetBuffer[0]);
  for (int j = 0; j < lenght; j++) {
    for (int i = 7; i >= 0; i--) {
      Serial.print(bitRead(packetBuffer[j], i));
    }
  }
  Serial.print(F("\n"));
}

//Sends Well-known response
void sendWellKnownResponse(bool isCON, short unsigned int messageID, char token[]) {
  String payload = getWellKnown();
  uint16_t msgLength = payload.length() + 6;
  char message[msgLength];
  message[1] = B01000101;
  message[4] = B11000001;
  message[5] = B00101000;
  message[6] = B11111111;
  if (isCON) {
    message[0] = B01100000;
    message[2] = (unsigned char)(messageID >> 8);
    message[3] = messageID & 0xff;
    memcpy(&message[7], payload.c_str(), payload.length());
  } else {
    message[0] = B01010000;
    message[2] = 0;
    message[3] = 0;
    memcpy(&message[7], payload.c_str(), payload.length());
  }
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  int r = Udp.write(message, msgLength);
  Udp.endPacket();
  Serial.println(F("Well-known sent"));
  free(&message);
}

//Sends UDP messages
void writeThroughUDP(char msg) {
  Udp.beginPacket(remoteIp, remoteP);
  Udp.write(msg);
  Udp.endPacket();
}


bool isWellKnown(String optionValue) {
  String wellKnown = ".well-known";
  for (int i = 0; i < 11; i++) {
    if (wellKnown.charAt(i) != optionValue.charAt(i))
      return false;
  }
  return true;
}
#if SERVER_RETRANSMIT
void retransmit() {
  for (int i = 0; i < MESSAGES_TO_ACK; i++) {
    if (notConfirmedMessagesID[i] != 0) {
      if (notConfirmedMessagesRT[i] < MAX_RETRANSMIT) {
        if (millis() - notConfirmedMessagesLastSent[i] > pow(2, notConfirmedMessagesRT[i] + 1) * SECOND) {
          notConfirmedMessagesLastSent[i] = millis();
          ++notConfirmedMessagesRT[i];
          Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
          int r = Udp.write(notConfirmedMessagesPayload[i], notConfirmedMessagesSize[i]);
          Udp.endPacket();
          Serial.println(F("Retransmit sent"));
        }
      } else if (notConfirmedMessagesRT[i] >= MAX_RETRANSMIT) {
        deleteMessageToRetransmit(notConfirmedMessagesID[i]);
      }
    }
  }
}

bool deleteMessageToRetransmit(int messageID) {
  for (int i = 0; i < MESSAGES_TO_ACK; i++) {
    if (notConfirmedMessagesID[i] == messageID) {
      notConfirmedMessagesID[i] = 0;
      notConfirmedMessagesRT[i] = 0;
      notConfirmedMessagesSize[i] = 0;
      notConfirmedMessagesLastSent[i] = 0;
      free(notConfirmedMessagesPayload[i]);
      return true;
    }
  }
  return false;
}

void allocateMessageToRetransmit(int ID, byte * payload, int s) {
  int availableSlot = findFirstFreeSlot();
  if (availableSlot == -1) return; //Brak miejsc w kolejce
  notConfirmedMessagesID[availableSlot] = ID;
  notConfirmedMessagesRT[availableSlot] = 0;
  notConfirmedMessagesSize[availableSlot] = s;
  notConfirmedMessagesLastSent[availableSlot] = millis();
  notConfirmedMessagesPayload[availableSlot] = malloc(s);
  memcpy(&notConfirmedMessagesPayload[availableSlot][0], payload, s);
  //  for (int i = 0; i < s; i++) {
  //    notConfirmedMessagesPayload[availableSlot][i] = payload[i];
  //  }
}

int findFirstFreeSlot() {
  for (int i = 0; i < MESSAGES_TO_ACK; i++) {
    if (notConfirmedMessagesID[i] == 0)
      return i;
  }
  return -1;
}
#endif

String getWellKnown() {
  String wellKnownResponse = "<lamp>;rt=\"lamp\";if=\"sensor\",<speaker>;rt=\"speaker\";if=\"sensor\",<rfrate>;rt=\"rfrate\";if=\"sensor\"";
  return wellKnownResponse;
}

int getResource(String resourceId) {
  int resourceValue;
  boolean receivedAck = false;
  do {
    Serial.print("Sending GET to Device ...");
    Serial.print(resourceId);
    int mappedResource = -1;
    if (resourceId.equals("lamp"))
      mappedResource = 0;
    else if (resourceId.equals("speaker"))
      mappedResource = 1;
    else if (resourceId.equals("isCar"))
      mappedResource = 2;
    else if (resourceId.equals("64dBcar"))
      mappedResource = 3;
    else if (resourceId.equals("rfrate"))
      mappedResource = 4;
    payload_t payload = { 0, mappedResource , 0};
    RF24NetworkHeader header(/*to node*/ deviceNodeId);
    bool ok = network.write(header, &payload, sizeof(payload));
    if (ok)
      Serial.println("ok sended.");
    else
      Serial.println("failed sending.");

    network.update();
    while ( network.available() ) {
      RF24NetworkHeader header;        // If so, grab it and print it out
      payload_t payload;
      network.read(header, &payload, sizeof(payload));

      Serial.print("Received value:");
      Serial.print(payload.value);
      Serial.print(" id: ");
      Serial.print(payload.id);
      Serial.print(" type: ");
      Serial.println(payload.type);

      if (payload.type == 2) {
        receivedAck = true;
        resourceValue = payload.value;
      }
      delay(50);
    }
    delay(50);
  } while (!receivedAck);

  return resourceValue;
}

void putResource(String resourceId) { //, int value) {
  unsigned long value;

  boolean receivedAck = false;
  do {
    Serial.print("Sending PUT to Device ...");
    Serial.println(resourceId);
    int mappedResource = -1;
    if (resourceId.equals("lamp"))
      mappedResource = 0;
    else if (resourceId.equals("speaker"))
      mappedResource = 1;
    int lastPayloadSize = lastPayload.length();
    int unity = lastPayload[lastPayloadSize - 1] - 48;
    value = unity;
    if (lastPayloadSize > 1)
      value = value + lastPayload[lastPayloadSize - 2] - 48;
    if (lastPayloadSize > 2)
      value = value + lastPayload[lastPayloadSize - 3] - 48;
    if (lastPayloadSize > 3)
      value = value + lastPayload[lastPayloadSize - 4] - 48;
    if (lastPayloadSize > 4)
      value = value + lastPayload[lastPayloadSize - 5] - 48;
    payload_t payload = { value, mappedResource , 1};
    Serial.print("Sending value:");
    Serial.print(payload.value);
    Serial.print(" id: ");
    Serial.print(payload.id);
    Serial.print(" type: ");
    Serial.println(payload.type);
    RF24NetworkHeader header(/*to node*/ deviceNodeId);
    bool ok = network.write(header, &payload, sizeof(payload));
    if (ok)
      Serial.println("ok sended.");
    else
      Serial.println("failed sending.");

    network.update();
    while ( network.available() ) {
      RF24NetworkHeader header;        // If so, grab it and print it out
      payload_t payload;
      network.read(header, &payload, sizeof(payload));

      Serial.print("Received value:");
      Serial.print(payload.value);
      Serial.print(" id: ");
      Serial.print(payload.id);
      Serial.print(" type: ");
      Serial.println(payload.type);

      if (payload.type == 2) {
        receivedAck = true;
      }
      delay(50);
    }
    delay(50);
  } while (!receivedAck);




  Serial.print(F("Resource "));
  Serial.print(resourceId);
  Serial.print(F(" with value: "));
  Serial.println(value);
}
