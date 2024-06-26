/****************************************************************************
 *   mac68k-fuji-drivers (c) 2024 Marcio Teixeira                           *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/

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
	iMacTCPBtn    = 3,
	iStatus       = 4,
	iBytesRead    = 5,
	iBytesWritten = 6
};

// Function Prototypes

static short getOwnedResId (DCtlPtr devCtlEnt, short subId) {
	const short unitNumber = -(devCtlEnt->dCtlRefNum + 1);
	return 0xC000 | (unitNumber << 5) | subId;
}

static void setButtonState (DCtlPtr devCtlEnt, short id, Boolean alreadyInstalled) {
	short         type;
	Rect          rect;
	ControlHandle hCntl;

	GetDItem (devCtlEnt->dCtlWindow, id, &type, (Handle*) &hCntl, &rect);
	SetControlValue (hCntl, alreadyInstalled);
	HiliteControl (hCntl, alreadyInstalled ? 255 : 0);
}

static void updateButtonState (DCtlPtr devCtlEnt) {
	setButtonState (devCtlEnt, iModemBtn,   isFujiModemRedirected());
	setButtonState (devCtlEnt, iPrinterBtn, isFujiPrinterRedirected());
	setButtonState (devCtlEnt, iMacTCPBtn,  isFujiMacTCPRedirected());
}

static void doEvent (EventRecord *event, DCtlPtr devCtlEnt) {
	DialogPtr dlgHit;   /* dialog for which event was generated */
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
			case iMacTCPBtn:
				if (fujiSerialRedirectMacTCP() != noErr) {
					SysBeep(10);
				}
				updateButtonState(devCtlEnt);
				break;
		}
	}
}

static void doRun (DialogPtr dlg, DCtlPtr devCtlEnt) {
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

static OSErr doOpen (IOParam *pb, DCtlPtr devCtlEnt ) {
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

static OSErr doPrime (IOParam *pb, DCtlPtr devCtlEnt) {
	return noErr;
}

static OSErr doControl (CntrlParam *pb, DCtlPtr devCtlEnt) {
	switch (pb->csCode) {
		case accEvent: doEvent (*(EventRecord**)pb->csParam, devCtlEnt); break;
		case accRun:   doRun   (devCtlEnt->dCtlWindow,       devCtlEnt); break;
	}
	return noErr;
}

static OSErr doStatus (CntrlParam *pb, DCtlPtr devCtlEnt) {
	return noErr;
}

static OSErr doClose (IOParam *pb, DCtlPtr devCtlEnt) {

	if (devCtlEnt->dCtlWindow) {
		DisposeDialog (devCtlEnt->dCtlWindow);
		devCtlEnt->dCtlWindow = 0;
	}

	return noErr;
}

OSErr main (ParamBlockRec* pb, DCtlPtr devCtlEnt, int n) {
	//devCtlEnt->dCtlFlags &= ~dCtlEnable;      /* we are not re-entrant */
	switch (n) {
		case kOpen:    return doOpen(    &pb->ioParam,    devCtlEnt );
		case kPrime:   return doPrime(   &pb->ioParam,    devCtlEnt );
		case kControl: return doControl( &pb->cntrlParam, devCtlEnt );
		case kStatus:  return doStatus(  &pb->cntrlParam, devCtlEnt );
		case kClose:   return doClose(   &pb->ioParam,    devCtlEnt );
	}
	//devCtlEnt->dCtlFlags |= dCtlEnable;   /* enable control calls once more */
	return 0;
}