/**
 * 6-input amplified mouse for Raspberry Pi Pico
 *
 * Aggregates up to 6 mouse inputs (dx, dy, buttons, wheel) into one
 * HID mouse with optional amplification. Input can come from:
 * - UART (e.g. host PC/RPi sending packed deltas)
 * - Quadrature encoders (6 ball mice wired directly: 4 pins per mouse)
 * - Future: USB host (MAX3421E + hub) or SPI optical sensors
 *
 * Build: Pico SDK, TinyUSB device (HID mouse).
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "tusb_config.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define NUM_MICE_MAX    6     /* max mice (array sizes, UART packet) */

/* Logic and input mode symbols (values come from config.h) */
#define LOGIC_MODE_SUM      0
#define LOGIC_MODE_AVERAGE  1
#define LOGIC_MODE_MAX      2
#define LOGIC_MODE_2_MIN    3
#define LOGIC_MODE_2_AND    4
#define LOGIC_MODE_2_OR     5
#define LOGIC_MODE_2_XOR    6
#define LOGIC_MODE_2_NAND   7
#define LOGIC_MODE_2_NOR    8
#define LOGIC_MODE_2_XNOR   9
#define INPUT_MODE_UART         0
#define INPUT_MODE_QUADRATURE   1
#define INPUT_MODE_BOTH         2

#include "config.h"
#include "settings.h"

#if NUM_MICE < 2 || NUM_MICE > NUM_MICE_MAX
#error "NUM_MICE must be between 2 and NUM_MICE_MAX (6). Run configure.py."
#endif

static inline int get_num_mice(void) {
  return (int)settings_get()->num_mice;
}

#define UART_ID         uart0
#define UART_BAUD       115200
#define UART_TX_PIN     0
#define UART_RX_PIN     1
#define HID_POLL_MS     2      /* send HID report every 2 ms when there is movement */

/* Quadrature: 6 mice × 4 pins (X_A, X_B, Y_A, Y_B). Pico GPIO numbers. */
static const uint8_t QUAD_PINS[NUM_MICE_MAX][4] = {
  { 2,  3,  4,  5 },  /* Mouse 0: X_A, X_B, Y_A, Y_B */
  { 6,  7,  8,  9 },  /* Mouse 1 */
  { 10, 11, 12, 13 }, /* Mouse 2 */
  { 14, 15, 16, 17 }, /* Mouse 3 */
  { 18, 19, 20, 21 }, /* Mouse 4 */
  { 22, 23, 24, 25 }, /* Mouse 5 */
};

/* Per-mouse state (from UART or host) */
typedef struct {
  int8_t dx;
  int8_t dy;
  uint8_t buttons;
  int8_t wheel;
} mouse_input_t;

static mouse_input_t g_mice[NUM_MICE_MAX];
static int16_t g_combined_dx, g_combined_dy;
static uint8_t g_combined_buttons;
static int16_t g_combined_wheel;
static bool g_has_report;

/* UART protocol: sync 0xAA then 6 × (dx, dy) then 1 byte buttons, 1 byte wheel (signed).
 * Total 1 + 12 + 1 + 1 = 15 bytes. */
#define UART_SYNC       0xAA
#define UART_PACKET_LEN (1 + NUM_MICE_MAX * 2 + 1 + 1)

static uint8_t uart_buf[UART_PACKET_LEN];
static int uart_len;

/* Config packet: 0x55 0xCF 0x01 then 8 bytes (num_mice, logic, input, output_mode, amplify_x100, quad_lo, quad_hi, save). */
#define UART_CONFIG_HEADER_LEN  3
#define UART_CONFIG_PAYLOAD_LEN 8
static int uart_config_state;
static int uart_config_len;
static uint8_t uart_config_buf[UART_CONFIG_PAYLOAD_LEN];

/* Quadrature decode: prev_ab and curr_ab are 2-bit (A=bit0, B=bit1). Returns -1, 0, or +1. */
static const int8_t quad_table[16] = {
  0, 1, -1, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, -1, 1, 0
};
static uint8_t quad_prev[NUM_MICE_MAX][2];  /* [mouse][0]=X_ab, [1]=Y_ab */
static int16_t quad_acc[NUM_MICE_MAX][2];   /* accumulated counts [mouse][dx,dy] */

