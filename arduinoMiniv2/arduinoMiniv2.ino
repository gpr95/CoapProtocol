#include <TimerOne.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

/** nRF24L01(+) radio attached using SPI */
RF24 radio(7, 8);
RF24Network network(radio);
/** Address in network radio topology of this node (node 00 is parent of that node) */
const uint16_t this_node = 01;
/** Address in network radio topology of the server node */
const uint16_t serverNodeId = 00;
/** Lamp state (1 - ON, 0 - OFF) */
int state = 1;
/** Frequency of speaker interruptions*/
int herces = 300;
/**
   value: value to put
   id: 0 - lamp, 1 - speaker, 2 - rf_is_carrier_on_the_line , 3 - rf_is_carrier_greater_than_minus_64dB, 4 - rf_data_rate
   type: 0 - get, 1 - put, 2 - ACK
*/

/** Structure to send through radio */
struct payload_t {
  unsigned long value;
  unsigned long id;
  unsigned long type;
};



void setup(void) {
  Serial.begin(57600);
  Serial.println("ArduinoMini_IOT_ELEMENT");

  /** Lamp */
  pinMode(3, OUTPUT);
  /** Speker */
  pinMode(4, OUTPUT);

  /** Radio interface */
  SPI.begin();
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node);

  /** Initialization of speaker output interruptions in period (microsecs is parameter)*/
  Timer1.initialize(hercToMicroSec(herces));
  Timer1.attachInterrupt(interruptHandler);

  /** Default turn on lamp */
  analogWrite(3, 255);
}

void loop(void) {

  /** Check the network regularly to not miss messages*/
  network.update();

  /** Is there anything in network for read? */
  while ( network.available() ) {

    RF24NetworkHeader header;
    payload_t payload;
    network.read(header, &payload, sizeof(payload));

    /** Print out received message */
    Serial.print("Received packet value:");
    Serial.print(payload.value);
    Serial.print(" id: ");
    Serial.print(payload.id);
    Serial.print(" type: ");
    Serial.println(payload.type);

    /** Check device which message concern */
    switch (payload.id) {
      case 0:
        /** If put */
        if (payload.type) {
          analogWrite(3, payload.value);
          if (payload.value > 128)
            state = 1;
          else
            state = 0;
          sendAck(payload.id);
        } else {
          sendGet(state, payload.id);
        }
        break;
      case 1:
        /** If put */
        if (payload.type) {
          Timer1.initialize(hercToMicroSec(payload.value));
          sendAck(payload.id);
        } else {
          unsigned int hercesLong = herces;
          sendGet(hercesLong, payload.id);
        }
        break;
      case 2:
        /** If get */
        if (payload.type == 0) {
          unsigned int isCarrier = radio.testCarrier();
          sendGet(isCarrier, payload.id);
        }
        break;
      case 3:
        /** If get */
        if (payload.type == 0)
        {
          unsigned int isCarrierGreaterThanMinus64dB = radio.testRPD();
          sendGet(isCarrierGreaterThanMinus64dB, payload.id);
        }
        break;
      case 4:
        /** If get */
        if (payload.type == 0)
        {
          rf24_datarate_e dataRate = radio.getDataRate();

          unsigned int dataRateLong;
          switch (dataRate) {
            case 0:
              dataRateLong = 1; // 1MBPS
              break;
            case 1:
              dataRateLong = 2; // 2MBPS
              break;
            case 2:
              dataRateLong = 250; // 250KBPS
              break;
          }
          sendGet(dataRateLong, payload.id);
        }
        break;
      default :
        break;
    }
  }
}

/** Send reply on get message */
void sendGet(unsigned long value, unsigned long id)
{
  Serial.print("Sending GET WITH ACK...");
  sendThroughRF24(value, id, 2);
}

/** Acknolegment of the message will be sent */
void sendAck(unsigned long id)
{
  Serial.print("Sending ACK...");
  sendThroughRF24(0, id, 2);
}

/** Sending to server node through RF radio */
void sendThroughRF24(unsigned long value, unsigned long id, unsigned long type)
{
  payload_t payload = { value, id, type };
  RF24NetworkHeader header(/*to node*/ serverNodeId);
  bool ok = network.write(header, &payload, sizeof(payload));
  if (ok)
    Serial.println("ok.");
  else
    Serial.println("failed.");
}

/** Convert Herc to micro sec.*/
int hercToMicroSec(int herc) {
  return 1000000 / herc;
}

/** Handler invoked every 1000000/herces microsecs */
void interruptHandler() {
  digitalWrite(4, digitalRead(9) ^ 1); //tone generation (changes pin9: 0 and 1)
}

