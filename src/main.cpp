#include "SensorMesh.h"

enum gw_mode_t {GW, CONFIG, BOTH};
gw_mode_t gw_mode = BOTH;

#ifndef SERIAL_GW
  #define SERIAL_GW Serial
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables), 
       battery_data(2, 60*60)    // 2 values, check every hour
  {
  }

protected:
  /* ========================== custom logic here ========================== */
  Trigger low_batt, critical_batt, serial;
  TimeSeriesData  battery_data;

  void onSensorDataRead() override {
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);

    battery_data.recordData(getRTCClock(), batt_voltage);   // record battery
    /* no alert
    alertIf(batt_voltage < 3.4f, critical_batt, HIGH_PRI_ALERT, "Battery is critical!");
    alertIf(batt_voltage < 3.6f, low_batt, LOW_PRI_ALERT, "Battery is low");
    */
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg dest[], int max_num) override {
    battery_data.calcMinMaxAvg(getRTCClock(), start_secs_ago, end_secs_ago, &dest[0], TELEM_CHANNEL_SELF, LPP_VOLTAGE);
    return 1;
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
    if (strcmp(command, "magic") == 0) {    // example 'custom' command handling
      strcpy(reply, "**Magic now done**");
      return true;   // handled
    } else if (memcmp(command, "sout ", 5) == 0) {
      if (gw_mode != CONFIG) {
        SERIAL_GW.println(&command[5]);
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "config mode");
      }
      return true;
    }
    return false;  // not handled
  }

  bool handleIncomingMsg(ContactInfo& from, uint32_t timestamp, uint8_t* data, uint flags, size_t len) override {
    if (len > 3 && !memcmp(data, "s> ", 3) && gw_mode != CONFIG) {
      data[len] = 0;
      SERIAL_GW.println((char*)&data[3]);
      return true;
    }

    return SensorMesh::handleIncomingMsg(from, timestamp, data, flags, len);
  }

public:
  void loop() {
    SensorMesh::loop();

    if (gw_mode != CONFIG) {
      static char in_data[156];
      static char out_data[160] = "s> ";

      int len = strlen(in_data);
      while (SERIAL_GW.available() && len < 155) {
        char c = SERIAL_GW.read();
        if (c != '\n') {
          in_data[len++] = c;
          in_data[len] = 0;
        }
      }
      if (len == 155) {  // buffer full ... send
        in_data[155] = '\r';
      }

      if (len > 0 && in_data[len - 1] == '\r') {  // received complete line
        serial.text[0] = 0; // retrigger serial alert
        in_data[len - 1] = 0;  // replace newline with C string null terminator
        strncpy(&out_data[3], in_data, 156);
        alertIf(true, serial, HIGH_PRI_ALERT, out_data);

        in_data[0] = 0;  // reset buffer
      }
    }
  }
  /* ======================================================================= */
};

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

void setup() {

#ifdef SGW_RX
  #if defined(NRF52_PLATFORM) || defined(ESP32)
    SERIAL_GW.setPins(SGW_RX, SGW_TX);
  #elif defined(STM32_PLATFORM)
    SERIAL_GW.setRx(SGW_RX);
    SERIAL_GW.setTx(SGW_TX);
  #endif
#endif

  Serial.begin(115200);
  if (SERIAL_GW != Serial) 
    SERIAL_GW.begin(115200);

  delay(1000);

  board.begin();

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

#ifdef MODE_PIN
  #ifndef MODE_PIN_CONFIG
    #define MODE_PIN_CONFIG LOW
  #endif
  #ifndef MODE_PIN_MODE
    #define MODE_PIN_MODE INPUT
  #endif
  pinMode(MODE_PIN, MODE_PIN_MODE);
  if (digitalRead(MODE_PIN) == MODE_PIN_CONFIG) {
    gw_mode = CONFIG;
  } else {
    gw_mode = GW;
  }
#endif

  if (gw_mode != GW) {
    Serial.print("Sensor ID: ");
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
  }
  command[0] = 0;

  sensors.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
#endif

  the_mesh.begin(fs);

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  if (!gw_mode == GW) { // don't process Serial if in GW only mode
    int len = strlen(command);
    while (Serial.available() && len < sizeof(command)-1) {
      char c = Serial.read();
      if (c != '\n') {
        command[len++] = c;
        command[len] = 0;
      }
      Serial.print(c);
    }
    if (len == sizeof(command)-1) {  // command buffer full
      command[sizeof(command)-1] = '\r';
    }

    if (len > 0 && command[len - 1] == '\r') {  // received complete line
      command[len - 1] = 0;  // replace newline with C string null terminator
      char reply[160];
      the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
      if (reply[0]) {
        Serial.print("  -> "); Serial.println(reply);
      }

      command[0] = 0;  // reset command buffer
    }
  }
  the_mesh.loop();
  sensors.loop();
}
