# Remote Serial Console over Telnet (ESP32-C3)

An **out-of-band remote serial console** using the **ESP32-C3** to provide emergency access to a headless server or embedded system when primary network services fail.

The ESP32-C3 acts as a **network-to-serial bridge**: it connects to your target device’s serial console via its internal USB CDC interface and exposes it securely over a **password-protected Telnet server** on a static IP.


## Overview and features

Suitable for ESP32-C3 boards with native USB-Serial bridge (Super Mini or Waveshare ESP32-C3-Zero).

- **Local Management**: Use UART0 for device configuration & debugging
- **Static IP** Telnet server on configurable IP and port
- **Low level USB CDC I/O** to avoid `usb_serial_jtag` driver (ESP-IDF v5.5.2 seems still buggy).
- **Telnet server**: support negotiation to supress local echo. Strips IAC/DO/WILL commands to keep terminal clean. Handles all Telnet newlines (LF, CR-LF, CR-NUL).
- **Session authentication**: Password optional but recommended since the remote serial console could be left open.


## Operation

### Monitor and configure

Connect to UART0.

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

Default password is **secret**. Set `sespass` to "" to disable it.

After reboot, check WiFi connection logs, etc.

You can reconfigure options at any moment. To show  current configuration:

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

### Spawn a terminal in the Linux box (for testing)

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

### Setup a terminal in the Linux box (systemd)

Check for USB properties of your device:

```
# udevadm info -a -n /dev/ttyACM0

...

  looking at parent device '/devices/pci0000:00/0000:00:11.0/0000:02:01.0/usb2/2-2/2-2.1':

    ATTRS{idProduct}=="1001"
    ATTRS{idVendor}=="303a"
    ATTRS{manufacturer}=="Espressif"
    ATTRS{product}=="USB JTAG/serial debug unit"
    ATTRS{serial}=="B8:F8:62:2D:70:AC"
...
```

Choose the most relevant for your case and create a new udev rule:

```
# cat /etc/udev/rules.d/99-esp32-console.rules

KERNEL=="ttyACM*", \
SUBSYSTEMS=="usb", \
ATTRS{idVendor}=="303a", \
ATTRS{idProduct}=="1001", \
SYMLINK+="ttyESP32"
```

This rule will create a symlink called `/dev/ttyESP32` to `/dev/ttyACMx` each time you plug your ESP32:

```
lrwxrwxrwx 1 root root          7 Jan  4 13:55 /dev/ttyESP32 -> ttyACM0
crw-rw---- 1 root dialout 166,  0 Jan  4 13:55 /dev/ttyACM0
```

Now you could do `agetty -L ttyESP32`, so you can leverage systemd tty generator:

```
systemctl enable --now getty@ttyESP32.service
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

