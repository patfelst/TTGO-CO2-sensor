#include <Arduino.h>

#include "Adafruit_NeoPixel.h"
#include "SparkFun_SCD30_Arduino_Library.h"  //Click here to get the library: http://librarymanager/All#SparkFun_SCD30
#include "Wire.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "DSEG7Modern40.h"
#include "DSEG7ModernBold60.h"

// #define TFT_BACKGND TFT_BLACK
#define TFT_BACKGND TFT_BLACK
#define sw_version  "v0.2"
#define LED_COUNT   16
#define LED_PIN     27

// LGFX for TTGO T-Display
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

 public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();

      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 14000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 18;
      cfg.pin_mosi = 19;
      cfg.pin_miso = -1;
      cfg.pin_dc = 16;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _light_instance.config();

      cfg.pin_bl = 4;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {
      auto cfg = _panel_instance.config();

      cfg.pin_cs = 5;
      cfg.pin_rst = 23;
      cfg.pin_busy = -1;

      cfg.panel_width = 135;
      cfg.panel_height = 240;
      cfg.offset_x = 52;
      cfg.offset_y = 40;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 16;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

// Create LCD instance
LGFX lcd;

// Create CO2 sensor instance
SCD30 airSensor;

// Create neopixel instance
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

uint16_t hsv_col = 0;
uint16_t co2_level = 0;
float temperature = 0.0;
float humidity = 0.0;
uint32_t co2_led_colour = 0;
int32_t co2_lcd_colour = 0;
uint8_t num_leds_lit = 0;
uint8_t led_brightness_pc = 50;  // In percent
uint8_t lcd_brightness_pc = 50;  // In percent
char txt[50] = "";

#define RECT_WIDTH  165
#define RECT_LEFT_X ((lcd.width() - RECT_WIDTH) / 2)

// TODO Remove delay(500) replace with non-blocking tine schedule
// TODO Use button to swap to history bargraph. History of max value and ave values reached for last 12 hours (swtich between max and ave with button)
// TODO Remove title bar AND software version, show as splash screen on reboot
// TODO add MENU to:
// TODO   - Calibrate in 1 of 2 methods: 7-day Automatic Self-Calibration (ASC) + Set Forced Recalibration value (FRC)
// TODO   - Enter temperature offset
// TODO   - Enter altitude
// TODO   - show software version
// TODO   - show FRC calibration value
// TODO   - Set screen rotation left/right

/*
-----------------
  setup()
-----------------
*/
void setup(void) {
  strip.begin();  // Start the RGBW LED ring
  strip.setBrightness((led_brightness_pc * 255) / 100);

  Wire.begin();       // Start I2C for CO2 sensor. Using default I2C pins SDA = 21, SCL = 22
  airSensor.begin();  // Start Sensirion SCD-30 CO2 sensor

  Serial.begin(115200);
  // Serial.printf("\nCO2 measurment interval = %d seconds\n", airSensor.getMeasurementInterval());
  Serial.print("Auto calibration set to ");
  if (airSensor.getAutoSelfCalibration() == true)
    Serial.println("true");
  else
    Serial.println("false");

  uint16_t settingVal;
  airSensor.getForcedRecalibration(&settingVal);
  Serial.printf("Forced recalibration factor (ppm) is %d", settingVal);

  lcd.init();  // LCD resolution 240 x 135 pixels
  lcd.setBrightness((lcd_brightness_pc * 255) / 100);
  lcd.setRotation(1);

  lcd.setFont(&fonts::FreeSansBold24pt7b);
  lcd.setTextDatum(top_center);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("CO2", lcd.width() / 2, 20);
  lcd.setTextDatum(bottom_center);
  lcd.drawString("Monitor", lcd.width() / 2, lcd.height() - 20);

  // Display software version
  lcd.setTextDatum(top_left);
  lcd.setTextColor(TFT_DARKGRAY, TFT_BLACK);
  lcd.setFont(&fonts::Font2);
  lcd.drawString(sw_version, 4, 7);

  delay(2000);
  lcd.clear();

// Draw divider lines at bottom of LCD
#define DIV_LINE_Y 29
  lcd.drawRect(0, lcd.height() - DIV_LINE_Y - 32, lcd.width() - 50, 33, TFT_DARKGREY);
  lcd.drawRect(0, lcd.height() - DIV_LINE_Y, lcd.width(), DIV_LINE_Y, TFT_DARKGREY);
  lcd.drawFastVLine(lcd.width() / 2, lcd.height() - DIV_LINE_Y, DIV_LINE_Y, TFT_DARKGREY);
}

/*
-----------------
  loop()
-----------------
*/
void loop(void) {
  // Check if CO2 data is available
  if (airSensor.dataAvailable()) {
    // Read CO2 sensor
    co2_level = airSensor.getCO2();
    temperature = airSensor.getTemperature();
    humidity = airSensor.getHumidity();

    // Set the RGB LEDs based on CO2 level from https://www.kane.co.uk/knowledge-centre/what-are-safe-levels-of-co-and-co2-in-rooms
    // Assume indoor CO2 levels
    switch (co2_level) {
      case 0 ... 1000:
        // CO2 400 - 1000: OK good air exchange
        strcpy(txt, "Good air quality");
        num_leds_lit = 4;
        co2_led_colour = strip.Color(0, 255, 0);  // GREEN
        co2_lcd_colour = TFT_GREEN;
        led_brightness_pc = 10;  // LED right brightness %
        lcd_brightness_pc = 35;  // LCD brightness %
        break;

      case 1001 ... 2000:
        // CO2 1000 - 2000: Drowsy and poor air
        strcpy(txt, "Drowsy, poor air");
        num_leds_lit = 8;
        co2_led_colour = strip.Color(255, 128, 0);  // ORANGE
        co2_lcd_colour = TFT_YELLOW;
        led_brightness_pc = 30;
        lcd_brightness_pc = 60;
        break;

      case 2001 ... 5000:
        //    Headaches, sleepiness and stagnant, stale, stuffy air.
        //    Poor concentration, loss of attention, increased heart rate and slight nausea may be present.
        strcpy(txt, "Headache sleepy");
        num_leds_lit = 12;
        co2_led_colour = strip.Color(255, 64, 0);  // DARK ORANGE
        co2_lcd_colour = TFT_ORANGE;
        led_brightness_pc = 50;
        lcd_brightness_pc = 75;
        break;

      case 5001 ... 65535:
        //    Workplace 8-hr exposure limit
        strcpy(txt, "8hr exposure limit");
        num_leds_lit = 16;
        co2_led_colour = strip.Color(255, 0, 0);  // RED
        co2_lcd_colour = TFT_RED;
        led_brightness_pc = 70;
        lcd_brightness_pc = 100;
        break;

      default:
        // PINK indicates an error
        strcpy(txt, "CO2 val error");
        num_leds_lit = 1;
        co2_led_colour = strip.Color(255, 0, 255);
        co2_lcd_colour = TFT_PINK;
        led_brightness_pc = 80;
        lcd_brightness_pc = 20;
        break;
    }

    // Set the Neopixel LED colour based on CO2 value
    strip.clear();
    strip.setBrightness((led_brightness_pc * 255) / 100);
    strip.fill(co2_led_colour, LED_COUNT - num_leds_lit, num_leds_lit);
    strip.show();
    lcd.setBrightness((lcd_brightness_pc * 255) / 100);

    // Display CO2 effect on humans on LCD
    int32_t ppm_y = lcd.height() - 33;
    lcd.setFont(&fonts::FreeSans12pt7b);
    lcd.setTextDatum(bottom_left);
    lcd.setTextPadding(185);
    lcd.setTextColor(TFT_MAGENTA, TFT_BACKGND);
    lcd.drawString(txt, 4, ppm_y);

    // Display CO2 value on LCD
    lcd.setTextDatum(top_right);
    lcd.setFont(&DSEG7_Modern_Bold_60);
    lcd.setTextColor(co2_lcd_colour, TFT_BACKGND);
    sprintf(txt, "%d", co2_level);
    lcd.setTextPadding(lcd.width());
    lcd.drawString(txt, lcd.width(), 7);

    // Display "ppm" on LCD
    lcd.setFont(&fonts::FreeSans12pt7b);
    lcd.setTextDatum(bottom_right);
    lcd.setTextPadding(0);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString("ppm", lcd.width(), ppm_y);

    // Prepare to display temp and humidity
    lcd.setFont(&fonts::FreeSans12pt7b);
    lcd.setTextPadding(90);

    // Display temperature on LCD
    uint32_t txt_x = 20;
    uint32_t txt_y = lcd.height() - 1;
    lcd.setTextDatum(bottom_left);
    lcd.setTextColor(TFT_LIGHTGRAY, TFT_BACKGND);
    sprintf(txt, "%2.1f   C", temperature);
    lcd.drawString(txt, txt_x, txt_y);
    lcd.drawCircle(txt_x + 55, txt_y - 20, 4, TFT_LIGHTGRAY);  // Degree symbol

    // Display humidity on LCD
    txt_x = lcd.width() - 15;
    lcd.setTextDatum(bottom_right);
    lcd.setTextColor(TFT_LIGHTGRAY, TFT_BACKGND);
    sprintf(txt, "%3.0f%% RH", humidity);
    lcd.drawString(txt, txt_x, txt_y);
  }
  delay(500);
}