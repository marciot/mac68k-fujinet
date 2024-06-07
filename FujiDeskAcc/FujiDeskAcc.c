#include <Dialogs.h>
#include <Devices.h>

#include "FujiNet.h"

// Control manager
enum {
	kOpen    = 0,
	kPrime   = 1,
	kControl = 2,
	kStatus  = 3,
	kClose   = 4
};

// Dialog items
enum {
	iModemBtn     = 1,
	iPrinterBtn   = 2,
	iStatus       = 3,
	iBytesRead    = 4,
	iBytesWritten = 5
};

// Function Prototypes

OSErr doOpen   ( IOParam    *paramBlock, DCtlPtr devCtlEnt );
OSErr doPrime  ( IOParam    *paramBlock, DCtlPtr devCtlEnt );
OSErr doClose  ( IOParam    *paramBlock, DCtlPtr devCtlEnt );
OSErr doControl( CntrlParam *paramBlock, DCtlPtr devCtlEnt );
OSErr doStatus ( CntrlParam *paramBlock, DCtlPtr devCtlEnt );

void doEvent(EventRecord *event, DCtlPtr devCtlEnt);
void doRun  (DialogPtr dlg,      DCtlPtr devCtlEnt);

OSErr doFujiStartup();

void updateButtonState(DCtlPtr devCtlEnt);

OSErr main (ParamBlockRec* pb, DCtlPtr devCtlEnt, int n) {
    //devCtlEnt->dCtlFlags &= ~dCtlEnable;		/* we are not re-entrant */
	switch (n) {
		case kOpen:    return doOpen(    &pb->ioParam,    devCtlEnt );
		case kPrime:   return doPrime(   &pb->ioParam,    devCtlEnt );
		case kControl: return doControl( &pb->cntrlParam, devCtlEnt );
		case kStatus:  return doStatus(  &pb->cntrlParam, devCtlEnt );
		case kClose:   return doClose(   &pb->ioParam,    devCtlEnt );
	}
	//devCtlEnt->dCtlFlags |= dCtlEnable;	/* enable control calls once more */
	return 0;
}

static short getOwnedResId (DCtlPtr devCtlEnt, short subId) {
	const short unitNumber = -(devCtlEnt->dCtlRefNum + 1);
	return 0xC000 | (unitNumber << 5) | subId;
}

OSErr doOpen (IOParam *pb, DCtlPtr devCtlEnt ) {
	const short BootDrive = *((short *)0x210); // BootDrive low-memory global

	// Make sure the glue routine was able to allocate our globals

	if (devCtlEnt->dCtlStorage == NULL) {
		goto error;
	}

	devCtlEnt->dCtlFlags |= dNeedTimeMask;
	devCtlEnt->dCtlEMask = keyDownMask | autoKeyMask | mDownMask | updateMask | activMask;
	devCtlEnt->dCtlDelay  = 60;
	devCtlEnt->dCtlMenu   = 0;

	// Open might be called multiple times, so only create
	// our window if it has not been created before

	if (devCtlEnt->dCtlWindow == NULL) {
		devCtlEnt->dCtlWindow = GetNewDialog (getOwnedResId(devCtlEnt, 0), NULL, (WindowPtr) -1);
		if (devCtlEnt->dCtlWindow == NULL) {
			goto error;
		}
		((WindowPeek)devCtlEnt->dCtlWindow)->windowKind = devCtlEnt->dCtlRefNum;
	}

	fujiSerialOpen (BootDrive);

	updateButtonState(devCtlEnt);

	return noErr;

error:
	SysBeep (10);
	CloseDriver (devCtlEnt->dCtlRefNum);
	return openErr;
}

OSErr doPrime (IOParam *pb, DCtlPtr devCtlEnt) {
    return noErr;
}

OSErr doControl (CntrlParam *pb, DCtlPtr devCtlEnt) {
	switch (pb->csCode) {
	    case accEvent: doEvent (*(EventRecord**)pb->csParam, devCtlEnt); break;
		case accRun:   doRun   (devCtlEnt->dCtlWindow,       devCtlEnt); break;
	}
	return noErr;
}

OSErr doStatus (CntrlParam *pb, DCtlPtr devCtlEnt) {
	return noErr;
}

OSErr doClose (IOParam *pb, DCtlPtr devCtlEnt) {

	if (devCtlEnt->dCtlWindow) {
		DisposeDialog (devCtlEnt->dCtlWindow);
		devCtlEnt->dCtlWindow = 0;
	}

	return noErr;
}

void doEvent (EventRecord *event, DCtlPtr devCtlEnt) {
	DialogPtr dlgHit;	/* dialog for which event was generated */
	short     itemHit;  /* item selected from dialog */
	Boolean   isOurs = DialogSelect(event, &dlgHit, &itemHit);

	if (isOurs && (dlgHit == devCtlEnt->dCtlWindow)) {
		short         type;
		Rect          rect;
		ControlHandle hCntl;

		GetDItem (dlgHit, itemHit, &type, (Handle*) &hCntl, &rect);
		if (type == ctrlItem + chkCtrl) {
			SetControlValue (hCntl, 1 - GetControlValue(hCntl));
		}
		switch (itemHit) {
			case iModemBtn:
				if (fujiSerialRedirectModem() != noErr) {
					SysBeep(10);
				}
				updateButtonState(devCtlEnt);
				break;
			case iPrinterBtn:
				if (fujiSerialRedirectPrinter() != noErr) {
					SysBeep(10);
				}
				updateButtonState(devCtlEnt);
				break;
		}
	}
}

void doRun (DialogPtr dlg, DCtlPtr devCtlEnt) {
	unsigned long bytesRead, bytesWritten;
	Str63 pStr1, pStr2, pStr3;

	GrafPtr SavedPort;
	GetPort(&SavedPort);
	SetPort(dlg);
	if (isFujiConnected()) {
		BlockMove("\pConnected", pStr1, 10);
	} else {
		BlockMove("\pNot found", pStr1, 10);
	}
	if (fujiSerialStats (&bytesRead, &bytesWritten)) {
	    NumToString (bytesRead,    pStr2);
    	NumToString (bytesWritten, pStr3);
		ParamText(pStr1, pStr2, pStr3, "\p");
	} else {
		ParamText(pStr1, "\p-", "\p-", "\p");
	}
	DrawDialog(devCtlEnt->dCtlWindow);
	SetPort(SavedPort);
}

void updateButtonState(DCtlPtr devCtlEnt) {
	short         type;
	Rect          rect;
	ControlHandle hCntl;

	if (isFujiModemRedirected()) {
		GetDItem (devCtlEnt->dCtlWindow, iModemBtn, &type, (Handle*) &hCntl, &rect);
		SetControlValue (hCntl, 1);
		HiliteControl (hCntl, 255);
	}

	if (isFujiPrinterRedirected()) {
		GetDItem (devCtlEnt->dCtlWindow, iPrinterBtn, &type, (Handle*) &hCntl, &rect);
		SetControlValue (hCntl, 1);
		HiliteControl (hCntl, 255);
	}
}