static void quadrature_init(void) {
  int n = get_num_mice();
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < 4; j++) {
      gpio_init(QUAD_PINS[i][j]);
      gpio_set_dir(QUAD_PINS[i][j], GPIO_IN);
      gpio_pull_up(QUAD_PINS[i][j]);
    }
    quad_prev[i][0] = (uint8_t)((gpio_get(QUAD_PINS[i][0]) ? 1u : 0u) | (gpio_get(QUAD_PINS[i][1]) ? 2u : 0u));
    quad_prev[i][1] = (uint8_t)((gpio_get(QUAD_PINS[i][2]) ? 1u : 0u) | (gpio_get(QUAD_PINS[i][3]) ? 2u : 0u));
    quad_acc[i][0] = quad_acc[i][1] = 0;
  }
}

static void quadrature_poll(void) {
  int n = get_num_mice();
  uint16_t qs = settings_get()->quad_scale;
  for (int i = 0; i < n; i++) {
    uint8_t x_ab = (uint8_t)((gpio_get(QUAD_PINS[i][0]) ? 1u : 0u) | (gpio_get(QUAD_PINS[i][1]) ? 2u : 0u));
    uint8_t y_ab = (uint8_t)((gpio_get(QUAD_PINS[i][2]) ? 1u : 0u) | (gpio_get(QUAD_PINS[i][3]) ? 2u : 0u));
    int8_t dx = quad_table[(quad_prev[i][0] << 2) | x_ab];
    int8_t dy = quad_table[(quad_prev[i][1] << 2) | y_ab];
    quad_prev[i][0] = x_ab;
    quad_prev[i][1] = y_ab;
    quad_acc[i][0] += dx;
    quad_acc[i][1] += dy;
  }
  /* Convert accumulated counts to g_mice deltas (with scaling) */
  for (int i = 0; i < n; i++) {
    int16_t ax = quad_acc[i][0], ay = quad_acc[i][1];
    int8_t dx = 0, dy = 0;
    if (qs > 0) {
      if (ax >= (int16_t)qs) { dx = (int8_t)(ax / (int16_t)qs); quad_acc[i][0] = (int16_t)(ax % (int16_t)qs); }
      else if (ax <= -(int16_t)qs) { dx = (int8_t)(ax / (int16_t)qs); quad_acc[i][0] = (int16_t)(ax % (int16_t)qs); }
      if (ay >= (int16_t)qs) { dy = (int8_t)(ay / (int16_t)qs); quad_acc[i][1] = (int16_t)(ay % (int16_t)qs); }
      else if (ay <= -(int16_t)qs) { dy = (int8_t)(ay / (int16_t)qs); quad_acc[i][1] = (int16_t)(ay % (int16_t)qs); }
    }
    if (dx != 0 || dy != 0) {
      g_mice[i].dx += dx;
      g_mice[i].dy += dy;
      if (g_mice[i].dx > 127) g_mice[i].dx = 127;
      if (g_mice[i].dx < -128) g_mice[i].dx = -128;
      if (g_mice[i].dy > 127) g_mice[i].dy = 127;
      if (g_mice[i].dy < -128) g_mice[i].dy = -128;
    }
  }
}

static void inputs_reset(void) {
  memset(g_mice, 0, sizeof(g_mice));
  g_combined_dx = g_combined_dy = 0;
  g_combined_buttons = 0;
  g_combined_wheel = 0;
  g_has_report = false;
}

/* 2-ball logic: compute one axis from A and B (signed 8-bit). Returns combined value. */
static int32_t logic2_axis(uint8_t mode, int8_t A, int8_t B) {
  int32_t a = (int32_t)A, b = (int32_t)B;
  int32_t aa = a < 0 ? -a : a, ab = b < 0 ? -b : b;
  switch (mode) {
    case LOGIC_MODE_2_MIN:  return aa <= ab ? a : b;
    case LOGIC_MODE_2_AND:
      if (a == 0 || b == 0) return 0;
      if ((a > 0) != (b > 0)) return 0;
      return aa <= ab ? a : b;
    case LOGIC_MODE_2_OR:   return a + b;
    case LOGIC_MODE_2_XOR:
      if (a == 0) return b;
      if (b == 0) return a;
      return a - b;
    case LOGIC_MODE_2_NAND:
      if (a != 0 && b != 0) return 0;
      return a + b;
    case LOGIC_MODE_2_NOR:  return 0;
    case LOGIC_MODE_2_XNOR:
      if (a == 0 && b == 0) return 0;
      if (a == 0) return b;
      if (b == 0) return a;
      if ((a > 0) != (b > 0)) return 0;
      return (a + b) / 2;
    default: return a + b;
  }
}

