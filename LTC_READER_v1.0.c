/*====================================================================
======================================================================
	SENDS SMPTE LTC SIGNAL VIA DIFFERENTIAL MANCHESTER ENCODING 
	[BI-PHASE MARK CODE]
	29.97fps (30 * 1000/1001) Version
    Version 1.0:  Reads & Stores 29.97 NDF Timecode
======================================================================
======================================================================
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <string.h>
#include "LTC.h"

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+++++++++++++++++++++++ GLOBAL SYMBOLS ++++++++++++++++++++++++
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Timecode Values Variables
volatile unsigned char frames = 0, seconds = 0, minutes = 0, hours = 0;
volatile unsigned char userbits[8] = {2,4,6,8,1,3,5,7};
static unsigned char sections[10]; //10 8-bit sections to make up 80-bit LTC block
volatile unsigned char parity_bit = 0; //Even Parity

//Timecode Generator Variables
volatile unsigned char midbit_send = 1;
volatile unsigned char sendbit = 0;
volatile unsigned char sendsection = 0;
volatile unsigned char sendsignal = 0b00000010; //For PORTB1
volatile unsigned char i = 0;
volatile unsigned int debugbit = 0;

//Timecode Reader Variables
volatile unsigned char frame_subcount = 0;  //Counts to "FRAME_MIDBITCOUNT" to display Frame
volatile unsigned char midbit_period = MIDBIT_CLOCKPERIOD;
volatile unsigned char current_pin = 0;
volatile unsigned char previous_pin = 0;
volatile unsigned char jamDetect = 0;
volatile unsigned char midbitBoundary = 0;
volatile unsigned char jamSync = 0;
volatile unsigned char changeDetect = 0;
volatile unsigned char codewordFound = 0;
volatile unsigned char ltcBit = 0;
volatile unsigned char ltcBitCount = 0;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+++++++++++++++++++++++ MAIN ENTRY ++++++++++++++++++++++++++++
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
int main(void)
{
	//SLEEP MODE & POWER MANAGEMENT SETUP
	SMCR |= 0b00000001; //Enable SE(sleep enable) bit and Idle Sleep Mode
	//PRR |=  ; //Power Reduction Register
    MCUCR |= 0b01100000; //The following two commands work to disable Brown-Out detection (BOD)
	MCUCR ^= 0b00100000;
	DIDR1 |= 0b00000011; //Disable Analog Comparator Pins AIN1/AIN0 Digital Input [PINs will always read 0]
	DIDR0 |= 0b00000000; //Enable ADC Pins ADC0D/ADC1D/ADC2D/ADC3D/ADC4D/ADC5D Digital Input
	ACSR |= 0b10000000; //Disable Analog Comparator

	//TIMER SETUP
	GTCCR = (1 << TSM) | (1 << PSRASY) | (1 << PSRSYNC); //Reset Prescaler and halts Timer0/1/2 so they can be synced when TSM bit cleared
    //Timer1 Output Compare Match Setup
        TCCR1A = 0x00; //0b00000000;  OC1A disconnected from Timer
        TCCR1B |= (1 << WGM12); //CTC (Compare Clear) Set to recount once Compare Match made
        TCCR1B |= (1 << CS10); 	//Start Timer by setting clock source
        TIMSK1 |= (1 << OCIE1A); //Timer/Counter1 Output Compare A Interrupt Enabled
        OCR1A = MIDBIT_CLOCKPERIOD; //Set Output Compare Value
        TCNT1 = 0x0000; //Set 16-bit Counter to 0
    //Timer1 Input Capture Setup

	//PIN SETUP
    //Generator Pin: PB1
        DDRB |= (1 << DDB1); //Set PB1 Pin as output
        PORTB &= 0b11111101;	//Clear PB1 Pin to Low (Pin Generates LTC)
    //Reader Pin: PC5
        DDRC |= (0 << DDC5); //Set PC5 Pin as Input
        PORTC &= 0b11011111; //Clear PC5 Pin Pullup Resistor to off
        previous_pin = (0b00100000 & PINC); //Read initial Pin level and set as default
        previous_pin = previous_pin >> 5; //Move PC5 spot to lsb spot to check
        previous_pin &= 0b00000001; //AND helps ignore any other PIN values picked up
    
    //Enable Global Interrupt Flag
	sei(); //Enable Global Interrupts
    
    //Start all Timers
    GTCCR = (0 << TSM); //Start Timers
    
    //Generator Preset (starting at zero time [HIGH], so that next period is the mid-bit)
    PORTB ^= sendsignal; //Start Generator LTC High at top of signal
    smpte_signalGenerate(); //Presets for first mid-bit value
    
	while(1)
	{
		sleep_mode();	//Go into Idle Sleep Mode until Interrupt occurs
	}
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+++++++++++++++++++++++ INTERRUPTS ++++++++++++++++++++++++++++
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//GENERATOR, DISPLAY & READER: Mid-bit (of the 80 LTC bits) Timer Compare Interrupt
ISR(TIMER1_COMPA_vect)
{
    //4 cycles from Main to Here
    
    //GENERATOR [ON PB1]
    PORTB ^= sendsignal;     //XOR with sendsignal.  Will switch when one in its place in sendsignal
    smpte_signalGenerate();     //Setup Next Generator Signal midbit value

    //DISPLAY
    display_smpte();
    
    //READER [ON PC5]
    readJam_smpte();
}


