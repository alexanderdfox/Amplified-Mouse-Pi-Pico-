/**
 * Runtime settings for amplified mouse (stored in flash, load at boot).
 * Defaults come from config.h; flash overrides if valid.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#define SETTINGS_NUM_MICE_MIN  2
#define SETTINGS_NUM_MICE_MAX  6
#define SETTINGS_LOGIC_SUM      0
#define SETTINGS_LOGIC_AVERAGE  1
#define SETTINGS_LOGIC_MAX      2
#define SETTINGS_LOGIC_2_MIN    3
#define SETTINGS_LOGIC_2_AND    4
#define SETTINGS_LOGIC_2_OR     5
#define SETTINGS_LOGIC_2_XOR    6
#define SETTINGS_LOGIC_2_NAND   7
#define SETTINGS_LOGIC_2_NOR    8
#define SETTINGS_LOGIC_2_XNOR   9
#define SETTINGS_INPUT_UART         0
#define SETTINGS_INPUT_QUADRATURE  1
#define SETTINGS_INPUT_BOTH         2
#define SETTINGS_OUTPUT_COMBINED   0   /* single combined mouse (instance 0) */
#define SETTINGS_OUTPUT_SEPARATE   1   /* 6 separate mice (instances 0..5) */

typedef struct {
  uint8_t num_mice;      /* 2..6 */
  uint8_t logic_mode;
  uint8_t input_mode;
  uint8_t output_mode;   /* combined (0) or separate (1) */
  float amplify;
  uint16_t quad_scale;
} settings_t;

/* Load defaults from config.h, then try load from flash. Call once at boot. */
void settings_init(void);

/* Pointer to current settings (valid after settings_init). */
const settings_t *settings_get(void);

/* Update a single setting (e.g. from UART). Values are clamped. */
void settings_set_num_mice(uint8_t n);
void settings_set_logic_mode(uint8_t m);
void settings_set_input_mode(uint8_t m);
void settings_set_amplify(float a);
void settings_set_quad_scale(uint16_t q);

/* Apply raw bytes from UART: num_mice, logic_mode, input_mode, output_mode, amplify_x100, quad_scale (2 bytes). */
void settings_apply_uart(uint8_t num_mice, uint8_t logic_mode, uint8_t input_mode,
                         uint8_t output_mode, uint8_t amplify_x100, uint16_t quad_scale);

/* Persist current settings to flash. Returns true on success. */
bool settings_save_to_flash(void);

#endif
