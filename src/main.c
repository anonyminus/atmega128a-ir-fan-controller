#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "I2C_LCD.h"

// --- Hardware Pin Configurations ---
#define BUTTON_DDR        DDRG
#define BUTTON_PIN        PING
#define BTN_FAN_CYCLE     PG3  // Cycles fan speed: (0 -> 1 -> 2 -> 3 -> 0)
#define BTN_SWEEP         PG4  // Physical Sweep ON / OFF Toggle

// --- Hardware Magic Constants ---
#define SERVO_MIN_POS     280
#define SERVO_MAX_POS     650
#define TIMER_MAX_MINUTES 60

// PWM Duty Cycle Definitions (Timer 3)
#define PWM_SPEED_OFF     0
#define PWM_SPEED_LOW     2999
#define PWM_SPEED_MID     3999
#define PWM_SPEED_HIGH    4998

// IR Remote Command Hex Maps
typedef enum {
    IR_CMD_TIMER_DEC  = 0x07, // Button -
    IR_CMD_TIMER_INC  = 0x15, // Button +
    IR_CMD_ALL_OFF    = 0x16, // Remote button for ALL OFF (Speed 0)
    IR_CMD_PLAY_PAUSE = 0x43, // Play/Pause Button
    IR_CMD_SPEED_1    = 0x45, // Remote button for Speed 1 (Toggle)
    IR_CMD_SPEED_2    = 0x46, // Remote button for Speed 2 (Toggle)
    IR_CMD_SPEED_3    = 0x47, // Remote button for Speed 3 (Toggle)
    IR_CMD_SWEEP      = 0x09  // Physical sweep button equivalent (Toggle)
} IR_Command_t;

// --- Button FSM States ---
typedef enum { 
    STATE_PUSHED, 
    STATE_RELEASED 
} ButtonState_t;

typedef enum { 
    ACTION_NONE, 
    ACTION_PUSH, 
    ACTION_RELEASE 
} ButtonAction_t;

typedef struct {
    volatile uint8_t *ddr;       
    volatile uint8_t *pin;       
    uint8_t pinNum;             
    ButtonState_t prevState;           
} Button_t;

// --- Global System Architecture Metrics ---
static uint8_t  currentFanSpeed  = 0;  // 0: Off, 1: Low, 2: Mid, 3: High
static uint8_t  isSweeping       = 0;  // 0: Stop, 1: Sweep
static uint16_t servoPos         = SERVO_MIN_POS;
static int8_t   servoDir         = 1;  // 1: Up, -1: Down

// Remote & Hardware Timers
static uint8_t  setMinutes       = 0;  
static int32_t  remainingSeconds = 0; 
static uint8_t  isTimerActive    = 0;  
static uint8_t  isTimerPaused    = 0;  
static uint8_t  lcdNeedsUpdate   = 1;  

static uint16_t ms_counter       = 0;    
static uint8_t  servo_ms_counter = 0;

// IR Module Frame Allocations
static volatile uint8_t  ir_bytes[4];
static volatile uint8_t  ir_bit_count = 0;
static volatile uint32_t ir_result    = 0;
static volatile uint8_t  ir_flag      = 0;

// --- Forward Declarations ---
void Button_Init(Button_t *btn, volatile uint8_t *ddr, volatile uint8_t *pin, uint8_t pinNum);
ButtonAction_t Button_GetState(Button_t *btn);
void Set_Fan_Speed(uint8_t speed);
void IR_Init(void);
uint32_t IR_Receive(void);
void Update_LCD_Display(void);

void Handle_Physical_Buttons(Button_t *cycle, Button_t *sweep);
void Handle_Remote_Input(void);
void Handle_Servo_Sweep(void);
void Update_System_Tick(void);

// --- Main Execution Engine ---
int main(void) {
    // Hardware PWM Configuration (Timer 3)
    DDRE  |= (1 << PE5) | (1 << PE3);
    TCCR3A |= (1 << COM3A1) | (1 << COM3C1) | (1 << WGM31);
    TCCR3B |= (1 << WGM33) | (1 << WGM32) | (1 << CS31) | (1 << CS30); // Prescaler 64
    TCCR3C  = 0;
    ICR3    = 4999;

    // Board Input Configurations
    Button_t btn_cycle, btn_sweep;
    Button_Init(&btn_cycle, &BUTTON_DDR, &BUTTON_PIN, BTN_FAN_CYCLE);
    Button_Init(&btn_sweep, &BUTTON_DDR, &BUTTON_PIN, BTN_SWEEP);

    IR_Init();  
    LCD_Init();
    LCD_BackLightOn();
    
    Set_Fan_Speed(0); 
    sei(); 

    while (1) {
        Handle_Physical_Buttons(&btn_cycle, &btn_sweep);
        Handle_Remote_Input();
        Handle_Servo_Sweep();
        Update_System_Tick();

        // Safe LCD Flush Frame 
        if (lcdNeedsUpdate && (ir_bit_count == 0)) {
            Update_LCD_Display();
            lcdNeedsUpdate = 0; 
        }
    }
}

