//
//Chameleon USB FX2 API Ver1.00 By OPTIMIZE
//

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>

#include "cusb2.h"

cusb2::cusb2(HANDLE h)	//コンソールモード時はhはNULLを指定
{
	loading = false;
	_id = 0;
	USBDevice = new CCyUSBDevice(h);
	bDevNodeChange = false;
	bArrived = false;
	if(h == NULL)
	{
		cons_mode = true;
		_USBDevice= NULL;
	}
	else
	{
		cons_mode = false;
		_USBDevice= new CCyUSBDevice();
	}
}

cusb2::~cusb2()
{
	if(USBDevice->IsOpen()) USBDevice->Close();
	delete USBDevice;
	delete _USBDevice;
}

bool cusb2::fwload(u8 id, u8 *fw, const char *manufacturer)
{
	u8 d;
	bool find = false;
	_id = id;
	if(USBDevice->IsOpen()) USBDevice->Close();

	if(cons_mode || (loading == false)){
		for(d=0; d < USBDevice->DeviceCount(); d++)
		{
			USBDevice->Open(d);
			if(((id == 0)&&((USBDevice->BcdDevice & 0x0f00) == 0x0000))	//FX2LP:0xA0nn FX2:0x00nn
				|| (USBDevice->BcdDevice == 0xff00+id))
			{
				if((USBDevice->VendorID == 0x04B4) && (USBDevice->ProductID == 0x8613)) find = true;
				if((USBDevice->VendorID == 0x04B4) && (USBDevice->ProductID == 0x1004)) find = true;
			}
			if(find) break;
			USBDevice->Manufacturer[0]=0;	//前回openしたデバイスの情報が残るので消しておく
			USBDevice->Close();
		}
		if(!find) return false;

		//必要なファームがロードされているか文字列で判断
		if(manufacturer != NULL)
		{
			wchar_t wcs[64];
#if _MSC_VER >= 1400
			size_t n;
			mbstowcs_s(&n, wcs, 64, manufacturer, 64);
#else
			mbstowcs( wcs, manufacturer , strlen(manufacturer));
#endif
			if(wcscmp(USBDevice->Manufacturer, wcs) == 0)
				return true;
		}

		//IICイメージのロード＆8051リセット
		u16 len, ofs;
		long llen;	

		//8051停止
		USBDevice->ControlEndPt->ReqCode = 0xA0;
		USBDevice->ControlEndPt->Value = 0xE600;
		USBDevice->ControlEndPt->Index = 0;
		llen = 1;
		u8 tmp = 1;
		USBDevice->ControlEndPt->Write(&tmp, llen);	

		fw += 8;
		bool fw_patch = false;
		u8 *fw_patch_ptr;
		for(;;)
		{
			len = fw[0]*0x100 + fw[1];
			ofs = fw[2]*0x100 + fw[3];	

			//FWのデバイスデスクリプタ VID=04B4 PID=1004 Bcd=0000を見つけ出し、
			//idに適合するBcd(FFxx)に書き換える
			const u8 des[]={0xb4, 0x04, 0x04, 0x10, 0x00, 0x00};
			u32 i,j;
			for(i = 0; i < (len & (unsigned)0x7fff); i++)
			{
				for(j = 0; j < 6; j++) if(fw[4+i+j] != des[j]) break;
				if(j >= 6)
				{
					fw[4+i+4] = id;
					fw[4+i+5] = 0xff;
					fw_patch = true;
					fw_patch_ptr = fw+4+i+4;
					break;
				}
			}
			//ベンダリクエスト'A0'を使用して8051に書き込む
			USBDevice->ControlEndPt->Value = ofs;
			llen = len & 0x7fff;
			USBDevice->ControlEndPt->Write(fw+4, llen);	

			if(len & 0x8000)
				break;	//最終（リセット）
			fw += len+4;
		}
		if(fw_patch)
		{
			fw_patch_ptr[0]=fw_patch_ptr[1]=0;
		}
		USBDevice->Close();
		loading = true;
		if(!cons_mode) return false;
		Sleep(100);
	}

	//再起動後のファームに接続
	find = false;
	u32 i;
	for(i = 0; i < 400; i++)
	{
		for(d=0; d < USBDevice->DeviceCount(); d++)
		{
			USBDevice->Open(d);
			if((USBDevice->BcdDevice == 0xff00+id) && 
				(USBDevice->VendorID == 0x04B4) && (USBDevice->ProductID == 0x1004)) find = true;
			if(find) break;
			USBDevice->Close();
		}
		if(find || !cons_mode) break;
		Sleep(100);
	}
	if(!find) return false;
	loading = false;
	return true;
}

