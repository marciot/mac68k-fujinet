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

#include <Devices.h>
#include <Files.h>
#include <Serial.h>

#include "FujiNet.h"
#include "FujiInterfaces.h"

// Configuration options

#define USE_JUMBO_WRITES  1 // Allow writes of more than 512 bytes at a time
#define REPORT_EXTRA      1 // SerGetBuff reports all unfetched bytes

// Menubar "led" indicators

#define LED_START_IO   ind_solid
#define LED_FINISH_IO  ind_hollow
#define LED_BLKED_IO   ind_dot
#define LED_WRONG_TAG  ind_ring
#define LED_ERROR      ind_cross

#define SER_WRIT_INDICATOR(symb) drawIndicatorAt (496, 1, symb);
#define SER_READ_INDICATOR(symb) drawIndicatorAt (496, 9, symb);

// Driver flags and prototypes

#define DFlags dWritEnableMask | dReadEnableMask | dStatEnableMask | dCtlEnableMask | dNeedTimeMask
#define JIODone 0x08FC

OSErr doOpen    (   IOParam *, DCtlEntry *);
OSErr doPrime   (   IOParam *, DCtlEntry *);
OSErr doClose   (   IOParam *, DCtlEntry *);
OSErr doControl (CntrlParam *, DCtlEntry *);
OSErr doStatus  (CntrlParam *, DCtlEntry *);

/**
 * The "main" function must be the 1st defined in the file
 *
 * To reduce the code size, we use our own entry rather than
 * the Symantec C++ provided stub.
 *
 * To compile:
 *
 *  - From "Project" menu, select "Set Project Type..."
 *  - Set to "Code Resource"
 *  - Set file type to "rsrc" and creator code to "RSED"
 *  - Set the name to ".FujiMain"
 *  - Set the Type to 'DRVR'
 *  - Set ID to -15904
 *  - Check "Custom Header"
 *  - In "Attrs", set to "System Heap" (40)
 *
 */

void main() {
	asm {

		// Driver Header: "Inside Macintosh: Devices", p I-25

		dc.w    DFlags                             ; flags
		dc.w    60                                 ; periodic ticks
		dc.w    0x0000                             ; DA event mask
		dc.w    0x0000                             ; menuID of DA menu
		dc.w    @DOpen    +  8                     ; open offset
		dc.w    @DPrime   + 10                     ; prime offset
		dc.w    @DControl + 12                     ; control offset
		dc.w    @DStatus  + 14                     ; status offset
		dc.w    @DClose   + 16                     ; close offset
		dc.b    "\p.Fuji"                          ; driver name

		// Driver Dispatch: "Inside Macintosh: Devices", p I-29

	DOpen:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doOpen                             ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		rts

	DPrime:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doPrime                            ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		bra     @IOReturn

	DControl:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doControl                          ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		cmpi.w  #killCode,CntrlParam.csCode(a0)    ; test for KillIO call (special case)
		bne     @IOReturn
		rts                                        ; KillIO must always return via RTS

	DStatus:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doStatus                           ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr

	IOReturn:
		move.w  CntrlParam.ioTrap(a0),d1
		btst    #noQueueBit,d1                     ; immediate calls are not queued, and must RTS
		beq     @Queued                            ; branch if queued

	NotQueued:
		tst.w   d0                                 ; test asynchronous return result
		ble     @ImmedRTS                          ; result must be <= 0
		clr.w   d0                                 ; "in progress" result (> 0) not passed back

	ImmedRTS:
		move.w  d0,IOParam.ioResult(a0)            ; for immediate calls you must explicitly
												   ; place the result in the ioResult field
		rts

	Queued:
		tst.w   d0                                 ; test asynchronous return result
		ble     @MyIODone                          ; I/O is complete if result <= 0
		clr.w   d0                                 ; "in progress" result (> 0) not passed back
		rts

	MyIODone:
		move.l  JIODone,-(SP)                      ; push IODone jump vector onto stack
		rts

	DClose:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doClose                            ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		; rts                                      ; close is always immediate, must return via RTS
	}
}

#include "LedIndicators.h" // Don't put this above main as it genererates code

static short getSource (short dCtlRefNum) {
	// -6 or -7  => 1
	// -8 or -9  => 2
	// otherwise => 3
	short tmp = ((~dCtlRefNum) - 5) >> 1;
	if (tmp > 1) tmp = 3;
	return tmp;
}

static OSErr fujiReadInput (struct FujiSerData *data) {
	OSErr err;
	long indicator = LED_ERROR;

	SER_READ_INDICATOR (LED_START_IO);

	data->conn.iopb.ioBuffer = (Ptr) &data->readData;
	err = PBReadSync ((ParmBlkPtr)&data->conn.iopb);
	if (err == noErr) {
		if (data->readData.id == MAC_FUJI_REPLY_TAG) {
			data->readPos   = 0;
			data->readAvail = 0;
			data->readLeft  = data->readData.avail;

			// The Pico will always report the total available bytes, even
			// when the maximum message size is 500. Store the number of bytes
			// in the read buffer in readLeft, with the overflow in readAvail.

			if (data->readLeft > NELEMENTS(data->readData.payload)) {
				data->readAvail  = data->readLeft - NELEMENTS(data->readData.payload);
				data->readLeft   = NELEMENTS(data->readData.payload);
			}

			indicator = LED_FINISH_IO;
		} else {
			indicator = LED_WRONG_TAG;
			err = -1;
		}
	}
	SER_READ_INDICATOR (indicator);
	return err;
}

