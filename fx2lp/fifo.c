//-----------------------------------------------------------------------------
//   File:      bulkloop.c
//   Contents:  Hooks required to implement USB peripheral function.
//
// $Archive: /USB/Examples/FX2LP/bulkloop/bulkloop.c $
// $Date: 3/23/05 2:55p $
// $Revision: 4 $
//
//
//-----------------------------------------------------------------------------
// Copyright 2003, Cypress Semiconductor Corporation
//-----------------------------------------------------------------------------
#pragma NOIV               		// Do not generate interrupt vectors

#include "fx2.h"
#include "fx2regs.h"
#include "syncdly.h"            // SYNCDELAY macro

extern BOOL GotSUD;             // Received setup data flag
extern BOOL Sleep;
extern BOOL Rwuen;
extern BOOL Selfpwr;

BYTE Configuration;             // Current configuration
BYTE AlternateSetting;          // Alternate settings

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;

void timer_init();

#define VR_NAKALL_ON    0xD0
#define VR_NAKALL_OFF   0xD1

#define	MODE_START		(0 << 6)
#define	MODE_REG_READ	(1 << 6)
#define	MODE_REG_WRITE	(2 << 6)
#define	MODE_IDLE		(3 << 6)

#define	CMD_EP2IN_START		0x50
#define	CMD_EP2IN_STOP		0x51
#define CMD_EP6IN_START		0x52
#define	CMD_EP6IN_STOP		0x53
#define CMD_EP8OUT_START	0x54
#define CMD_EP8OUT_STOP		0x55

#define	CMD_PORT_CFG		0x56	// reg_addr_mask, out_pins
#define	CMD_REG_READ		0x57	// addr	(return 1byte)
#define	CMD_REG_WRITE		0x58	// addr, value
#define	CMD_PORT_READ		0x59	// (return 1byte)
#define	CMD_PORT_WRITE		0x5a	// value
#define	CMD_IFCONFIG		0x5b	// value

#define	CMD_MODE_IDLE		0x5f

#ifdef ENABLE_DEBUG_SERIAL
void tty_out(s8 *s){
	u8 i=0;
	while(s[i]){
		if(s[i]=='\n'){
			SBUF0=0x0d;
			for(;;) if(TI==1) break;
			TI=0;
			SBUF0=0x0a;
		}
		else{
			SBUF0=s[i];
		}
		i++;
		for(;;){
			if(TI==1) break;
		}
		TI=0;
	}
}

const s8 hexchar[16]={
'0','1','2','3','4','5','6','7',
'8','9','A','B','C','D','E','F'
};

void tty_hex(u8 u){
	u8 s[3];
	s[0]=hexchar[u>>4];
	s[1]=hexchar[u & 0x0f];
	s[2]=0;
	tty_out(s);
}
#endif


//-----------------------------------------------------------------------------
// Task Dispatcher hooks
//   The following hooks are called by the task dispatcher.
//-----------------------------------------------------------------------------

