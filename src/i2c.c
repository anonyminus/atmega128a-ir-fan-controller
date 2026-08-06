#include "I2C.h"

void I2C_Init(void)
{
    // REMOVED: I2C_DDR |= (1<<I2C_SCL) | (1<<I2C_SDA); 
    // Forcing pins as standard outputs breaks the Open-Drain requirement of I2C.
    
    TWBR = 72;  // 100KHz SCL frequency at F_CPU = 16MHz
    // TWBR = 32;  // 200KHz
    // TWBR = 12;  // 400KHz
}

void I2C_Start(void)
{
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    while(!(TWCR & (1<<TWINT)));
}

void I2C_Stop(void)
{
    TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWSTO);
}

void I2C_TxData(uint8_t data)
{
    TWDR = data;    // Load data into 8-bit register
    TWCR = (1<<TWINT) | (1<<TWEN);
    while(!(TWCR & (1<<TWINT)));  // Wait for transmission complete
}

void I2C_TxByte(uint8_t devAddrRW, uint8_t data)
{
    I2C_Start();
    I2C_TxData(devAddrRW);
    I2C_TxData(data);
    I2C_Stop();
}
