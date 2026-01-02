# Remote Serial Console over Telnet (ESP32-C3)

An **out-of-band remote serial console** using the **ESP32-C3** to provide emergency access to a headless server or embedded system when primary network services fail.

The ESP32-C3 acts as a **network-to-serial bridge**: it connects to your target device’s serial console via its internal USB CDC interface and exposes it securely over a **password-protected Telnet server** on a static IP.


## Overview and features

Flash the firmware into an ESP32-C3 board (Super Mini or Waveshare ESP32-C3-Zero).

- **Hardware**: ESP32-C3 board (built-in USB-JTAG + USB CDC)
- **Target Console**: Linux connected to ESP32-C3 USB CDC native USB peripheral
- **Local Management**: UART0 used for device configuration & debugging
- **Network Access**: Static IP Telnet server on configurable IP and port


- **Low level USB CDC I/O**: Direct USB CDC read/write to avoid `usb_serial_jtag` driver (ESP-IDF v5.5.2 seems still buggy).
- **Telnet negotiation support**: strips IAC/DO/WILL commands to keep terminal clean, disables local echo.
- **Session password authentication**: Optional but recommended since the remote serial console could be left open. Bruteforce prevention.
- **Newline translation**: translates Telnet newline (CR LF) into serial newline (LF).


## Operation

### Use UART0 to monitor and configure

```
W (302) main: Device is not configured yet.

=== ESP32 Console Ready ===
=== Configuration Status ===
Device is INCOMPLETELY CONFIGURED.

=== Configuration Values ===
ssid:    (not set)
pass:    (not set)
ip:      (not set)
mask:    255.255.255.0
gw:      192.168.1.1
port:    23
sespass: *** (set)

Please configure the device. Reboot when ready.

Type 'help' to see available commands.

esp>
```

Default password is **secret**.

Configure options:

```
esp> ssid "My wifi AP"
OK
esp> pass mypass
OK
esp> ip 192.168.1.4
OK
esp> sespass secret
OK
esp> reboot
Rebooting...
```

Set **sespass** to "" to disable it.

Check connection logs, etc.

Check configuration:

```
esp> conf
=== Configuration Status ===
Device is FULLY CONFIGURED and ready to connect.

=== Configuration Values ===
ssid:    "My wifi AP"
pass:    *** (set)
ip:      192.168.1.4
mask:    255.255.255.0
gw:      192.168.1.1
port:    23
sespass: *** (set)
```

You can reconfigure options at any moment.


### Spawn a terminal in the Linux box

Plug the board to the Linux box.

Identify the tty assigned to the ESP32 (could be different in each reboot).

```bash
# readlink  /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*
../../ttyACM0
```

Get the device name:

```bash
# basename ../../ttyACM0
ttyACM0
```

Spawn a serial console:

```bash
# setsid \
  agetty -L \
  $(basename $(readlink /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*))\
  linux
```

### Access console remotely

Telnet to configured IP and port. Using a telnet client: Putty, Telnet, etc.

```bash
telnet 192.168.1.4 23
```

Example session:

```
Remote Console Telnet Server
Password: ******

--- Remote console open ---
(Telnet EOF or disconnect to close session)


Ubuntu 18.04.2 LTS cuadrado ttyACM0

cuadrado login: reinoso
Password:
Last login: Fri Jan  2 12:47:07 CET 2026 on ttyACM0
Welcome to Ubuntu 18.04.2 LTS (GNU/Linux 4.15.0-55-generic x86_64)

reinoso@cuadrado:~$
```

Error when you try to send bytes to Linux when ESP32 is not connected, or agetty is not running at the other end:

```
--- Remote console open ---
(Telnet EOF or disconnect to close session)


--- Remote end not listening ---

```

## Security Notes

- Telnet traffic is **unencrypted**.
- Session requires password, but credentials are sent in plaintext.


## Build and Flash

Tested on ESP-IDF version: v5.5.2

```
idf.py set-target esp32c3
idf.py menuconfig
idf.py build
idf.py flash
```

Options for menuconfig:

- Component config → ESP System Settings → Channel for console output: **Default: UART0**
- Component config → ESP System Settings → Channel for console secondary output: **No secondary console**


## Author

Reinoso Guzman (https://www.electronicayciencia.com).


## License

The MIT License (MIT).