void TD_Init(void)             // Called once at startup
{
	// set the CPU clock to 48MHz and drive CLKOUT
    CPUCS = ((CPUCS & ~bmCLKSPD) | bmCLKSPD1 | bmCLKOE) ;

	// set FD bus 8bit
	EP2FIFOCFG = 0x04;
	SYNCDELAY;

	EP4FIFOCFG = 0x04;
	SYNCDELAY;

	EP6FIFOCFG = 0x04;
	SYNCDELAY;

	EP8FIFOCFG = 0x04;
	SYNCDELAY;

#ifdef ENABLE_DEBUG_SERIAL
	//serial port init
	UART230  = 0x01; // port0 set 115K baud
	SCON0    = 0x50; // 0101 0000 mode=1, REN_1=1
	TI       = 0;

//	tty_out("\n*** CFX2LOG original by OPTIMIZE, modified by splash5, Ver2.00 ***");
	tty_hex(0);
#endif

	timer_init();
	
	// EP1 can be in and out
	// see TRM section 15.14
	EP1OUTCFG = 0xa0;
	EP1INCFG = 0xa0;
	SYNCDELAY;

	// clear EP1 out buffer count
	EP1OUTBC = 0;

	// see TRM 15.5.9
	REVCTL = 0x03;
	SYNCDELAY;		

	// IN, Size = 512, buf = Quad Buf, BULK transfer
	EP2CFG = 0xe0;
	SYNCDELAY;                    

	// IN, Size = 512, buf = Quad Buf, BULK transfer
	EP6CFG = 0xe0;
	SYNCDELAY;

	// disable EP8
	EP8CFG &= 0x7f;
	SYNCDELAY;                    

	// disable EP4
	EP4CFG &= 0x7f;
	SYNCDELAY;

	// FIFO status flag all active LOW
	FIFOPINPOLAR = 0x00;
	SYNCDELAY;

	// EP2 auto in length 512 bytes
	EP2AUTOINLENH = 0x02;
	SYNCDELAY;
	
	EP2AUTOINLENL = 0x00;
	SYNCDELAY;
	
	// EP6 auto in length 512 bytes
	EP6AUTOINLENH = 0x02;
	SYNCDELAY;
	
	EP6AUTOINLENL = 0x00;
	SYNCDELAY;

	// reset FIFO
	FIFORESET = 0x80; SYNCDELAY; // activate NAK-ALL to avoid race conditions
	FIFORESET = 0x82; SYNCDELAY; // reset, FIFO 2
	FIFORESET = 0x84; SYNCDELAY; // reset, FIFO 4
	FIFORESET = 0x86; SYNCDELAY; // reset, FIFO 6
	FIFORESET = 0x88; SYNCDELAY; // reset, FIFO 8
	FIFORESET = 0x00; SYNCDELAY; // deactivate NAK-ALL

	// enable dual autopointer feature & inc
	AUTOPTRSETUP = 0x07;

	// FLAGA = ep2FF, FLAGB = ep6FF, FLAGC = ep8EF
	PINFLAGSAB = 0xec;
	SYNCDELAY;
	PINFLAGSCD = 0x0b;
	SYNCDELAY;

	// default in idle mode
	IOD = MODE_IDLE;

	// keeps MODE pins output
	OED = 0xc0;
}

u8 val_ifconfig;

