#include "I2C_LCD.h"

static uint8_t I2C_LCD_Data = 0x00; 

void LCD_EnablePin(void)
{
    I2C_LCD_Data |= (1 << LCD_E);
    I2C_TxByte(LCD_DEV_ADDR, I2C_LCD_Data);
    _delay_us(1); 
    
    I2C_LCD_Data &= ~(1 << LCD_E);
    I2C_TxByte(LCD_DEV_ADDR, I2C_LCD_Data);
    _delay_us(50); 
}

void LCD_WriteInitNibble(uint8_t nibble)
{
    I2C_LCD_Data = (I2C_LCD_Data & 0x0F) | ((nibble << 4) & 0xF0);
    LCD_EnablePin();
    _delay_ms(5); 
}

void LCD_Data4Bit(uint8_t data)
{
    // High Nibble
    I2C_LCD_Data = (I2C_LCD_Data & 0x0F) | (data & 0xF0);
    LCD_EnablePin();
    // Low Nibble
    I2C_LCD_Data = (I2C_LCD_Data & 0x0F) | ((data << 4) & 0xF0);
    LCD_EnablePin();
}

void LCD_WriteCommand(uint8_t commandData)
{
    I2C_LCD_Data &= ~((1 << LCD_RS) | (1 << LCD_RW)); // Clear both RS and RW together
    LCD_Data4Bit(commandData);
}

void LCD_WriteData(uint8_t charData)
{
    I2C_LCD_Data |= (1 << LCD_RS);  
    I2C_LCD_Data &= ~(1 << LCD_RW); 
    LCD_Data4Bit(charData);
}

// Inline toggle pattern matching 
void LCD_BackLightOn(void)  { I2C_LCD_Data |= (1 << LCD_BACKLIGHT);  I2C_TxByte(LCD_DEV_ADDR, I2C_LCD_Data); }
void LCD_BackLightOff(void) { I2C_LCD_Data &= ~(1 << LCD_BACKLIGHT); I2C_TxByte(LCD_DEV_ADDR, I2C_LCD_Data); }

void LCD_GotoXY(uint8_t row, uint8_t col)
{
    LCD_WriteCommand(0x80 + (0x40 * (row % 2)) + (col % 16));
}

void LCD_WriteString(char *string)
{
    while (*string) LCD_WriteData(*string++);
}

void LCD_WriteStringXY(uint8_t row, uint8_t col, char *string)
{
    LCD_GotoXY(row, col);
    LCD_WriteString(string);
}

void LCD_Init(void)
{
    I2C_Init();
    _delay_ms(50); 
    I2C_LCD_Data |= (1 << LCD_BACKLIGHT); 

    for(uint8_t i=0; i<3; i++) LCD_WriteInitNibble(0x03); // Loop instead of writing it out 3 times
    LCD_WriteInitNibble(0x02);
    
    LCD_WriteCommand(COMMAND_4BIT_MODE);       
    LCD_WriteCommand(COMMAND_DISPLAY_OFF);     
    LCD_WriteCommand(COMMAND_DISPLAY_CLEAR);   
    _delay_ms(2);                              
    LCD_WriteCommand(COMMAND_ENTRY_MODE);       
    LCD_WriteCommand(COMMAND_DISPLAY_ON);      
}
