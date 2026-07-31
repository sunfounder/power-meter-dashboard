#pragma once

/* ============================================================
 * 4-Channel Power Meter â€?Pin Configuration
 * Board: Lilygo T-Display-S3 (ESP32-S3) + custom baseboard
 * Updated: 2024-07-27 â€?verified with multimeter
 * ============================================================ */

/* ---- Display (ST7789 via I80 8-bit parallel, ON MODULE) ----
 * DO NOT CHANGE. Internal module connections. */
#define PIN_LCD_BL      38
#define PIN_LCD_D0      39
#define PIN_LCD_D1      40
#define PIN_LCD_D2      41
#define PIN_LCD_D3      42
#define PIN_LCD_D4      45
#define PIN_LCD_D5      46
#define PIN_LCD_D6      47
#define PIN_LCD_D7      48
#define PIN_POWER_ON    15
#define PIN_LCD_RES     5
#define PIN_LCD_CS      6
#define PIN_LCD_DC      7
#define PIN_LCD_WR      8
#define PIN_LCD_RD      9

/* ---- Physical buttons (on module) ---- */
#define PIN_BUTTON_1    0     // BOOT button
#define PIN_BUTTON_2    14    // User button

/* ---- I2C Bus ---- */
#define PIN_I2C_SDA     43
#define PIN_I2C_SCL     44

/* ---- I2C Device Addresses ---- */
#define ADS1115_ADDR    0x48
#define INA226_ADDR_CH1 0x40
#define INA226_ADDR_CH2 0x41
#define INA226_ADDR_CH3 0x44
#define INA226_ADDR_CH4 0x45

/* ---- Buzzer ---- */
#define PIN_BUZZER      2     // GPIO2

/* ---- Ambient NTC (B3950 10K) ---- */
#define PIN_AMB_NTC     1     // GPIO1

/* ---- Channel Enable Controls (V1: EN0 only, EN1 hardwired ON) ----
 *
 * EN0: Master switch â€?follows record start/stop
 * EN1: Primary output (hardware default ON, firmware not used)
 * EN2: Not populated (V2 will add)
 *
 * Verified 2024-07-27:
 *   CHA: EN0=GPIO17, EN1=GPIO18
 *   CHB: EN0=GPIO21, EN1=GPIO16
 *   CHC: EN0=GPIO3,  EN1=GPIO10
 *   CHD: EN0=GPIO12, EN1=GPIO13
 * -------------------------------------------------------- */

#define PIN_CHA_EN      17
#define PIN_CHB_EN      21
#define PIN_CHC_EN      3
#define PIN_CHD_EN      12

/* ---- INA226 Shunt Resistor (milliohms) ---- */
#define INA226_SHUNT_MOHM  5.0   // 5m¦¸ (O3602V10 BOM: R7=5mR 2512)

/* ---- NTC Parameters (B3950 10K) ---- */
#define NTC_SERIES_RESISTOR    10000
#define NTC_NOMINAL_RESISTANCE 10000
#define NTC_NOMINAL_TEMP       25
#define NTC_B_VALUE            3950

/* Ambient NTC calibration offset (Â°C) */
#define AMB_NTC_CALIB_OFFSET   -5.5f
