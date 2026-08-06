#ifndef I2C_LCD_H_
#define I2C_LCD_H_

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include "I2C.h"

// PCF8574 Backpack Pin Mapping to HD44780
#define LCD_RS              0
#define LCD_RW              1
#define LCD_E               2
#define LCD_BACKLIGHT       3

// Device I2C address (shifted left by 1 for the R/W bit space)
#define LCD_DEV_ADDR        (0x27 << 1)

// HD44780 Commands
#define COMMAND_DISPLAY_CLEAR       0x01    // Clear all display
#define COMMAND_DISPLAY_ON          0x0C    // Display ON, Cursor OFF, Blink OFF
#define COMMAND_DISPLAY_OFF         0x08    // Display OFF, Cursor OFF, Blink OFF
#define COMMAND_ENTRY_MODE          0x06    // Increment cursor auto
#define COMMAND_4BIT_MODE           0x28    // 4-bit mode, 2 lines, 5x8 Font

// Function Declarations
void LCD_Init(void);
void LCD_WriteInitNibble(uint8_t nibble);
void LCD_Data4Bit(uint8_t data);
void LCD_EnablePin(void);
void LCD_WriteCommand(uint8_t commandData);
void LCD_WriteData(uint8_t charData);
void LCD_BackLightOn(void);
void LCD_BackLightOff(void);
void LCD_GotoXY(uint8_t row, uint8_t col);
void LCD_WriteString(char *string);
void LCD_WriteStringXY(uint8_t row, uint8_t col, char *string);

#endif /* I2C_LCD_H_ */
