// usb_reader.h - USB background reader task for ESP32

#ifndef USB_READER_H
#define USB_READER_H

#include <stdint.h>
#include <stdbool.h>

// arg: ring buffer to write into
void usb_reader_task(void *arg);

#endif // USB_READER_H