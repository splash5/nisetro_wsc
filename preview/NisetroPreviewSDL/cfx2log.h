#ifndef __CFX2_LOG_H__
#define __CFX2_LOG_H__

#define VIDEO_EP_ADDR		0x82
#define AUDIO_EP_ADDR		0x86

#define	CMD_EP2IN_START		0x50	// FIFO FX2 -> PC, for video
#define	CMD_EP2IN_STOP		0x51	//
#define CMD_EP6IN_START		0x52	// FIFO FX2 -> PC, for audio
#define	CMD_EP6IN_STOP		0x53	// 
#define CMD_EP8OUT_START	0x54	// FIFO PC -> FX2
#define CMD_EP8OUT_STOP		0x55

#define	CMD_PORT_CFG		0x56	// addr_mask, out_pins
#define	CMD_REG_READ		0x57	// addr	(return 1byte)
#define	CMD_REG_WRITE		0x58	// addr, value
#define	CMD_PORT_READ		0x59	// (return 1byte PORTD full state)
#define	CMD_PORT_WRITE		0x5a	// value
#define	CMD_IFCONFIG		0x5b	// value

#define	CMD_MODE_IDLE		0x5f

#define BUF_LEN	(1024*8)	// USB転送に使用するバッファサイズ
#define	QUE_NUM	(32)		// USB転送に使用するバッファの個数

#define PIO_FIFO_DIR		0x08
#define	PIO_RESET			0x10
#define PIO_DROP			0x20

#endif
