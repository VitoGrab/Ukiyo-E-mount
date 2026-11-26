#ifndef UKIYO_PORT_BOARD_H
#define UKIYO_PORT_BOARD_H
#include <stdint.h>
#include <sam.h>
// Arduino XIAO pin numbers, not SAMD21 PORT-A bit indices.
#define EM_PIN_TX 6
#define EM_PIN_RX 7
#define EM_PIN_BODY_VD 8
#define EM_PIN_BODY_CS 9
#define EM_PIN_LENS_CS 10
#define EM_BAUD 0xC000u
#define EM_BAUD_FAST 0x8000u
// Protocol-side focus travel bounds retained from Techart board.h.
// They constrain reported position fields; Ukiyo has no focus motor.
#define ENC_TRAVEL_LO 4144
#define ENC_TRAVEL_HI 5632
#endif
