/*
 * Firmware for the WICCON social battery SAO
 * Copyright 2024 Renze Nicolai
 * License: MIT
 *
 */

#include "ch32v003fun.h"
#include "i2c_slave.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "color_utilities.h"
#include "ch32v003_touch.h"

// Firmware version
#define FW_VERSION 1

// I2C peripheral configuration
#define I2C_ADDR_EEPROM  0x50
#define I2C_ADDR_CONTROL 0x57

// Pins
#define PIN_SDA     PC1
#define PIN_SCL     PC2
#define PIN_TOUCH_1 PD6
#define PIN_TOUCH_2 PA1
#define PIN_TOUCH_3 PA2
#define PIN_TOUCH_4 PD5
#define PIN_TOUCH_5 PD4
#define PIN_MODE    PC0
#define PIN_BUTTON  PC7
#define PIN_LED     PC6
#define PIN_IO1     PC5
#define PIN_IO2     PC3
#define PIN_SWIO    PD1
#define PIN_E1      PD2
#define PIN_E2      PD3



// I2C registers
#define I2C_REG_FW_VERSION_0      0  // LSB
#define I2C_REG_FW_VERSION_1      1  // MSB
#define I2C_REG_GPIO_MODE         2
#define I2C_REG_GPIO_INPUTS       3
#define I2C_REG_GPIO_OUTPUTS      4
#define I2C_REG_MODE              5
#define I2C_REG_TOUCH0_0          6 // LSB
#define I2C_REG_TOUCH0_1          7 // MSB
#define I2C_REG_TOUCH1_0          8 // LSB
#define I2C_REG_TOUCH1_1          9 // MSB
#define I2C_REG_TOUCH2_0          10 // LSB
#define I2C_REG_TOUCH2_1          11 // MSB
#define I2C_REG_TOUCH3_0          12 // LSB
#define I2C_REG_TOUCH3_1          13 // MSB
#define I2C_REG_TOUCH4_0          14 // LSB
#define I2C_REG_TOUCH4_1          15 // MSB
#define I2C_REG_SOCIAL_LEVEL      16
#define I2C_REG_RAINBOW_SPEED     17
#define I2C_REG_KNIGHTRIDER_SPEED 18
#define I2C_REG_BUTTON            19
#define I2C_REG_BUTTON_ENABLED    20
#define I2C_REG_ADDR_LED0_GREEN   21
#define I2C_REG_ADDR_LED0_RED     22
#define I2C_REG_ADDR_LED0_BLUE    23
#define I2C_REG_ADDR_LED1_GREEN   24
#define I2C_REG_ADDR_LED1_RED     25
#define I2C_REG_ADDR_LED1_BLUE    26
#define I2C_REG_ADDR_LED2_GREEN   27
#define I2C_REG_ADDR_LED2_RED     28
#define I2C_REG_ADDR_LED2_BLUE    29
#define I2C_REG_ADDR_LED3_GREEN   30
#define I2C_REG_ADDR_LED3_RED     31
#define I2C_REG_ADDR_LED3_BLUE    32
#define I2C_REG_ADDR_LED4_GREEN   33
#define I2C_REG_ADDR_LED4_RED     34
#define I2C_REG_ADDR_LED4_BLUE    35

// Variables
volatile uint8_t i2c_registers[35] = {0};
volatile uint8_t led_effect_data[15] = {0};
const uint8_t eeprom_registers[] = {'L','I','F','E',21,6,8,0,'W','I','C','C','O','N',' ','S','O','C','I','A','L',' ','B','A','T','T','E','R','Y','W','I','C','C','O','N',0x07,0x28,0,0,0,0,0,0};

uint32_t poll_interval_inputs = 20 * DELAY_MS_TIME;
uint32_t input_poll_previous = 0;

uint8_t social_level = 0; //0-4
uint8_t system_mode = 0;
bool button_enabled = false;
uint8_t rainbow_speed = 15;
uint8_t knightrider_speed = 0xFF - 10;
uint8_t knightrider_led = 0;
uint16_t knightrider_value = 0;
bool knightrider_direction = false;

// Hardware control functions
bool get_mode() {
    return !funDigitalRead(PIN_MODE);
}