CCyUSBEndPoint* cusb2::get_endpoint(u8 addr)
{
	u8 ep;
	for(ep = 0; ep < USBDevice->EndPointCount(); ep++)
	{
		if(USBDevice->EndPoints[ep]->Address == addr)
		{
			USBDevice->EndPoints[ep]->Reset();
			return USBDevice->EndPoints[ep];
		}
	}
	return NULL;
}

bool cusb2::xfer(CCyUSBEndPoint *ep, PUCHAR buf, LONG &len)
{
	if(ep->GetXferSize() < (unsigned)len)
		ep->SetXferSize(len);
	return(ep->XferData(buf, len));
}

void thread_proc(cusb2_tcb *tcb)
{
	u32 i;
	u32 qnum=0;
	bool success = false;
	LONG len ;
	CCyUSBEndPoint *ep;


	ep = tcb->ep;
	tcb->nsuccess = 0;
	tcb->nfailure = 0;
	tcb->looping = true;

	OVERLAPPED *ovlp = new OVERLAPPED[tcb->ques];
	PUCHAR *data = new PUCHAR[tcb->ques];
	PUCHAR *context = new PUCHAR[tcb->ques];
	ep->SetXferSize(tcb->xfer);
	BOOL ret=SetThreadPriority(tcb->th, THREAD_PRIORITY_HIGHEST);
	//BOOL ret = SetThreadPriority(tcb->th, THREAD_PRIORITY_ABOVE_NORMAL);

	if(tcb->epaddr & 0x80)
	{	//IN(FX2->PC)転送
		for(i=0; i<tcb->ques; i++)
		{
			data[i] = new UCHAR[tcb->xfer];
			ovlp[i].hEvent = CreateEvent(NULL, false, false, NULL);
			context[i] = ep->BeginDataXfer(data[i], tcb->xfer, &(ovlp[i]));
			qnum++;
		}
		for(i=0;;)
		{
			if(!ep->WaitForXfer(&(ovlp[i]),500))
			{
				ep->Abort();
				WaitForSingleObject(&(ovlp[i]),500);
			}

//			len = tcb->xfer;
			success = ep->FinishDataXfer(data[i], len, &(ovlp[i]), context[i]);
			qnum--;

			if(success)
			{
				if(tcb->looping)
				{
					if(!tcb->cb_func(data[i], (u32)len , tcb))
					{
						tcb->looping = false;
					}
				}
				tcb->nsuccess++;
			}
			else
			{
				tcb->nfailure++;
			}

			if(tcb->looping)
			{
				context[i] = ep->BeginDataXfer(data[i], tcb->xfer, &(ovlp[i]));
				qnum++;
			}

			if(++i == tcb->ques) i = 0;
			if(tcb->looping == false)
			{
				if(qnum == 0)
					break;
			}
		}
	}
	else
	{	//OUT(PC->FX2)転送
		for(i=0; i<tcb->ques; i++)
		{
			data[i] = new UCHAR[tcb->xfer];
			ovlp[i].hEvent = CreateEvent(NULL, false, false, NULL);
			if(tcb->looping)
			{
				if(!tcb->cb_func(data[i], (u32)tcb->xfer , tcb))
				{
					tcb->looping = false;
				}
			}
			context[i] = ep->BeginDataXfer(data[i], tcb->xfer, &(ovlp[i]));
			qnum++;
		}
		for(i=0;;)
		{
			if(!ep->WaitForXfer(&(ovlp[i]),500))
			{
				ep->Abort();
				WaitForSingleObject(&(ovlp[i]),500);
			}

			len = tcb->xfer;
			success = ep->FinishDataXfer(data[i], len, &(ovlp[i]), context[i]);
			qnum--;

			if(success)
			{
				tcb->nsuccess++;
			}
			else
			{
				tcb->nfailure++;
			}

			if(tcb->looping)
			{
				if(!tcb->cb_func(data[i], (u32)tcb->xfer , tcb))
				{
					tcb->looping = false;
				}
				context[i] = ep->BeginDataXfer(data[i], tcb->xfer, &(ovlp[i]));
				qnum++;
			}

			if(++i == tcb->ques) i = 0;
			if(tcb->looping == false)
			{
				if(qnum == 0)
					break;
			}
		}
	}

	for(i=0; i<tcb->ques; i++)
	{
		CloseHandle(ovlp[i].hEvent);
		delete [] data[i];
	}
	delete [] context;
	delete [] data;
	delete [] ovlp;
}