void TD_Poll(void)              // Called repeatedly while the device is idle
{
	static u8 cmd_cnt, ret_cnt;

	// rename to output pin mask?
	static u8 reg_addr_mask;

	if (!(EP1OUTCS & bmEPBUSY))
	{
		cmd_cnt = EP1OUTBC;
		ret_cnt = 0;

		AUTOPTR1H=MSB( &EP1OUTBUF );
        AUTOPTR1L=LSB( &EP1OUTBUF );
		AUTOPTRH2=MSB( &EP1INBUF );
        AUTOPTRL2=LSB( &EP1INBUF );

		while (cmd_cnt-- != 0)
		{
			switch(XAUTODAT1)
			{
				case CMD_PORT_CFG:
					// PORTD[7:6] fixed to mode selection
					// first XAUTODAT1 means reg_addr_mask bits
					reg_addr_mask = (0xc0 | XAUTODAT1);

					// second XAUTODAT1 means pins as output
					// also set reg_addr and mode as output
					OED = (reg_addr_mask | XAUTODAT1);

					reg_addr_mask = ~reg_addr_mask;
	 			    cmd_cnt -=2;
					break;
				case CMD_IFCONFIG:
					// just cache value
					val_ifconfig = XAUTODAT1;
					cmd_cnt -= 1;
					break;
				case CMD_REG_WRITE:
					// set which register to write
					IOD = MODE_REG_WRITE | (IOD & reg_addr_mask) | XAUTODAT1;

					// switch EZUSB interface to port mode
					IFCONFIG = 0xE0;

					// set PORTB all output
					OEB = 0xff;
					// port data from XAUTODAT1
					IOB = XAUTODAT1;

					cmd_cnt -= 2;
					break;
				case CMD_REG_READ:
					// set which register to read
					IOD = MODE_REG_READ | (IOD & reg_addr_mask) | XAUTODAT1;

					// switch EZUSB interface to port mode
					IFCONFIG = 0xE0;

					// set PORTB all input
					OEB = 0x00;

					// port data into XAUTODAT2
					XAUTODAT2 = IOB;

					ret_cnt++;
					cmd_cnt -= 1;
					break;
				case CMD_PORT_WRITE:
					// 
					IOD = (IOD & ~reg_addr_mask) | XAUTODAT1;
					cmd_cnt -=1;
					break;
				case CMD_PORT_READ:
					// all signals on PORTD
					XAUTODAT2 = IOD;
					ret_cnt++;
					break;
				case CMD_EP6IN_START:
					IFCONFIG = val_ifconfig;

					FIFORESET = 0x80;
					SYNCDELAY;

					// reset EP6 FIFO
					FIFORESET = 0x86;
					SYNCDELAY;

					FIFORESET = 0x00;
					SYNCDELAY;

					// 8bit, Auto-IN
					EP6FIFOCFG = 0x0c;
					SYNCDELAY;

					IOD = MODE_START | (IOD & 0x3f);
					break;
				case CMD_EP2IN_START:
					IFCONFIG = val_ifconfig;

					FIFORESET = 0x80;
					SYNCDELAY;

					// reset EP2 FIFO
					FIFORESET = 0x82;
					SYNCDELAY;

					FIFORESET = 0x00;
					SYNCDELAY;

					// 8bit, Auto-IN
					EP2FIFOCFG = 0x0c;
					SYNCDELAY;

					IOD = MODE_START | (IOD & 0x3f);
					break;
				case CMD_EP8OUT_START:
					// TODO
					break;
				case CMD_EP2IN_STOP:
					EP2FIFOCFG = 0x04;	// 8bit
					break;
				case CMD_EP6IN_STOP:
					EP6FIFOCFG = 0x04;	// 8bit
					break;
				case CMD_EP8OUT_STOP:
					EP8FIFOCFG = 0x04;	// 8bit
					break;
				case CMD_MODE_IDLE:
					IOD = MODE_IDLE | (IOD & 0x3f);
					break;
			}
		}

		// clear EP1 out buffer
		EP1OUTBC = 0;	//arm EP1OUT

		if(ret_cnt)
			EP1INBC = ret_cnt;
	}
}

// Called before the device goes into suspend mode
BOOL TD_Suspend(void)
{
   return(TRUE);
}

// Called after the device resumes
BOOL TD_Resume(void)
{
   return(TRUE);
}

//-----------------------------------------------------------------------------
// Device Request hooks
//   The following hooks are called by the end point 0 device request parser.
//-----------------------------------------------------------------------------

BOOL DR_GetDescriptor(void)
{
   return(TRUE);
}

BOOL DR_SetConfiguration(void)   // Called when a Set Configuration command is received
{
   Configuration = SETUPDAT[2];
   return(TRUE);            // Handled by user code
}

BOOL DR_GetConfiguration(void)   // Called when a Get Configuration command is received
{
   EP0BUF[0] = Configuration;
   EP0BCH = 0;
   EP0BCL = 1;
   return(TRUE);            // Handled by user code
}

BOOL DR_SetInterface(void)       // Called when a Set Interface command is received
{
   AlternateSetting = SETUPDAT[2];
   return(TRUE);            // Handled by user code
}

BOOL DR_GetInterface(void)       // Called when a Set Interface command is received
{
   EP0BUF[0] = AlternateSetting;
   EP0BCH = 0;
   EP0BCL = 1;
   return(TRUE);            // Handled by user code
}

BOOL DR_GetStatus(void)
{
   return(TRUE);
}

BOOL DR_ClearFeature(void)
{
   return(TRUE);
}

BOOL DR_SetFeature(void)
{
   return(TRUE);
}

BOOL DR_VendorCmnd(void)
{
  BYTE tmp;
  
  switch (SETUPDAT[1])
  {
     case VR_NAKALL_ON:
        tmp = FIFORESET;
        tmp |= bmNAKALL;      
        SYNCDELAY;                    
        FIFORESET = tmp;
        break;
     case VR_NAKALL_OFF:
        tmp = FIFORESET;
        tmp &= ~bmNAKALL;      
        SYNCDELAY;                    
        FIFORESET = tmp;
        break;
     default:
        return(TRUE);
  }

  return(FALSE);
}

