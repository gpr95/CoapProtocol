#include <TimerOne.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>


RF24 radio(7, 8);               // nRF24L01(+) radio attached using Getting Started board


RF24Network network(radio);      // Network uses that radio
const uint16_t this_node = 01;    // Address of our node in Octal format ( 04,031, etc)
const uint16_t serverNodeId = 00;   // Address of the other node in Octal format

int id = 0;

/**
   value: value to put
   id: 0 - lamp, 1 - speaker, 2 - rf_is_carrier_on_the_line , 3 - rf_is_carrier_greater_than_minus_64dB, 4 - rf_data_rate
   type: 0 - get, 1 - put, 2 ACK
*/
struct payload_t {                 // Structure of our payload
  unsigned long value;
  unsigned long id;
  unsigned long type;
};

int state = 1; // lamp state
int herces = 300;
void setup(void)
{
  Serial.begin(57600);
  Serial.println("ArduinoMini_IOT_ELEMENT");
  pinMode(3, OUTPUT); // lamp
  pinMode(4, OUTPUT); // speaker


  SPI.begin();
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node);

  // Init speaker
  Timer1.initialize(hercToMicroSec(herces));
  Timer1.attachInterrupt(interruptHandler);
  
  analogWrite(3, 255); // LAMP is default turned on
}

void loop(void) {

  network.update();                  // Check the network regularly


  while ( network.available() ) {     // Is there anything ready for us?

    RF24NetworkHeader header;        // If so, grab it and print it out
    payload_t payload;
    network.read(header, &payload, sizeof(payload));

    // Print payload
    Serial.print("Received packet value:");
    Serial.print(payload.value);
    Serial.print(" id: ");
    Serial.print(payload.id);
    Serial.print(" type: ");
    Serial.println(payload.type);

    if (payload.id == 0) {
      if (payload.type == 0)
      {
        sendGet(state,payload.id);
      }
      else if (payload.type == 1)
      {
        analogWrite(3, payload.value);
        if (payload.value > 128)
          state = 1;
        else
          state = 0;
        sendAck(id);
       
      }
    } else if (payload.id == 1) {
      if (payload.type == 0)
      {
        unsigned int hercesLong = herces;
        sendGet(hercesLong,payload.id);
      }
      else if (payload.type == 1)
      {
         // Init speaker
        Timer1.initialize(hercToMicroSec(payload.value));
        sendAck(id);
      }
    } else if (payload.id == 2) {
      if (payload.type == 0)
      {
        unsigned int isCarrier = radio.testCarrier();
        sendGet(isCarrier,payload.id);
      }
    } else if (payload.id == 3) {
      if (payload.type == 0)
      {
        unsigned int isCarrierGreaterThanMinus64dB = radio.testRPD();
        sendGet(isCarrierGreaterThanMinus64dB,payload.id);
      }
    } else if (payload.id == 4) {
       if (payload.type == 0)
      {
        rf24_datarate_e dataRate = radio.getDataRate();
        
        unsigned int dataRateLong;
        switch(dataRate) {
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
        sendGet(dataRateLong,payload.id);
      }
    }
  }
}


void sendGet(unsigned long value, unsigned long id)
{
  Serial.print("Sending GET WITH ACK...");
  sendThroughRF24(value, id, 2);
}
void sendAck(unsigned long value)
{
  Serial.print("Sending ACK...");
  sendThroughRF24(0, id, 2);
}

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

int hercToMicroSec(int herc) {
  return 1000000/herc;
}

void interruptHandler(){
 digitalWrite(4,digitalRead(9)^1);//tone generation (changes pin9: 0 and 1)
}

