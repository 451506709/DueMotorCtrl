#include <Arduino.h>
#include "adc.h"
#include "pwm.h"
#include "vhz.h"
#include "foc.h"
#include "config.h"

volatile int interruptCount;

//buffer that is written by DMA automatically by ADC hardware
volatile uint16_t adc_buf[7];
volatile uint16_t busVoltRaw;
volatile uint16_t current1Raw;
volatile uint16_t current2Raw;
volatile uint16_t invTemp1Raw;
volatile uint16_t invTemp2Raw;
volatile uint16_t motorTemp1Raw;
volatile uint16_t motorTemp2Raw;


void ADC_Handler() //cascaded trigger - PWM triggers ADC which triggers a DMA driven capture of all adc inputs. The end of capture triggers this interrupt
{
	int f=ADC->ADC_ISR;
	if (f & (1<<27)){ //receive counter end of buffer
		busVoltRaw = adc_buf[3] & 0xFFF;
		current1Raw = adc_buf[5] & 0xFFF;
		current2Raw = adc_buf[4] & 0xFFF;
		invTemp1Raw = adc_buf[2] & 0xFFF;
		invTemp2Raw = adc_buf[6] & 0xFFF;
		motorTemp1Raw = adc_buf[1] & 0xFFF;
		motorTemp2Raw = adc_buf[0] & 0xFFF;

		ADC->ADC_RPR=(uint32_t)adc_buf;   // DMA buffer
		ADC->ADC_RCR=7; //# of samples to take - in this case one sample per enabled ADC port

		interruptCount++;
		//now, while still in the handler do the FOC magic right here.		
		if (settings.controlType == 0) updatePosVHz();
		if (settings.controlType == 1) updateFOC();
	}
	if (f & (1 << 26)) busVoltRaw = 4000;
}

void setup_adc()
{
    interruptCount = 0;
  
	pmc_enable_periph_clk(ID_ADC);
	adc_init(ADC, SystemCoreClock, ADC_FREQ_MAX, ADC_STARTUP_FAST); //just about to change a bunch of these parameters with the next command

  /*
  The MCLK is 12MHz on our boards. The ADC can only run 1MHz so the prescaler must be at least 12x.
  The ADC should take Tracking+Transfer for each read when it is set to switch channels with each read
  Start up time happens just when the string of conversions first starts so that's only a single time each trigger

  Calculation for the given settings:
  There will be 8 clocks for start up, then each conversion should take tracking + transfer time
  which is 3 + 5 = 8 clocks. So, 8 + (5 * 8) = 48 clock ticks to do all 5 ADC reads.
  The ADC clock is 1MHz so all conversions take 1M / 48 = 48uS. We want this time to be as fast as possible
  so that all readings are close to the same time period and all are close to the mid point of the PWM cycle
  like they should be. But, it can't be too short and compromise the ADC reading stability
  
  This is all done behind the program's back via DMA so really the program gets an interrupt with all 
  five readings and requires practically no execution time to do so. 
  */
  
  ADC->ADC_MR = (1 << 0) //allow hardware triggering (for PWM based trigger)
	      + (4 << 1) //PWM event 0 is to be our triggering condition
              + (5 << 8) //12x MCLK divider ((This value + 1) * 2) = divisor
	      + (1 << 16) //8 periods start up time (0=0clks, 1=8clks, 2=16clks, 3=24, 4=64, 5=80, 6=96, etc)
              + (0 << 20) //settling time (0=3clks, 1=5clks, 2=9clks, 3=17clks)
              + (2 << 24) //tracking time (Value + 1) clocks
              + (1 << 28);//transfer time ((Value * 2) + 3) clocks
	      
  //for some idiotic reason the arduino analog numbers are backward of the hardware numbers for A0-A7 so keep that in mind
  ADC->ADC_CHER=0xFD; //enable A0-A5, A7 (hardware is reversed for first 8)

  //ADC->ADC_EMR = 3 + (5 << 4) + (1 << 24); //Select Channel 5 for comparison, trigger if ADC out of the window, enable tagging
  //ADC->ADC_CWR = ADC_CURR_LOWTHRESH + (ADC_CURR_HIGHTHRESH << 16); //set comparison window. We fault if current1 goes outside this window

  ADC->ADC_IDR = ~(1<<27); //dont disable the ADC interrupt for rx end
  ADC->ADC_IER = 1<<27; //do enable it
  ADC->ADC_RPR = (uint32_t)adc_buf;   // DMA buffer
  ADC->ADC_RCR = 7; //# of samples to take - in this case one sample per enabled ADC port
  ADC->ADC_RNPR = 0;
  ADC->ADC_RNCR = 0;
  ADC->ADC_PTCR = 1; //enable dma mode
  //ADC->ADC_CR=2; //this would start conversions but we don't do that manually, instead the PWM hardware triggers for us
  NVIC_EnableIRQ(ADC_IRQn);
}

int32_t getBusVoltage()
{
	int32_t valu = (busVoltRaw - settings.busVoltageBias) * settings.busVoltageScale;
	return valu;
}

int32_t getCurrent1()
{
	int32_t valu = (current1Raw - settings.current1Bias) * settings.current1Scale;
	return valu;  
}

int32_t getCurrent2()
{
  	int32_t valu = (current2Raw - settings.current2Bias) * settings.current2Scale;
	return valu;
}

int32_t getInvTemp1()
{
	int32_t valu = (invTemp1Raw - settings.inverterTemp1Bias) * settings.inverterTemp1Scale;
	return valu;
}

int32_t getInvTemp2()
{
  	int32_t valu = (invTemp2Raw - settings.inverterTemp2Bias) * settings.inverterTemp2Scale;
	return valu;
}

int32_t getMotorTemp1()
{
	int32_t valu = (motorTemp1Raw - settings.motorTemp1Bias) * settings.motorTemp1Scale;
	return valu;
}

int32_t getMotorTemp2()
{
  	int32_t valu = (motorTemp2Raw - settings.motorTemp1Bias) * settings.motorTemp1Scale;
	return valu;
}
