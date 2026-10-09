#ifndef SB_DIGITAL_IO_PICO
#define SB_DIGITAL_IO_PICO

#include "hardware/gpio.h"
#include "hardware/structs/sio.h"


#define SB_PIN_MODE_OUTPUT(pin) gpio_set_dir(pin, true)
#define SB_PIN_MODE_INPUT(pin) gpio_set_dir(pin, false)

#define SB_digitalWrite(pin, out) ((out) ? (sio_hw->gpio_set = 1u << (pin)) : (sio_hw->gpio_clr = 1u << (pin)))
#define SB_digitalWrite1(pin, out) do {        \
    uint32_t _m = 1u << (pin);                \
    if (out) sio_hw->gpio_set = _m;           \
    else     sio_hw->gpio_clr = _m;           \
} while (0)
#define SB_write_mask(mask, out) do { \
    if (out) sio_hw->gpio_set = (mask); \
    else     sio_hw->gpio_clr = (mask); \
} while (0)

#define SB_digitalRead(pin) gpio_get(pin)

#endif