static void aggregate_and_amplify(void) {
  int32_t dx = 0, dy = 0;
  const settings_t *s = settings_get();
  int n = get_num_mice();
  uint8_t lm = s->logic_mode;

  if (lm == LOGIC_MODE_SUM) {
    for (int i = 0; i < n; i++) {
      dx += g_mice[i].dx;
      dy += g_mice[i].dy;
    }
  } else if (lm == LOGIC_MODE_AVERAGE) {
    for (int i = 0; i < n; i++) {
      dx += g_mice[i].dx;
      dy += g_mice[i].dy;
    }
    if (n > 0) {
      dx /= n;
      dy /= n;
    }
  } else if (lm == LOGIC_MODE_MAX) {
    int32_t best_dx = 0, best_dy = 0;
    int32_t best_adx = 0, best_ady = 0;
    for (int i = 0; i < n; i++) {
      int32_t adx = (int32_t)g_mice[i].dx;
      int32_t ady = (int32_t)g_mice[i].dy;
      if (adx < 0) adx = -adx;
      if (ady < 0) ady = -ady;
      if (adx >= best_adx) { best_adx = adx; best_dx = (int32_t)g_mice[i].dx; }
      if (ady >= best_ady) { best_ady = ady; best_dy = (int32_t)g_mice[i].dy; }
    }
    dx = best_dx;
    dy = best_dy;
  } else if (lm >= LOGIC_MODE_2_MIN && lm <= LOGIC_MODE_2_XNOR) {
    dx = logic2_axis(lm, g_mice[0].dx, g_mice[1].dx);
    dy = logic2_axis(lm, g_mice[0].dy, g_mice[1].dy);
  } else {
    for (int i = 0; i < n; i++) {
      dx += g_mice[i].dx;
      dy += g_mice[i].dy;
    }
  }

  dx = (int32_t)((float)dx * s->amplify);
  dy = (int32_t)((float)dy * s->amplify);
  if (dx > 127) dx = 127;
  if (dx < -128) dx = -128;
  if (dy > 127) dy = 127;
  if (dy < -128) dy = -128;
  g_combined_dx = (int16_t)dx;
  g_combined_dy = (int16_t)dy;
  g_has_report = (dx != 0 || dy != 0 || g_combined_wheel != 0 || g_combined_buttons != 0);
}

#define UART_CONFIG_SYNC1  0x55
#define UART_CONFIG_SYNC2  0xCF
#define UART_CONFIG_CMD    0x01

static void uart_process_byte(uint8_t b) {
  /* Config packet: 0x55 0xCF 0x01 N L I A Q_lo Q_hi [save]. */
  if (uart_config_state == 1) {
    uart_config_state = (b == UART_CONFIG_SYNC2) ? 2 : 0;
    return;
  }
  if (uart_config_state == 2) {
    uart_config_state = (b == UART_CONFIG_CMD) ? 3 : 0;
    if (uart_config_state == 3) uart_config_len = 0;
    return;
  }
  if (uart_config_state == 3) {
    uart_config_buf[uart_config_len++] = b;
    if (uart_config_len >= UART_CONFIG_PAYLOAD_LEN) {
      uart_config_state = 0;
      settings_apply_uart(
        uart_config_buf[0],
        uart_config_buf[1],
        uart_config_buf[2],
        uart_config_buf[3],  /* output_mode */
        uart_config_buf[4],  /* amplify_x100 */
        (uint16_t)uart_config_buf[5] | ((uint16_t)uart_config_buf[6] << 8)
      );
      if (uart_config_buf[7] != 0)
        settings_save_to_flash();
    }
    return;
  }

  if (uart_len == 0) {
    if (b == UART_CONFIG_SYNC1) {
      uart_config_state = 1;
      return;
    }
    if (b == UART_SYNC) {
      uart_buf[0] = b;
      uart_len = 1;
    }
    return;
  }

  uart_buf[uart_len++] = b;
  if (uart_len >= UART_PACKET_LEN) {
    uart_len = 0;
    if (uart_buf[0] != UART_SYNC) return;

    int n = get_num_mice();
    uint8_t bt = uart_buf[1 + NUM_MICE_MAX * 2] & 0x07;
    int8_t wh = (int8_t)uart_buf[1 + NUM_MICE_MAX * 2 + 1];
    for (int i = 0; i < n; i++) {
      g_mice[i].dx      = (int8_t)uart_buf[1 + i * 2 + 0];
      g_mice[i].dy      = (int8_t)uart_buf[1 + i * 2 + 1];
      g_mice[i].buttons = bt;
      g_mice[i].wheel   = wh;
    }
    g_combined_buttons = bt;
    g_combined_wheel   = wh;
  }
}

