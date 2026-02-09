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

#define NUM_MICE        6
#define UART_ID         uart0
#define UART_BAUD       115200
#define UART_TX_PIN     0
#define UART_RX_PIN     1

#define AMPLIFY         1.0f   /* scale factor for combined movement (e.g. 1.5 = 50% more) */
#define HID_POLL_MS     2      /* send HID report every 2 ms when there is movement */

/* Input source: UART only, 6 quadrature encoders on GPIO, or both */
#define INPUT_MODE_UART         0
#define INPUT_MODE_QUADRATURE   1
#define INPUT_MODE_BOTH         2
#define INPUT_MODE              INPUT_MODE_UART  /* set to INPUT_MODE_QUADRATURE for 6 ball encoders on GPIO */

/* Quadrature: 6 mice × 4 pins (X_A, X_B, Y_A, Y_B). Pico GPIO numbers. */
#if INPUT_MODE == INPUT_MODE_QUADRATURE || INPUT_MODE == INPUT_MODE_BOTH
static const uint8_t QUAD_PINS[NUM_MICE][4] = {
  { 2,  3,  4,  5 },  /* Mouse 0: X_A, X_B, Y_A, Y_B */
  { 6,  7,  8,  9 },  /* Mouse 1 */
  { 10, 11, 12, 13 }, /* Mouse 2 */
  { 14, 15, 16, 17 }, /* Mouse 3 */
  { 18, 19, 20, 21 }, /* Mouse 4 */
  { 22, 23, 24, 25 }, /* Mouse 5 */
};
#define QUAD_SCALE 2   /* quadrature counts per HID delta (tune for feel) */
#endif

/* Per-mouse state (from UART or host) */
typedef struct {
  int8_t dx;
  int8_t dy;
  uint8_t buttons;
  int8_t wheel;
} mouse_input_t;

static mouse_input_t g_mice[NUM_MICE];
static int16_t g_combined_dx, g_combined_dy;
static uint8_t g_combined_buttons;
static int16_t g_combined_wheel;
static bool g_has_report;

/* UART protocol: sync 0xAA then 6 × (dx, dy) then 1 byte buttons, 1 byte wheel (signed).
 * Total 1 + 12 + 1 + 1 = 15 bytes. */
#define UART_SYNC       0xAA
#define UART_PACKET_LEN (1 + NUM_MICE * 2 + 1 + 1)

static uint8_t uart_buf[UART_PACKET_LEN];
static int uart_len;

#if INPUT_MODE == INPUT_MODE_QUADRATURE || INPUT_MODE == INPUT_MODE_BOTH
/* Quadrature decode: prev_ab and curr_ab are 2-bit (A=bit0, B=bit1). Returns -1, 0, or +1. */
static const int8_t quad_table[16] = {
  0, 1, -1, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, -1, 1, 0
};
static uint8_t quad_prev[NUM_MICE][2];  /* [mouse][0]=X_ab, [1]=Y_ab */
static int16_t quad_acc[NUM_MICE][2];  /* accumulated counts [mouse][dx,dy] */

static void quadrature_init(void) {
  for (int i = 0; i < NUM_MICE; i++) {
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
  for (int i = 0; i < NUM_MICE; i++) {
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
  for (int i = 0; i < NUM_MICE; i++) {
    int16_t ax = quad_acc[i][0], ay = quad_acc[i][1];
    int8_t dx = 0, dy = 0;
    if (ax >= QUAD_SCALE) { dx = (int8_t)(ax / QUAD_SCALE); quad_acc[i][0] = (int16_t)(ax % QUAD_SCALE); }
    else if (ax <= -QUAD_SCALE) { dx = (int8_t)(ax / QUAD_SCALE); quad_acc[i][0] = (int16_t)(ax % QUAD_SCALE); }
    if (ay >= QUAD_SCALE) { dy = (int8_t)(ay / QUAD_SCALE); quad_acc[i][1] = (int16_t)(ay % QUAD_SCALE); }
    else if (ay <= -QUAD_SCALE) { dy = (int8_t)(ay / QUAD_SCALE); quad_acc[i][1] = (int16_t)(ay % QUAD_SCALE); }
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
#endif

static void inputs_reset(void) {
  memset(g_mice, 0, sizeof(g_mice));
  g_combined_dx = g_combined_dy = 0;
  g_combined_buttons = 0;
  g_combined_wheel = 0;
  g_has_report = false;
}

static void aggregate_and_amplify(void) {
  int32_t dx = 0, dy = 0;
  for (int i = 0; i < NUM_MICE; i++) {
    dx += g_mice[i].dx;
    dy += g_mice[i].dy;
  }
  dx = (int32_t)((float)dx * AMPLIFY);
  dy = (int32_t)((float)dy * AMPLIFY);
  if (dx > 127) dx = 127;
  if (dx < -128) dx = -128;
  if (dy > 127) dy = 127;
  if (dy < -128) dy = -128;
  g_combined_dx = (int16_t)dx;
  g_combined_dy = (int16_t)dy;
  g_has_report = (dx != 0 || dy != 0 || g_combined_wheel != 0 || g_combined_buttons != 0);
}

static void uart_process_byte(uint8_t b) {
  if (uart_len == 0) {
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

    for (int i = 0; i < NUM_MICE; i++) {
      g_mice[i].dx  = (int8_t)uart_buf[1 + i * 2 + 0];
      g_mice[i].dy  = (int8_t)uart_buf[1 + i * 2 + 1];
    }
    g_combined_buttons = uart_buf[1 + NUM_MICE * 2] & 0x07;
    g_combined_wheel   = (int8_t)uart_buf[1 + NUM_MICE * 2 + 1];
    /* aggregate_and_amplify() called in main loop */
  }
}

static void uart_poll(void) {
  while (uart_is_readable(UART_ID)) {
    uint8_t c = (uint8_t)uart_getc(UART_ID);
    uart_process_byte(c);
  }
}

static void send_mouse_report(void) {
  if (!tud_mounted() || !tud_hid_ready()) return;
  if (!g_has_report && g_combined_dx == 0 && g_combined_dy == 0 &&
      g_combined_wheel == 0) return;

  tud_hid_mouse_report(REPORT_ID_MOUSE,
                       g_combined_buttons,
                       (int8_t)g_combined_dx,
                       (int8_t)g_combined_dy,
                       (int8_t)g_combined_wheel,
                       0);

  g_combined_dx = g_combined_dy = 0;
  g_combined_wheel = 0;
  g_has_report = false;
  memset(g_mice, 0, sizeof(g_mice)); /* consumed */
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

#if INPUT_MODE == INPUT_MODE_UART || INPUT_MODE == INPUT_MODE_BOTH
  uart_init(UART_ID, UART_BAUD);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
#endif
#if INPUT_MODE == INPUT_MODE_QUADRATURE || INPUT_MODE == INPUT_MODE_BOTH
  quadrature_init();
#endif

  inputs_reset();

  uint32_t last_hid = 0;
  while (1) {
    tud_task();
#if INPUT_MODE == INPUT_MODE_UART || INPUT_MODE == INPUT_MODE_BOTH
    uart_poll();
#endif
#if INPUT_MODE == INPUT_MODE_QUADRATURE || INPUT_MODE == INPUT_MODE_BOTH
    quadrature_poll();
#endif
    aggregate_and_amplify();

    if (g_has_report && (board_millis() - last_hid >= HID_POLL_MS)) {
      send_mouse_report();
      last_hid = board_millis();
    }
  }
}
