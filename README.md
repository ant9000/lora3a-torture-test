DESCRIPTION
-----------

This firmware can be used as a basis for exploring the harvesting capabilities of Lora3a Sensor1 node. It is based on [RIOT](https://github.com/RIOT-OS/RIOT) - The friendly OS for IoT.

The sensor node wakes up periodically. It reads its sensors and broadcasts the measures, waits briefly for a command from the gateway, then gets back to a low power sleep.

The Lora3a Dongle can be made into a gateway, which presents itself to the host computer as a serial port over USB. Connecting to the serial port, you will see the received packets printed out.

INSTALL
-------

Make sure you have a toolchain for ARM. On Debian 10, it is as simple as

```
sudo apt install gcc-arm-none-eabi
```

For the general case, see the [RIOT documentation](https://github.com/RIOT-OS/RIOT/wiki/Family:-ARM).

Then you need RIOT itself, the lora3a boards definition, and the present firmware:


```
git clone https://github.com/RIOT-OS/RIOT
git clone https://github.com/ant9000/lora3a-boards
git clone https://github.com/ant9000/lora3a-torture-test
cd lora3a-torture-test
```

USAGE
-----

To compile firmware for a sensor node and flash it, the command needed is:

```
make -j$(nproc) flash
```

A firmware for using a Lora3a Dongle as gateway is flashed like this:

```
make -j$(nproc) flash ROLE=gateway
```

ENCRYPTION
----------

All transmitted data is encrypted (and hashed) with AES-GCM-128. The default encryption key can be changed via `AES_KEY` variable; its value should be a 32 characters hex string. Both the gateway _and_ the nodes need to be flashed with the same key, in order to be able to communicate:

```
make -j$(nproc) flash AES_KEY=...
```

Encryption can also be disabled, using `AES=0` on the command line when flashing. Again, this needs to be done on both the gateway and the nodes:

```
make -j$(nproc) flash AES=0
```

ADDRESSING
----------

Each network participant has an address. Default values are 1 for a node, and 254 for the gateway. More nodes can be added by flashing each of them with a unique `ADDRESS=n`, with `n` in the range 1-254 (inclusive):

```
make -j$(nproc) flash ADDRESS=42
```

Nodes send their packets to the broadcast address 255; the gateway answers will be node specific.

SERIAL CONNECTION
-----------------

The Lora3a Dongle can be flashed and a serial connection be opened to it in one go:

```
make -j$(nproc) flash term ROLE=gateway
```

RIOT expects the dongle to be available on port `/dev/ttyACM0`; this can be changed via the `PORT` command-line variable.

If a serial cable is attached to it, also the sensor node can be attached to, with the same logic:

```
make term PORT=/dev/ttyXXX
```
