/**
 * Runtime settings: defaults from config.h, optional load/save in flash.
 */
#include "settings.h"
#include "config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

#define SETTINGS_MAGIC   "AMCF"
#define SETTINGS_OFFSET  (PICO_FLASH_SIZE_BYTES - 4096)  /* last 4K sector */
#define SETTINGS_PAYLOAD_LEN  8  /* num_mice, logic, input, amplify_x100, quad_scale(2), reserved(2) */

static settings_t g_settings;

static uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

static void clamp_settings(void) {
  if (g_settings.num_mice < SETTINGS_NUM_MICE_MIN) g_settings.num_mice = SETTINGS_NUM_MICE_MIN;
  if (g_settings.num_mice > SETTINGS_NUM_MICE_MAX) g_settings.num_mice = SETTINGS_NUM_MICE_MAX;
  if (g_settings.logic_mode > SETTINGS_LOGIC_2_XNOR) g_settings.logic_mode = SETTINGS_LOGIC_SUM;
  if (g_settings.input_mode > SETTINGS_INPUT_BOTH) g_settings.input_mode = SETTINGS_INPUT_UART;
  if (g_settings.amplify < 0.1f) g_settings.amplify = 0.1f;
  if (g_settings.amplify > 10.0f) g_settings.amplify = 10.0f;
  if (g_settings.quad_scale < 1) g_settings.quad_scale = 1;
  if (g_settings.quad_scale > 1000) g_settings.quad_scale = 1000;
}

void settings_init(void) {
  /* Defaults from config.h */
  g_settings.num_mice   = (uint8_t)NUM_MICE;
  g_settings.logic_mode = (uint8_t)LOGIC_MODE;
  g_settings.input_mode = (uint8_t)INPUT_MODE;
  g_settings.amplify    = AMPLIFY;
  g_settings.quad_scale = (uint16_t)QUAD_SCALE;
  clamp_settings();

  /* Try load from flash */
  const uint8_t *flash = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + SETTINGS_OFFSET);
  if (memcmp(flash, SETTINGS_MAGIC, 4) != 0)
    return;
  uint8_t payload[SETTINGS_PAYLOAD_LEN];
  memcpy(payload, flash + 4, SETTINGS_PAYLOAD_LEN);
  if (crc8(payload, SETTINGS_PAYLOAD_LEN) != flash[4 + SETTINGS_PAYLOAD_LEN])
    return;
  g_settings.num_mice   = payload[0];
  g_settings.logic_mode = payload[1];
  g_settings.input_mode = payload[2];
  g_settings.amplify    = (float)payload[3] / 100.0f;
  g_settings.quad_scale = (uint16_t)payload[4] | ((uint16_t)payload[5] << 8);
  clamp_settings();
}

const settings_t *settings_get(void) {
  return &g_settings;
}

void settings_set_num_mice(uint8_t n) {
  g_settings.num_mice = n;
  clamp_settings();
}

void settings_set_logic_mode(uint8_t m) {
  g_settings.logic_mode = m;
  clamp_settings();
}

void settings_set_input_mode(uint8_t m) {
  g_settings.input_mode = m;
  clamp_settings();
}

void settings_set_amplify(float a) {
  g_settings.amplify = a;
  clamp_settings();
}

void settings_set_quad_scale(uint16_t q) {
  g_settings.quad_scale = q;
  clamp_settings();
}

void settings_apply_uart(uint8_t num_mice, uint8_t logic_mode, uint8_t input_mode,
                         uint8_t amplify_x100, uint16_t quad_scale) {
  g_settings.num_mice   = num_mice;
  g_settings.logic_mode = logic_mode;
  g_settings.input_mode = input_mode;
  g_settings.amplify    = (float)amplify_x100 / 100.0f;
  g_settings.quad_scale = quad_scale;
  clamp_settings();
}

bool settings_save_to_flash(void) {
  uint8_t buf[4 + SETTINGS_PAYLOAD_LEN + 1];
  memcpy(buf, SETTINGS_MAGIC, 4);
  buf[4] = g_settings.num_mice;
  buf[5] = g_settings.logic_mode;
  buf[6] = g_settings.input_mode;
  buf[7] = (uint8_t)((int)(g_settings.amplify * 100.0f) % 256);
  buf[8] = (uint8_t)(g_settings.quad_scale & 0xFF);
  buf[9] = (uint8_t)(g_settings.quad_scale >> 8);
  buf[10] = 0;
  buf[11] = 0;
  buf[12] = crc8(buf + 4, SETTINGS_PAYLOAD_LEN);

  uint32_t irq = save_and_disable_interrupts();
  flash_range_erase(SETTINGS_OFFSET, 4096);
  flash_range_program(SETTINGS_OFFSET, buf, sizeof(buf));
  restore_interrupts(irq);
  return true;
}
