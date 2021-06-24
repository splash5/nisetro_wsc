#include "fx2.h"
#include "fx2regs.h"
#include "fx2sdly.h"            // SYNCDELAY macro

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;

#ifdef ENABLE_DEBUG_SERIAL
u8 ms_counter;

void timer0() interrupt 1  //register BANK1
{
	if(ms_counter != 0) --ms_counter;
	TL0 = LSB(0x10000-12000000/1000);
	TH0 = MSB(0x10000-12000000/1000);
}

void timer_init()
{
	TMOD = 0x01;	// GATE0:0 CT0:0 M1M0:01
	CKCON = (CKCON&(~0x08)) | 0x08; //T0M:1(48MHz/4=12MHz)

	ms_counter = 0;
	TR0 = 0;		//enable timer0
	ET0 = 0;		//timer0 irq enable
}
#else
void timer_init(void)
{
	TR0 = 0;
	ET0 = 0;
}
#endif