/********** Device driver routines **********/

static OSErr doControl (CntrlParam *pb, DCtlEntry *devCtlEnt) {
	struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

	//HUnlock (data->fuji);
	//HUnlock (devCtlEnt->dCtlStorage);
	HUnlock ((Handle)devCtlEnt->dCtlDriver);
	return noErr;
}

static OSErr doStatus (CntrlParam *pb, DCtlEntry *devCtlEnt) {
	struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

	if (pb->csCode == 2) {
		// SetGetBuff: Return how much data is available

		if (data->readLeft == 0) {
			fujiReadInput (data);
		}

		pb->csParam[0] = 0;                                // High order-word
	#if REPORT_EXTRA
		pb->csParam[1] = data->readLeft + data->readAvail; // Low order-word
	#else
		pb->csParam[1] = data->readLeft;
	#endif

	} else if (pb->csCode == 8) {

		// SerStatus: Obtain status information from the serial driver
		SerStaRec *status = (SerStaRec *) &pb->csParam[0];

		status->rdPend  = 0;
		status->wrPend  = 0;
		status->ctsHold = 0;
		status->cumErrs = 0;
		status->cumErrs  = 0;
		status->xOffSent = 0;
		status->xOffHold = 0;
	}
	//HUnlock (data->fuji);
	//HUnlock (devCtlEnt->dCtlStorage);
	HUnlock ((Handle)devCtlEnt->dCtlDriver);
	return noErr;
}

static OSErr doPrime (IOParam *pb, DCtlEntry *devCtlEnt) {
	OSErr err = noErr;
	struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;
	long inOutBytes = pb->ioReqCount;
	short cmd = 0x00FF & pb->ioTrap;

	if (cmd == aWrCmd) {         // Do a write operation
		#if USE_JUMBO_WRITES
			Ptr inBytesPtr = pb->ioBuffer;
			SER_WRIT_INDICATOR (LED_START_IO);
			while (inOutBytes > 0) {
				// Determine how much to write
				long bytesToWrite = inOutBytes;
				if (bytesToWrite > 512) {
					bytesToWrite = 512;
				}

				// Write a block of data
				FUJI_TAG_ID  = MAC_FUJI_REQUEST_TAG;
				FUJI_TAG_SRC = (getSource (devCtlEnt->dCtlRefNum) << 8);
				FUJI_TAG_LEN = bytesToWrite;
				data->conn.iopb.ioBuffer = inBytesPtr;
				err = PBWriteSync ((ParmBlkPtr)&data->conn.iopb);
				if (err) break;
				// Increment pointers
				inBytesPtr += bytesToWrite;
				inOutBytes -= bytesToWrite;
			}
			pb->ioActCount      = pb->ioReqCount - inOutBytes;
			data->bytesWritten += pb->ioReqCount - inOutBytes;
		#else
			SER_WRIT_INDICATOR (LED_START_IO);
			if (inOutBytes > 512) {
				inOutBytes = 512;
			}
			FUJI_TAG_ID  = MAC_FUJI_REQUEST_TAG;
			FUJI_TAG_SRC = (getSource (devCtlEnt->dCtlRefNum) << 8);
			FUJI_TAG_LEN = inOutBytes;
			data->conn.iopb.ioBuffer = pb->ioBuffer;
			err = PBWriteSync ((ParmBlkPtr)&data->conn.iopb);
			pb->ioActCount      = inOutBytes;
			data->bytesWritten += inOutBytes;
		#endif
		SER_WRIT_INDICATOR (err == noErr ? LED_FINISH_IO : LED_ERROR);
	}

	else if (cmd == aRdCmd) {    // Do a read operation
		SER_READ_INDICATOR (LED_START_IO);
		while (inOutBytes > 0) {
			long bytesToRead = inOutBytes;

			if (data->readLeft == 0) {
				err = fujiReadInput (data);
				if (err) break;
			}
			if (bytesToRead > data->readLeft) {
				bytesToRead = data->readLeft;
			}
			if (bytesToRead) {
				BlockMove (data->readData.payload + data->readPos, pb->ioBuffer, bytesToRead);
				data->readLeft   -= bytesToRead;
				data->readPos    += bytesToRead;
				inOutBytes       -= bytesToRead;
			}
		}
		pb->ioActCount    = pb->ioReqCount;
		data->bytesRead  += pb->ioReqCount;
		SER_READ_INDICATOR (err == noErr ? LED_FINISH_IO : LED_ERROR);
	}

	//HUnlock (data->fuji);
	//HUnlock (devCtlEnt->dCtlStorage);
	HUnlock ((Handle)devCtlEnt->dCtlDriver);
	return err;
}

static OSErr doOpen (IOParam *pb, DCtlEntry *devCtlEnt) {

	// Make sure the dCtlStorage was populated by the FujiNet DA

	if (devCtlEnt->dCtlStorage == 0L) {
		return openErr;
	} else {

		// Make sure the port is configured correctly

		struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;
		if (data->conn.iopb.ioRefNum == 0L) {
			return portNotCf;
		}
	}
	return noErr;
}

static OSErr doClose (IOParam *pb, DCtlEntry *devCtlEnt) {
	return noErr;
}