// --- Driver & Logic Implementation ---

void Button_Init(Button_t *btn, volatile uint8_t *ddr, volatile uint8_t *pin, uint8_t pinNum) {
    btn->ddr       = ddr;
    btn->pin       = pin;
    btn->pinNum    = pinNum;
    btn->prevState = STATE_RELEASED; 
    
    *btn->ddr &= ~(1 << btn->pinNum); // Input Mode
    *(btn->ddr + 1) |= (1 << btn->pinNum); // Offset map directly into PORT register for Internal Pull-up
}

ButtonAction_t Button_GetState(Button_t *btn) {
    ButtonState_t curr = (*btn->pin & (1 << btn->pinNum)) ? STATE_RELEASED : STATE_PUSHED;
    
    if (curr != btn->prevState) {
        _delay_ms(5); // Debounce delay execution
        ButtonState_t confirm = (*btn->pin & (1 << btn->pinNum)) ? STATE_RELEASED : STATE_PUSHED;
        
        if (confirm != btn->prevState) {
            btn->prevState = confirm; 
            return (confirm == STATE_PUSHED) ? ACTION_PUSH : ACTION_RELEASE;
        }
    }
    return ACTION_NONE; 
}

void Set_Fan_Speed(uint8_t speed) {
    currentFanSpeed = speed;

    switch (speed) {
        case 0:
            OCR3A            = PWM_SPEED_OFF;  
            isSweeping       = 0;     
            OCR3C            = SERVO_MIN_POS;  
            isTimerActive    = 0;  
            isTimerPaused    = 0;
            remainingSeconds = 0;
            setMinutes       = 0;    
            break;
        case 1:
            OCR3A = PWM_SPEED_LOW;
            break;
        case 2:
            OCR3A = PWM_SPEED_MID;
            break;
        case 3:
            OCR3A = PWM_SPEED_HIGH;
            break;
        default:
            break;
    }
    lcdNeedsUpdate = 1; 
}

void IR_Init(void) {
    DDRE  &= ~(1 << PE4);  // Input
    PORTE |= (1 << PE4);   // Pull-up Enabled

    EICRB |= (1 << ISC41);
    EICRB &= ~(1 << ISC40); // Catch Falling Edge Sequences
    EIMSK |= (1 << INT4);   

    TCCR1A = 0x00;
    TCCR1B = (1 << CS12);   // Prescaler 256 (16us ticks)
    TCNT1  = 0;
}

ISR(INT4_vect) {
    uint16_t time = TCNT1;
    TCNT1 = 0; 

    if (time > 750 && time < 950) { 
        ir_bit_count = 0;
        ir_bytes[0] = ir_bytes[1] = ir_bytes[2] = ir_bytes[3] = 0;
    }
    else if (ir_bit_count < 32) {
        uint8_t byte_idx = ir_bit_count / 8;
        uint8_t bit_idx  = ir_bit_count % 8;

        if (time >= 110 && time <= 180) { // Bit '1'
            ir_bytes[byte_idx] |= (1 << bit_idx); 
            ir_bit_count++;
        }
        else if (time >= 40 && time < 110) { // Bit '0'
            ir_bit_count++; 
        }
        else {
            ir_bit_count = 0; // Error reset
        }

        if (ir_bit_count == 32) {
            if (ir_bytes[2] == (uint8_t)(~ir_bytes[3])) { // Integrity Handshake
                ir_result = ((uint32_t)ir_bytes[0] << 24) |
                            ((uint32_t)ir_bytes[1] << 16) |
                            ((uint32_t)ir_bytes[2] << 8)  |
                            ir_bytes[3];
                ir_flag = 1;
            }
            ir_bit_count = 0; 
        }
    }
}

uint32_t IR_Receive(void) {
    if (ir_flag) {
        ir_flag = 0;
        return ir_result;
    }
    return 0;
}

