/**
  ******************************************************************************
  * @file    usbd_midi.h
  * @brief   Class-compliant USB-MIDI 1.0 device (single embedded port).
  *          Bulk-OUT carries host->device MIDI; parsed events are forwarded to
  *          the app via app_midi_event(). Bulk-IN is declared for compliance but
  *          unused. See usb-midi.md.
  ******************************************************************************
  */

#ifndef __USBD_MIDI_H
#define __USBD_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

/* Endpoints (OTG-FS EP1; same pair the CDC build uses for its data EP). */
#define MIDI_IN_EP                      0x81U   /* device -> host (unused) */
#define MIDI_OUT_EP                     0x01U   /* host -> device (notes)  */
#define MIDI_DATA_FS_MAX_PACKET_SIZE    64U

/* Full config descriptor length: 9 (cfg) + 9 (AC if) + 9 (AC hdr)
 * + 9 (MS if) + 7 (MS hdr) + 6 + 6 (IN jacks) + 9 + 9 (OUT jacks)
 * + 9 + 5 (OUT ep + CS) + 9 + 5 (IN ep + CS) = 101. */
#define USB_MIDI_CONFIG_DESC_SIZ        101U

extern USBD_ClassTypeDef USBD_MIDI;

/* True once the host has configured the device (for the UI screen). */
uint8_t USBD_MIDI_IsConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_MIDI_H */