//-----------------------------------------------------------------------------
// USB Interrupt Handlers
//   The following functions are called by the USB interrupt jump table.
//-----------------------------------------------------------------------------

// Setup Data Available Interrupt Handler
void ISR_Sudav(void) interrupt 0
{
   GotSUD = TRUE;            // Set flag
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSUDAV;         // Clear SUDAV IRQ
}

// Setup Token Interrupt Handler
void ISR_Sutok(void) interrupt 0
{
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSUTOK;         // Clear SUTOK IRQ
}

void ISR_Sof(void) interrupt 0
{
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSOF;            // Clear SOF IRQ
}

void ISR_Ures(void) interrupt 0
{
   // whenever we get a USB reset, we should revert to full speed mode
   pConfigDscr = pFullSpeedConfigDscr;
   ((CONFIGDSCR xdata *) pConfigDscr)->type = CONFIG_DSCR;
   pOtherConfigDscr = pHighSpeedConfigDscr;
   ((CONFIGDSCR xdata *) pOtherConfigDscr)->type = OTHERSPEED_DSCR;

   EZUSB_IRQ_CLEAR();
   USBIRQ = bmURES;         // Clear URES IRQ
}

void ISR_Susp(void) interrupt 0
{
   Sleep = TRUE;
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSUSP;
}

void ISR_Highspeed(void) interrupt 0
{
   if (EZUSB_HIGHSPEED())
   {
      pConfigDscr = pHighSpeedConfigDscr;
      ((CONFIGDSCR xdata *) pConfigDscr)->type = CONFIG_DSCR;
      pOtherConfigDscr = pFullSpeedConfigDscr;
      ((CONFIGDSCR xdata *) pOtherConfigDscr)->type = OTHERSPEED_DSCR;
   }

   EZUSB_IRQ_CLEAR();
   USBIRQ = bmHSGRANT;
}
void ISR_Ep0ack(void) interrupt 0
{
}
void ISR_Stub(void) interrupt 0
{
}
void ISR_Ep0in(void) interrupt 0
{
}
void ISR_Ep0out(void) interrupt 0
{
}
void ISR_Ep1in(void) interrupt 0
{
}
void ISR_Ep1out(void) interrupt 0
{
}
void ISR_Ep2inout(void) interrupt 0
{
}
void ISR_Ep4inout(void) interrupt 0
{
}
void ISR_Ep6inout(void) interrupt 0
{
}
void ISR_Ep8inout(void) interrupt 0
{
}
void ISR_Ibn(void) interrupt 0
{
}
void ISR_Ep0pingnak(void) interrupt 0
{
}
void ISR_Ep1pingnak(void) interrupt 0
{
}
void ISR_Ep2pingnak(void) interrupt 0
{
}
void ISR_Ep4pingnak(void) interrupt 0
{
}
void ISR_Ep6pingnak(void) interrupt 0
{
}
void ISR_Ep8pingnak(void) interrupt 0
{
}
void ISR_Errorlimit(void) interrupt 0
{
}
void ISR_Ep2piderror(void) interrupt 0
{
}
void ISR_Ep4piderror(void) interrupt 0
{
}
void ISR_Ep6piderror(void) interrupt 0
{
}
void ISR_Ep8piderror(void) interrupt 0
{
}
void ISR_Ep2pflag(void) interrupt 0
{
}
void ISR_Ep4pflag(void) interrupt 0
{
}
void ISR_Ep6pflag(void) interrupt 0
{
}
void ISR_Ep8pflag(void) interrupt 0
{
}
void ISR_Ep2eflag(void) interrupt 0
{
}
void ISR_Ep4eflag(void) interrupt 0
{
}
void ISR_Ep6eflag(void) interrupt 0
{
}
void ISR_Ep8eflag(void) interrupt 0
{
}
void ISR_Ep2fflag(void) interrupt 0
{
}
void ISR_Ep4fflag(void) interrupt 0
{
}
void ISR_Ep6fflag(void) interrupt 0
{
}
void ISR_Ep8fflag(void) interrupt 0
{
}
void ISR_GpifComplete(void) interrupt 0
{
}
void ISR_GpifWaveform(void) interrupt 0
{
}