void Update_LCD_Display(void) {
    char line1[17];
    char line2[17];
    
    snprintf(line1, sizeof(line1), "Spd:%d Swp:%s", currentFanSpeed, isSweeping ? "ON " : "OFF");
    LCD_WriteStringXY(0, 0, line1);
    
    if (isTimerActive) {
        int16_t dispMins = remainingSeconds / 60;
        int16_t dispSecs = remainingSeconds % 60;
        snprintf(line2, sizeof(line2), "Run:%02d:%02d %s", dispMins, dispSecs, isTimerPaused ? "PAUSE" : "  ");
    } else {
        snprintf(line2, sizeof(line2), "Set Timer:%2d min", setMinutes);
    }
    LCD_WriteStringXY(1, 0, line2);
}

// --- Abstract Loop Segments ---

void Handle_Physical_Buttons(Button_t *cycle, Button_t *sweep) {
    if (Button_GetState(cycle) == ACTION_RELEASE) {
        Set_Fan_Speed((currentFanSpeed + 1) % 4);
    }
    
    if (Button_GetState(sweep) == ACTION_RELEASE) {
        if (currentFanSpeed > 0 && !isTimerPaused) { 
            isSweeping = !isSweeping; 
            lcdNeedsUpdate = 1;
        }
    }
}

void Handle_Remote_Input(void) {
    uint32_t ir_code = IR_Receive();
    if (ir_code == 0) return;

    uint8_t cmd = (ir_code >> 8) & 0xFF; 

    switch ((IR_Command_t)cmd) {
        case IR_CMD_TIMER_INC:
            if (!isTimerActive && setMinutes < TIMER_MAX_MINUTES) {
                setMinutes++;
                lcdNeedsUpdate = 1;
            }
            break; 
            
        case IR_CMD_TIMER_DEC:
            if (!isTimerActive && setMinutes > 0) {
                setMinutes--;
                lcdNeedsUpdate = 1;
            }
            break; 
        
        case IR_CMD_PLAY_PAUSE:
            if (isTimerActive) {
                if (isTimerPaused) {
                    Set_Fan_Speed(0); // Cancel Active Sequence Execution completely
                } else {
                    isTimerPaused = 1;
                    OCR3A = PWM_SPEED_OFF; // Instant safety dampening for hardware EMI noise reduction
                }
            } else if (setMinutes > 0) {
                isTimerActive    = 1;
                isTimerPaused    = 0;
                remainingSeconds = (int32_t)setMinutes * 60;
                if (currentFanSpeed == 0) { 
                    Set_Fan_Speed(1); // Deploy Safe Initial Low Motor State
                }
            }
            lcdNeedsUpdate = 1;
            break;

        // --- Remote ALL OFF Control ---
        case IR_CMD_ALL_OFF:
            Set_Fan_Speed(0); // Shut down everything
            break;
            
        // --- Remote Speed Toggle Controls ---
        case IR_CMD_SPEED_1:
            if (currentFanSpeed == 1) {
                Set_Fan_Speed(0); // Toggle OFF if already at Speed 1
            } else {
                Set_Fan_Speed(1); // Turn ON to Speed 1
            }
            break;
            
        case IR_CMD_SPEED_2:
            if (currentFanSpeed == 2) {
                Set_Fan_Speed(0); // Toggle OFF if already at Speed 2
            } else {
                Set_Fan_Speed(2); // Turn ON to Speed 2
            }
            break;
            
        case IR_CMD_SPEED_3:
            if (currentFanSpeed == 3) {
                Set_Fan_Speed(0); // Toggle OFF if already at Speed 3
            } else {
                Set_Fan_Speed(3); // Turn ON to Speed 3
            }
            break;
            
        // --- Remote Sweep Toggle Control ---
        case IR_CMD_SWEEP: // Already behaves as a toggle
            if (currentFanSpeed > 0 && !isTimerPaused) { 
                isSweeping = !isSweeping; 
                lcdNeedsUpdate = 1;
            }
            break;

        default:
            break;
    }
}

void Handle_Servo_Sweep(void) {
    if (isSweeping && currentFanSpeed > 0 && !isTimerPaused) {
        if (servo_ms_counter >= 15) {
            servo_ms_counter = 0;
            servoPos += servoDir; 
            OCR3C = servoPos;

            if (servoPos >= SERVO_MAX_POS) {
                servoDir = -1; 
            } else if (servoPos <= SERVO_MIN_POS) {
                servoDir = 1;  
            }
        }
    }
}

void Update_System_Tick(void) {
    _delay_ms(1);
    ms_counter++;
    servo_ms_counter++;
    
    if (ms_counter >= 1000) { 
        ms_counter = 0;
        if (isTimerActive && !isTimerPaused && remainingSeconds > 0) {
            remainingSeconds--;
            lcdNeedsUpdate = 1; 
            
            if (remainingSeconds == 0) {
                Set_Fan_Speed(0); // Expired safely
            }
        }
    }
}
