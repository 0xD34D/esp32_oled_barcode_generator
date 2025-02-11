#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_check.h"
#include "esp_console.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

/*
 * We warn if a secondary serial console is enabled. A secondary serial console
 * is always output-only and hence not very useful for interactive console
 * applications. If you encounter this warning, consider disabling the secondary
 * serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning \
    "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif
#define PROMPT_STR CONFIG_IDF_TARGET

// display I2C pins
#define I2C_SDA GPIO_NUM_1
#define I2C_SCL GPIO_NUM_2
// display dimensions
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 32

typedef enum { EAN_8 = 0, EAN_13, UPC_A } barcode_type_e;

static const char *TAG = "oled_barcode";
static esp_console_repl_t *repl = NULL;
const uint8_t bar_code_digits[] = {0b0001101, 0b0011001, 0b0010011, 0b0111101,
                                   0b0100011, 0b0110001, 0b0101111, 0b0111011,
                                   0b0110111, 0b0001011};

// for EAN-13, indicates if a left digit should be encoded with even (0) or odd
// (1) parity
const uint8_t ean_13_left_parity[] = {0b000000, 0b001011, 0b001101, 0b001110,
                                      0b010011, 0b011001, 0b011100, 0b010101,
                                      0b010110, 0b011010};

u8g2_t u8g2;
u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;

static int console_display_barcode(int argc, char **argv);
static int console_dump_display(int argc, char **argv);
const esp_console_cmd_t cmds[] = {
    {
        .command = "barcode",
        .help = "displays the given EAN-8, EAN-13, or UPC-A barcode on the "
                "OLED display",
        .hint = "<code>",
        .func = &console_display_barcode,
    },
    {
        .command = "dump",
        .help = "Dump the display buffer to serial",
        .hint = NULL,
        .func = &console_dump_display,
    },
};

void setup_u8g2() {
  u8g2_esp32_hal.sda = I2C_SDA;
  u8g2_esp32_hal.scl = I2C_SCL;
  u8g2_esp32_hal_init(u8g2_esp32_hal);

  u8g2_Setup_ssd1306_i2c_128x32_univision_f(
      &u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
  u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

  u8g2_InitDisplay(&u8g2);
  u8g2_SetPowerSave(&u8g2, 0);
  u8g2_SetDrawColor(&u8g2, 0);
}

// swaps the 7 least significant bits of a byte
uint8_t swap7bits(uint8_t a) {
  uint8_t v = 0;

  if (a & 0x40) v |= 0x01;
  if (a & 0x20) v |= 0x02;
  if (a & 0x10) v |= 0x04;
  if (a & 0x08) v |= 0x08;
  if (a & 0x04) v |= 0x10;
  if (a & 0x02) v |= 0x20;
  if (a & 0x01) v |= 0x40;

  return v;
}

/**
 * Draw a barcode on the display
 * Function derived from information found at
 * https://en.wikipedia.org/wiki/Universal_Product_Code
 *
 * @param str The string to encode in the barcode
 * The string must contain only digits, and be 12 characters long
 */
esp_err_t draw_bar_code(u8g2_t *u8g2, const char *str) {
  int N = strlen(str);
  int mid;
  barcode_type_e type;

  if (N != 8 && N != 12 && N != 13) {
    return ESP_ERR_INVALID_SIZE;
  }

  for (int i = 0; i < N; i++) {
    if (str[i] < '0' || str[i] > '9') {
      return ESP_ERR_INVALID_ARG;
    }
  }

  if (N == 8) {
    type = EAN_8;
    mid = 4;
  } else if (N == 12) {
    type = UPC_A;
    mid = 6;
  } else {
    type = EAN_13;
    mid = 7;
  }

  // calculate the start position of the barcode on the display
  int pos = (DISPLAY_WIDTH - ((N * 7) + 11)) / 2;

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawBox(u8g2, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_SetFont(u8g2, u8g2_font_squeezed_b6_tr);
  if (type != EAN_8) {
    u8g2_DrawGlyph(u8g2, pos - 6, DISPLAY_HEIGHT - 1, str[0]);
  }

  // draw start guard (bar-space-bar)
  u8g2_DrawVLine(u8g2, pos++, 0, DISPLAY_HEIGHT);
  pos++;
  u8g2_DrawVLine(u8g2, pos++, 0, DISPLAY_HEIGHT);

  // draw digits
  uint8_t glyph_height = u8g2_GetMaxCharHeight(u8g2);
  uint8_t first_digit = str[0] - '0';
  uint8_t start_idx = (type == EAN_13) ? 1 : 0;
  for (uint8_t i = start_idx; i < N; i++) {
    uint8_t digit = str[i] - '0';
    uint8_t b = bar_code_digits[digit];
    // second half of the barcode is inverted
    if (i >= mid) {
      b = ~b;
    } else if (type == EAN_13) {
      bool is_g_digit = ean_13_left_parity[first_digit] & (1 << (5 - (i - 1)));
      if (is_g_digit) {
        b = swap7bits(~b);
      }
    }

    // draw center guard (space-bar-space-bar-space)
    if (i == mid) {
      pos++;
      u8g2_DrawVLine(u8g2, pos++, 0, DISPLAY_HEIGHT);
      pos++;
      u8g2_DrawVLine(u8g2, pos++, 0, DISPLAY_HEIGHT);
      pos++;
    }

    // draw text representation of the digit (except first and last)
    if (type != UPC_A || (i != 0 && i != N - 1)) {
      u8g2_DrawGlyph(u8g2, pos + 1, DISPLAY_HEIGHT - 1, str[i]);
    }

    //  first and last digits extend further
    int length = (DISPLAY_HEIGHT - 8);
    if (type == UPC_A && (i == 0 || i == N - 1)) {
      length = DISPLAY_HEIGHT;
    }
    for (int k = 0; k < 7; k++) {
      if (b & (0x01 << (6 - k))) {
        u8g2_DrawVLine(u8g2, pos, 0, length);
      }
      pos++;
    }
  }
  // draw end guard (bar-space-bar)
  u8g2_DrawVLine(u8g2, pos++, 0, DISPLAY_HEIGHT);
  pos++;
  u8g2_DrawVLine(u8g2, pos++, 0, DISPLAY_HEIGHT);
  if (type == UPC_A) {
    u8g2_DrawGlyph(u8g2, ++pos, DISPLAY_HEIGHT - 1, str[N - 1]);
  }
  u8g2_SendBuffer(u8g2);

  return ESP_OK;
}

static int console_display_barcode(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <ean>\n", argv[0]);
    return 1;
  }

  draw_bar_code(&u8g2, argv[1]);
  return 0;
}

void dump_display(const char *s) { printf("%s", s); }

static int console_dump_display(int argc, char **argv) {
  u8g2_WriteBufferPBM(&u8g2, dump_display);
  return 0;
}

void app_main(void) {
  setup_u8g2();
  draw_bar_code(&u8g2, "012345678912");
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  /* Prompt to be printed before each line.
   * This can be customized, made dynamic, etc.
   */
  repl_config.prompt = PROMPT_STR ">";
  repl_config.max_cmdline_length = 64;

  // Init console based on menuconfig settings
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || \
    defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
  esp_console_dev_uart_config_t hw_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

  // USJ console can be set only on esp32p4, having separate USB PHYs for
  // USB_OTG and USJ
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
  esp_console_dev_usb_serial_jtag_config_t hw_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(
      esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

  for (int count = 0; count < sizeof(cmds) / sizeof(esp_console_cmd_t);
       count++) {
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[count]));
  }

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}