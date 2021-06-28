//
//Chameleon USB FX2 API Ver1.00 By OPTIMIZE
//

//#include <afxwin.h>         // MFC core and standard components
#include <windows.h>
#include <dbt.h>

#include "cyapi.h"

#ifndef __CUSB2_H__
#define __CUSB2_H__

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short int u16;
typedef signed short int s16;
typedef unsigned int u32;
typedef signed int s32;

#define CUSB_DEBUG   0

struct _cusb2_tcb
{
	HANDLE th;
	CCyUSBEndPoint *ep;
	u8	epaddr;
	LONG xfer;
	u32 ques;
	bool (*cb_func)(u8 *, u32 , _cusb2_tcb*);
	bool looping;
	u32 nsuccess;
	u32 nfailure;
	void* userpointer;
};

typedef struct _cusb2_tcb cusb2_tcb;

class cusb2{
	CCyUSBDevice *_USBDevice;
	bool bDevNodeChange;
	bool bArrived;
	bool loading;
	bool cons_mode;
	u8 _id;

public:
	CCyUSBDevice *USBDevice;
	cusb2(HANDLE h);
	~cusb2();

	bool fwload(u8 id, u8 *fw, const char *manufacturer);
	CCyUSBEndPoint* get_endpoint(u8 addr);
	bool xfer(CCyUSBEndPoint *ep, PUCHAR buf, LONG &len);

	cusb2_tcb *prepare_thread(u8 epaddr, u32 xfer, s32 ques, bool (*cb_func)(u8 *, u32 , _cusb2_tcb*));
	void start_thread(cusb2_tcb *tcb);
	void delete_thread(cusb2_tcb *tcb);
	bool PnpEvent(WPARAM wParam, LPARAM lParam);
	
private:
	s32 check_fx2(u8 id);
};


#endif