void read_touch(uint32_t* value) {
    int iterations = 10;
    value[0] = ReadTouchPin(GPIOD, 6, 6, iterations); // 1
    value[1] = ReadTouchPin(GPIOA, 1, 1, iterations); // 2
    value[2] = ReadTouchPin(GPIOA, 2, 0, iterations); // 3
    value[3] = ReadTouchPin(GPIOD, 5, 5, iterations); // 4
    value[4] = ReadTouchPin(GPIOD, 4, 7, iterations); // 5
}

void knightrider_step(uint8_t color) {
    for (uint8_t i = 0; i < 15; i++) {
        if (led_effect_data[i] > 10) {
            led_effect_data[i]-= 10;
        } else {
            led_effect_data[i] = 0;
        }
    }

    if (knightrider_value > (0xFF - knightrider_speed)) {
        knightrider_value = 0;
        if (!knightrider_direction) {
            knightrider_led++;
            if (knightrider_led >= 4) {
                knightrider_direction = true;
            }
        } else {
            knightrider_led--;
            if (knightrider_led == 0) {
                knightrider_direction = false;
            }
        }
    } else {
        knightrider_value++;
    }

    if (led_effect_data[(knightrider_led * 3) + color] < 215) {
        led_effect_data[(knightrider_led * 3) + color] += 50;
    } else {
        led_effect_data[(knightrider_led * 3) + color] = 255;
    }
}