static void uart_poll(void) {
  while (uart_is_readable(UART_ID)) {
    uint8_t c = (uint8_t)uart_getc(UART_ID);
    uart_process_byte(c);
  }
}

static void send_mouse_report(void) {
  const settings_t *s = settings_get();
  uint8_t out_mode = s->output_mode;

  if (out_mode == SETTINGS_OUTPUT_SEPARATE) {
    /* Six separate mice: send each g_mice[i] to HID instance i. */
    int n = get_num_mice();
    for (int i = 0; i < n; i++) {
      if (!tud_mounted() || !tud_hid_n_ready(i)) continue;
      if (g_mice[i].dx == 0 && g_mice[i].dy == 0 && g_mice[i].wheel == 0 && g_mice[i].buttons == 0)
        continue;
      tud_hid_n_mouse_report(i, REPORT_ID_MOUSE,
                            g_mice[i].buttons,
                            g_mice[i].dx, g_mice[i].dy,
                            g_mice[i].wheel, 0);
      g_mice[i].dx = g_mice[i].dy = g_mice[i].wheel = 0;
      g_mice[i].buttons = 0;
    }
    return;
  }

  /* Combined: single mouse on instance 0. */
  if (!tud_mounted() || !tud_hid_n_ready(0)) return;
  if (!g_has_report && g_combined_dx == 0 && g_combined_dy == 0 &&
      g_combined_wheel == 0) return;

  tud_hid_n_mouse_report(0, REPORT_ID_MOUSE,
                         g_combined_buttons,
                         (int8_t)g_combined_dx,
                         (int8_t)g_combined_dy,
                         (int8_t)g_combined_wheel,
                         0);

  g_combined_dx = g_combined_dy = 0;
  g_combined_wheel = 0;
  g_has_report = false;
  memset(g_mice, 0, sizeof(g_mice));
}

void tud_mount_cb(void) {}
void tud_umount_cb(void) {}
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }
void tud_resume_cb(void) {}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

int main(void) {
  stdio_init_all();
  board_init();
  tud_init(BOARD_TUD_RHPORT);
  settings_init();

  uint8_t input_mode = settings_get()->input_mode;
  if (input_mode == INPUT_MODE_UART || input_mode == INPUT_MODE_BOTH) {
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  }
  if (input_mode == INPUT_MODE_QUADRATURE || input_mode == INPUT_MODE_BOTH)
    quadrature_init();

  inputs_reset();

  uint32_t last_hid = 0;
  while (1) {
    tud_task();
    input_mode = settings_get()->input_mode;
    if (input_mode == INPUT_MODE_UART || input_mode == INPUT_MODE_BOTH)
      uart_poll();
    if (input_mode == INPUT_MODE_QUADRATURE || input_mode == INPUT_MODE_BOTH)
      quadrature_poll();
    if (settings_get()->output_mode == SETTINGS_OUTPUT_COMBINED)
      aggregate_and_amplify();

    if ((settings_get()->output_mode == SETTINGS_OUTPUT_SEPARATE) || g_has_report) {
      if (board_millis() - last_hid >= HID_POLL_MS) {
        send_mouse_report();
        last_hid = board_millis();
      }
    }
  }
}
