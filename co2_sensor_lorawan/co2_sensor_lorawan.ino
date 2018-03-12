#include <lmic.h>
#include <hal/hal.h>
#include <SoftwareSerial.h>
#include "mhz19.h"

/** CONFIGURE THIS **/

//Sensor pin connection
const int rxPin = 4; //connect to tx on sensor
const int txPin = 5; //connect to rx on sensor

//The Things Network credentials. Replace with own keys.
static const u1_t NWKSKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u1_t APPSKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u4_t DEVADDR = 0x00000000;
const unsigned TX_INTERVAL = 20;

/** END OF CONFIGURATION **/



static osjob_t sendjob;

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

SoftwareSerial sensor(rxPin, txPin); // RX, TX

//Store latest sensor data
int latestCO2;
int latestTemp;

// Pin mapping Dragino Shield
const lmic_pinmap lmic_pins = {
    .nss = 10,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 9,
    .dio = {2, 6, 7},
};
void onEvent (ev_t ev) {
    if (ev == EV_TXCOMPLETE) {
        Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
        // Schedule next transmission
        os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
    }
}

void do_send(osjob_t* j){
    // Payload to send (uplink)
    struct{
      int CO2;
      int temp;
    }sensorData;
    sensorData.CO2 = latestCO2;
    sensorData.temp = latestTemp;
    
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, (unsigned char *)&sensorData, sizeof(sensorData), 0);
        Serial.println(F("Sending uplink packet..."));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}


static bool exchange_command(uint8_t cmd, uint8_t data[], int timeout)
{
    // create command buffer
    uint8_t buf[9];
    int len = prepare_tx(cmd, data, buf, sizeof(buf));

    // send the command
    sensor.write(buf, len);

    // wait for response
    long start = millis();
    while ((millis() - start) < timeout) {
        if (sensor.available() > 0) {
            uint8_t b = sensor.read();
            if (process_rx(b, cmd, data)) {
                return true;
            }
        }
    }

    return false;
}

static bool read_temp_co2(int *co2, int *temp)
{
    uint8_t data[] = {0, 0, 0, 0, 0, 0};
    bool result = exchange_command(0x86, data, 3000);
    if (result) {
        *co2 = (data[0] << 8) + data[1];
        *temp = data[2] - 40;
#if 1
        char raw[32];
        sprintf(raw, "RAW: %02X %02X %02X %02X %02X %02X", data[0], data[1], data[2], data[3], data[4], data[5]);
        Serial.println(raw);
#endif
    }
    return result;
}

void setup() {
  Serial.begin(115200);
  sensor.begin(9600);
  Serial.println(F("CO2 Sensor reader startup"));

  //DRAGINO INITIALIZATION
  // LMIC init
  os_init();
  
  //  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  
  //  // Set static session parameters.
  LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
  
  //  // Disable link check validation
  LMIC_setLinkCheckMode(0);
  
  // Use spreading factor 7
  LMIC.dn2Dr = DR_SF7;
  
//  // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
  LMIC_setDrTxpow(DR_SF7,14);
  
//  // Start job
  do_send(&sendjob);
}

void loop() { // run over and over
    if (read_temp_co2(&latestCO2, &latestTemp)) {
        Serial.print("CO2:");
        Serial.println(latestCO2, DEC);
        Serial.print("TEMP:");
        Serial.println(latestTemp, DEC);
    }
    os_runloop_once();
    delay(5000);
}

