# MCSerialGW

RS232 Gateway using MeshCore as described in [this post](https://digitalsober.wordpress.com/2025/08/30/serial-gateways-for-meshcore/).

SerialGW sends data received from a Serial port to LORA and prints messages received from LORA to serial.

## Configuration

Some configuration is necessary at compile time to define the serial port and eventually a mode button (to specify mode at boot).

Configuration of serial port is done with the three following symbols:
* `SERIAL_GW` specifies the arduino `SerialXX` object to use (defaults to `Serial`)
* `SGW_RX` overrides the RX pin
* `SGW_TX` overrides the TX pin
* `SERIAL_GW_BAUDRAITE` specifies the baudrate used by the GW (115200 by default)

When using the main serial port, mode pin permits to choose the function of the port at boot (command/data), the pin will be read a little more than 1s after reset, so the boot pin on ESP32 can be used (but not pressed during boot) :
* `MODE_PIN` specifies the pin used
* `MODE_PIN_CONFIG` specifies the state that leads to config mode (default is `LOW`)
* `MODE_PIN_MODE` defines the direction of the pin (`INPUT`/`INPUT_PULLUP`/`INPUT_PULLDOWN`, default is `INPUT`)

## CLI Commands

The main cli command is `sout`, which writes its arguments on the serial port.

`config` command accepts two parameters `on` or `off` and permits to enter or leave config mode when only one serial port is used.

## Using setperm to control senders and receivers

SerialGW uses the ACL mechanism of sensor nodes to control receivers and senders.

To send messages through the gateway, a node must be admin and messages should start with `s> ` indicating that the message is a serial one. So every possible sender should have at least a value of `03` in the ACL. This can be done by issuing a `setperm <sender_key> 3` command (in mccli you can use the name).

To have serial messages sent from the gateway, the receivers should accept notifications and so should have a `c` as first nibble of the ACL value. For a receiver that can also be sender, one can do `setperm <receiver_key> c3` (`c3` and `3` are the most classical permission used in this firmware).

Beware that the current behaviour is to send message to all receivers in turn. First receiver will be sent the message in three consecutive tentatives with timeout. This is not ideal as the wait can be long but the implementation is simpler ... The advice is to try to limit the receivers or put reliable ones.