// Addressable LEDs
void write_addressable_leds(uint8_t* data, uint8_t length) __attribute__((optimize("O0")));
void write_addressable_leds(uint8_t* data, uint8_t length) {
    I2C1->CTLR2 &= ~(I2C_CTLR2_ITEVTEN); // Disable I2C event interrupt
    for (uint8_t pos_byte = 0; pos_byte < length; pos_byte++) {
        for (int i = 7; i >= 0; i--) {
            if ((data[pos_byte] >> i) & 1) {
                // Send 1
                __asm__("nop");__asm__("nop");
                GPIOC->BSHR |= 1 << (6);
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");
                GPIOC->BSHR |= 1 << (6 + 16);
            } else {
                // Send 0
                GPIOC->BSHR |= 1 << (6);
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");
                GPIOC->BSHR |= 1 << (6 + 16);
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
    }
    I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN; // Enable I2C event interrupt
}

// Functions: I2C

void onRead(uint8_t reg) {
    // Empty
}

void onWrite(uint8_t reg, uint8_t length) {
    // GPIO mode
    funPinMode(PIN_IO1, i2c_registers[I2C_REG_GPIO_MODE] & (1 << 0) ? GPIO_CFGLR_OUT_10Mhz_PP : GPIO_CFGLR_IN_PUPD);
    funPinMode(PIN_IO2, i2c_registers[I2C_REG_GPIO_MODE] & (1 << 1) ? GPIO_CFGLR_OUT_10Mhz_PP : GPIO_CFGLR_IN_PUPD);
    funPinMode(PIN_E1, i2c_registers[I2C_REG_GPIO_MODE] & (1 << 2) ? GPIO_CFGLR_OUT_10Mhz_PP : GPIO_CFGLR_IN_PUPD);
    funPinMode(PIN_E2, i2c_registers[I2C_REG_GPIO_MODE] & (1 << 3) ? GPIO_CFGLR_OUT_10Mhz_PP : GPIO_CFGLR_IN_PUPD);

    // GPIO output
    funDigitalWrite(PIN_IO1, i2c_registers[I2C_REG_GPIO_OUTPUTS] & (1 << 0));
    funDigitalWrite(PIN_IO2, i2c_registers[I2C_REG_GPIO_OUTPUTS] & (1 << 1));
    funDigitalWrite(PIN_E1, i2c_registers[I2C_REG_GPIO_OUTPUTS] & (1 << 2));
    funDigitalWrite(PIN_E2, i2c_registers[I2C_REG_GPIO_OUTPUTS] & (1 << 3));

    // Control registers
    system_mode = i2c_registers[I2C_REG_MODE];
    social_level = i2c_registers[I2C_REG_SOCIAL_LEVEL];
    rainbow_speed = i2c_registers[I2C_REG_RAINBOW_SPEED];
    knightrider_speed = i2c_registers[I2C_REG_KNIGHTRIDER_SPEED];
    button_enabled = i2c_registers[I2C_REG_BUTTON_ENABLED];

}

uint8_t read_other_inputs() {
    uint8_t value = 0;
    value |= funDigitalRead(PIN_IO1) << 0;
    value |= funDigitalRead(PIN_IO2) << 1;
    value |= funDigitalRead(PIN_E1) << 2;
    value |= funDigitalRead(PIN_E2) << 3;
    return value;
}

int main() {
    SystemInit();
    funGpioInitAll();

    // Enable ADC
	RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
	InitTouchADC();

    // Mode jumper
    funPinMode(PIN_MODE, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_MODE, true); // Pull-up

    // SAO IO1
    funPinMode(PIN_IO1, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_IO1, true); // Pull-up

    // SAO IO2
    funPinMode(PIN_IO2, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_IO2, true); // Pull-up

    // Button
    funPinMode(PIN_BUTTON, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_BUTTON, true); // Pull-up

    // Testpoint 1
    funPinMode(PIN_E1, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_E1, true); // Pull-up

    // Testpoint 2
    funPinMode(PIN_E2, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_E2, true); // Pull-up

    // LEDs
    funPinMode(PIN_LED, GPIO_CFGLR_OUT_10Mhz_PP);

    // Check if I2C bus is usable
    // This is done by enabling the internal pull-down resistors and checking the state of both SCL and SDA.
    // If either is held high by the bus pull-up resistors then the bus is considered usable.
    funPinMode(PIN_SDA, GPIO_CFGLR_IN_PUPD);
    funPinMode(PIN_SCL, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(PIN_SDA, false); // Pull-down
    funDigitalWrite(PIN_SCL, false); // Pull-down

    if (funDigitalRead(PIN_SDA) || funDigitalRead(PIN_SCL)) {
        // Initialize GPIO for I2C
        funPinMode(PIN_SDA, GPIO_CFGLR_OUT_10Mhz_AF_OD);
        funPinMode(PIN_SCL, GPIO_CFGLR_OUT_10Mhz_AF_OD);

        // Initialize I2C in peripheral mode
        SetupI2CSlave(I2C_ADDR_CONTROL, i2c_registers, sizeof(i2c_registers), onWrite, onRead, false);
        SetupSecondaryI2CSlave(I2C_ADDR_EEPROM, (uint8_t*) eeprom_registers, sizeof(eeprom_registers), NULL, NULL, true);
    } else {
        for (uint8_t i = 0; i < 5; i++) {
            led_effect_data[i * 3 + 0] = 0x00;
            led_effect_data[i * 3 + 1] = 0xFF;
            led_effect_data[i * 3 + 2] = 0x00;
        }
        write_addressable_leds((uint8_t*) led_effect_data, 15);
        Delay_Ms(100);
    }

    uint8_t hue = 0;

    uint32_t baseline[5] = {0};
    read_touch(baseline);

    rainbow_speed = 15; // Default speed of the rainbow

    if (!get_mode()) {
        system_mode = 1;
        button_enabled = true;
    }

    bool prev_button = false;

    while (1) {
        uint32_t now = SysTick->CNT;
        if (now - input_poll_previous >= poll_interval_inputs) {
            input_poll_previous = now;

            // Read touch inputs
            uint32_t raw_touch_value[5] = {0};
            read_touch(raw_touch_value);

            int32_t touch_value[5] = {0};
            for (uint8_t i = 0; i < 5; i++) {
                touch_value[i] = raw_touch_value[i] - baseline[i];
                if (touch_value[i] > 1900) {
                    social_level = i;
                }
            }

            // Read button
            bool button = !funDigitalRead(PIN_BUTTON);
            if (button && !prev_button && button_enabled) {
                system_mode++;
                if (system_mode > 7) system_mode = 1;
            }
            prev_button = button;

            // Update I2C registers
            I2C1->CTLR2 &= ~(I2C_CTLR2_ITEVTEN); // Disable I2C event interrupt
            i2c_registers[I2C_REG_FW_VERSION_0] = (FW_VERSION     ) & 0xFF;
            i2c_registers[I2C_REG_FW_VERSION_1] = (FW_VERSION >> 8) & 0xFF;
            i2c_registers[I2C_REG_GPIO_INPUTS] = read_other_inputs();
            i2c_registers[I2C_REG_SOCIAL_LEVEL] = social_level;
            i2c_registers[I2C_REG_RAINBOW_SPEED] = rainbow_speed;
            i2c_registers[I2C_REG_KNIGHTRIDER_SPEED] = knightrider_speed;
            i2c_registers[I2C_REG_BUTTON] = (button & 1) | ((prev_button & 1) << 1);
            i2c_registers[I2C_REG_BUTTON_ENABLED] = button_enabled;
            for (uint8_t i = 0; i < 5; i++) {
                uint16_t* touch_i2c_reg = (uint16_t*)&i2c_registers[I2C_REG_TOUCH0_0 + i * 2];
                *touch_i2c_reg = touch_value[i];
            }
            I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN; // Enable I2C event interrupt


            switch (system_mode) {
                case 0:
                    // I2C controls LEDs
                    write_addressable_leds((uint8_t*) &i2c_registers[I2C_REG_ADDR_LED0_GREEN], 15);
                    break;
                case 1: {
                    // Social battery
                    for (uint8_t i = 0; i < 5; i++) {
                        if (social_level < i) {
                            led_effect_data[(i * 3) + 0] = 0;
                            led_effect_data[(i * 3) + 1] = 0;
                        } else {
                            led_effect_data[(i * 3) + 0] = 50 * social_level;
                            led_effect_data[(i * 3) + 1] = 0xFF - 50 * social_level;
                        }
                        led_effect_data[(i * 3) + 2] = touch_value[i] > 2000 ? 0xFF : 0x00;
                    }
                    break;
                }
                case 2: {
                    // Rainbow
                    for (uint8_t led = 0; led < 5; led++) {
                        uint32_t color = EHSVtoHEX(hue + (led*rainbow_speed), 240, 128);
                        led_effect_data[(led * 3) + 0] = (color >>  8) & 0xFF;
                        led_effect_data[(led * 3) + 1] = (color >> 16) & 0xFF;
                        led_effect_data[(led * 3) + 2] = (color >>  0) & 0xFF;
                    }
                    hue++;
                    break;
                }
                case 3: {
                    // Transgender colors
                    led_effect_data[0] = 0; // G
                    led_effect_data[1] = 0; // R
                    led_effect_data[2] = 255; // B
                    led_effect_data[3] = 150; // G
                    led_effect_data[4] = 255; // R
                    led_effect_data[5] = 174; // B
                    led_effect_data[6] = 255;
                    led_effect_data[7] = 255;
                    led_effect_data[8] = 255;
                    led_effect_data[9] = 150; // G
                    led_effect_data[10] = 255; // R
                    led_effect_data[11] = 174; // B
                    led_effect_data[12] = 0; // G
                    led_effect_data[13] = 0; // R
                    led_effect_data[14] = 255; // B
                    break;
                }
                case 4: {
                    // Dutch flag colors
                    led_effect_data[0] = 0; // G
                    led_effect_data[1] = 255; // R
                    led_effect_data[2] = 0; // B
                    led_effect_data[3] = 0; // G
                    led_effect_data[4] = 255; // R
                    led_effect_data[5] = 0; // B
                    led_effect_data[6] = 255;
                    led_effect_data[7] = 255;
                    led_effect_data[8] = 255;
                    led_effect_data[9] = 0; // G
                    led_effect_data[10] = 0; // R
                    led_effect_data[11] = 255; // B
                    led_effect_data[12] = 0; // G
                    led_effect_data[13] = 0; // R
                    led_effect_data[14] = 255; // B
                    break;
                }
                case 5: {
                    // Knightrider (red)
                    knightrider_step(1);
                    break;
                }
                case 6: {
                    // Knightrider (green)
                    knightrider_step(0);
                    break;
                }
                case 7: {
                    // Knightrider (blue)
                    knightrider_step(2);
                    break;
                }
            }

            if (system_mode > 0) {
                write_addressable_leds((uint8_t*) led_effect_data, 15);
            }
        }
    }
}
