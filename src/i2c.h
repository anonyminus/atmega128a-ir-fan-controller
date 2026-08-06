#ifndef I2C_H_
#define I2C_H_

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>

// On ATmega128A, Dedicated Hardware TWI pins are:
// PD0 -> SCL
// PD1 -> SDA
#define I2C_DDR  DDRD
#define I2C_SCL  PORTD0
#define I2C_SDA  PORTD1

void I2C_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
void I2C_TxData(uint8_t data);
void I2C_TxByte(uint8_t devAddrRW, uint8_t data);

#endif /* I2C_H_ */