cusb2_tcb* cusb2::prepare_thread(u8 epaddr, u32 xfer, s32 ques, bool (*cb_func)(u8 *, u32 , _cusb2_tcb*))
{
	cusb2_tcb *tcb = new cusb2_tcb;
	tcb->epaddr = epaddr;
	tcb->ep = get_endpoint(epaddr);
	tcb->epaddr = epaddr;
	tcb->xfer = xfer;
	tcb->ques = ques;
	tcb->cb_func = cb_func;
	tcb->userpointer = NULL;
	tcb->th = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_proc, tcb, CREATE_SUSPENDED, NULL);
	
	// move to start_thread
	// Sleep(100);

	return tcb;
}

void cusb2::start_thread(cusb2_tcb *tcb)
{
	ResumeThread(tcb->th);
	Sleep(100);	// maybe for waiting first receiving??
}

void cusb2::delete_thread(cusb2_tcb *tcb)
{
	WaitForSingleObject(tcb->th, INFINITE);
	delete tcb;
}

s32 cusb2::check_fx2(u8 id)
{
	u8 d;
	s32 ret = 0;
	for(d=0; d < _USBDevice->DeviceCount(); d++)
	{
		_USBDevice->Open(d);
		if(((id == 0)&&((_USBDevice->BcdDevice & 0x0f00) == 0x0000))	//FX2LP:0xA0nn FX2:0x00nn
			|| (_USBDevice->BcdDevice == 0xff00+id))
		{
			if((_USBDevice->VendorID == 0x04B4) && (_USBDevice->ProductID == 0x8613)) ret = 1;
			if((_USBDevice->VendorID == 0x04B4) && (_USBDevice->ProductID == 0x1004)) ret = 2;
		}
		_USBDevice->Close();
		if(ret) break;
	}
	return ret;
}

bool cusb2::PnpEvent(WPARAM wParam, LPARAM lParam) 
{
	s32 st;
	if (wParam == DBT_DEVICEARRIVAL) 
		if (bDevNodeChange) 
			bArrived = true;

	if (wParam == DBT_DEVICEREMOVECOMPLETE) 
	{
		if (bDevNodeChange) {
			bDevNodeChange = false;
			if(check_fx2(_id) == 0)
				return true;		//対象FX2がリムーブ
		}
	}

	if (wParam == DBT_DEVNODES_CHANGED) 
	{
		bDevNodeChange = true;

		if (bArrived) {
			bArrived = false;
			bDevNodeChange = false;
			st = check_fx2(_id);
			if(st == 1)	return true;				//FWロード要
			if((st == 2) && loading) return true;	//RENUM
		}
	}
	return false;
}
