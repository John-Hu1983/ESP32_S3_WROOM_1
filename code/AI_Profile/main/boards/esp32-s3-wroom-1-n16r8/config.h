#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// clang-format off

#define AUDIO_INPUT_SAMPLE_RATE            16000
#define AUDIO_OUTPUT_SAMPLE_RATE           16000

/* Power control */
#define POWER_LOCK_IO_PORT                 (GPBA02B_PORT_B)
#define POWER_LOCK_IO_PIN                  (3)

/* Button */
#define BUTTON_UP_IO_PORT                  (GPBA02B_PORT_A)
#define BUTTON_UP_IO_PIN                   (0)
#define BUTTON_DOWN_IO_PORT                (GPBA02B_PORT_A)
#define BUTTON_DOWN_IO_PIN                 (1)

/* LCD monitor */
#define LCD_SPI_HOST                       (SPI2_HOST)
#define LCD_IO_RS                          (GPIO_NUM_14)
#define LCD_IO_CS                          (GPIO_NUM_21)
#define LCD_IO_MISO                        (GPIO_NUM_NC)
#define LCD_IO_MOSI                        (GPIO_NUM_12)
#define LCD_IO_CLK                         (GPIO_NUM_13)
#define LCD_DEFAULT_CLOCK_HZ               (40 * 1000 * 1000)
#define LCD_DEFAULT_WIDTH                  (320)
#define LCD_DEFAULT_HEIGHT                 (480)
#define LCD_DEFAULT_MADCTL                 (0x48)
#define LCD_DEFAULT_COLMOD                 (0x55)
#define LCD_DEFAULT_INVERT_COLOR           (1)
#define LCD_IO_RESET_PORT                  (GPBA02B_PORT_A)
#define LCD_IO_RESET_PIN                   (5)

/* Desktop UI font */
#define DESKTOP_TEXT_FONT                  font_noto_sans_basic_16_4
#define DESKTOP_SYMBOL_FONT                font_material_symbols_16_4

/* PDM */
#define PDM_CLK_IO                         (GPIO_NUM_41)
#define PDM_DATA_IO                        (GPIO_NUM_42)
#define PDM_ENABLE_PORT                    (GPBA02B_PORT_B)
#define PDM_ENABLE_PIN                     (0)

/* I2S */
#define I2S_NUM                            (I2S_NUM_0)
#define I2S_BCK_IO                         (GPIO_NUM_40)
#define I2S_WS_IO                          (GPIO_NUM_39)
#define I2S_DO_IO                          (GPIO_NUM_38)
#define I2S_DI_IO                          (GPIO_NUM_NC)
#define I2S_ENABLE_PORT                    (GPBA02B_PORT_B)
#define I2S_ENABLE_PIN                     (1)

/* GPBA02B peripheral */
#define GPBA02B_SPI_HOST                   (SPI3_HOST)
#define GPBA02B_IO_MISO                    (GPIO_NUM_47)
#define GPBA02B_IO_MOSI                    (GPIO_NUM_48)
#define GPBA02B_IO_CLK                     (GPIO_NUM_45)
#define GPBA02B_IO_CS                      (GPIO_NUM_46)
#define GPBA02B_DEFAULT_CLOCK_HZ           (8 * 1000 * 1000)
#define GPBA02B_DEVICE_ID                  (0)
#define GPBA02B_HOST_IRQ_GPIO              (GPIO_NUM_NC)

/* RFID MFRC522 */
#define MFRC522_SPI_HOST                   (SPI3_HOST)
#define MFRC522_IO_MISO                    (GPIO_NUM_47)
#define MFRC522_IO_MOSI                    (GPIO_NUM_48)
#define MFRC522_IO_CLK                     (GPIO_NUM_45)
#define MFRC522_IO_CS                      (GPIO_NUM_0)
#define MFRC522_DEFAULT_CLOCK_HZ           (8 * 1000 * 1000)
#define MFRC522_RESET_PORT                 (GPBA02B_PORT_A)
#define MFRC522_RESET_PIN                  (6)
#define MFRC522_IRQ_PORT                   (GPBA02B_PORT_B)  // Reserved, software can poll IRQ state.
#define MFRC522_IRQ_PIN                    (4)

/* PWM */
#define GPBA02B_PWM_CN0_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN0_PIN                (0)
#define GPBA02B_PWM_CN1_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN1_PIN                (1)
#define GPBA02B_PWM_CN2_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN2_PIN                (2)
#define GPBA02B_PWM_CN3_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN3_PIN                (3)
#define GPBA02B_PWM_CN4_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN4_PIN                (4)
#define GPBA02B_PWM_CN5_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN5_PIN                (5)
#define GPBA02B_PWM_CN6_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN6_PIN                (6)
#define GPBA02B_PWM_CN7_PORT               (GPBA02B_PORT_A)
#define GPBA02B_PWM_CN7_PIN                (7)
#define GPBA02B_PWM_CN8_PORT               (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN8_PIN                (0)
#define GPBA02B_PWM_CN9_PORT               (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN9_PIN                (1)
#define GPBA02B_PWM_CN10_PORT              (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN10_PIN               (2)
#define GPBA02B_PWM_CN11_PORT              (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN11_PIN               (3)
#define GPBA02B_PWM_CN12_PORT              (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN12_PIN               (4)
#define GPBA02B_PWM_CN13_PORT              (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN13_PIN               (5)
#define GPBA02B_PWM_CN14_PORT              (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN14_PIN               (6)
#define GPBA02B_PWM_CN15_PORT              (GPBA02B_PORT_C)
#define GPBA02B_PWM_CN15_PIN               (7)

/* Printer*/
#define PRINTER_UART_HOST                  (UART_NUM_1)
#define PRINTER_UART_BAUDRATE              (115200)
#define PRINTER_UART_TX_GPIO               (GPIO_NUM_17)
#define PRINTER_UART_RX_GPIO               (GPIO_NUM_16)
#define PRINTER_UART_DTR_GPIO              (GPIO_NUM_15)

// clang-format on

#endif  // _BOARD_CONFIG_H_
