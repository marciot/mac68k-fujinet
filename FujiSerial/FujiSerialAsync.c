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

#include <stddef.h>

#include <Devices.h>
#include <Files.h>
#include <Serial.h>
#include <Retrace.h>

#include "FujiNet.h"
#include "FujiInterfaces.h"

// Configuration options

#define REPORT_EXTRA      1 // SerGetBuff reports all unfetched bytes
#define SANITY_CHECK      1 // Do additional error checking
#define USE_VBL_INDICATOR 0 // Extra "led" indicator shows when VBL task is blocking

#define VBL_TICKS 30

// Menubar "led" indicators

#include "LedIndicators.h"

#define LED_IDLE       ind_hollow
#define LED_ASYNC_IO   ind_solid
#define LED_BLKED_IO   ind_dot
#define LED_WRONG_TAG  ind_ring
#define LED_ERROR      ind_cross

#define VBL_WRIT_INDICATOR(symb) drawIndicatorAt (496, 1, symb);
#define VBL_READ_INDICATOR(symb) drawIndicatorAt (496, 9, symb);
#define VBL_TASK_INDICATOR(symb) drawIndicatorAt (488, 1, symb);

// Driver flags and prototypes

#define DFlags dWritEnableMask | dReadEnableMask | dStatEnableMask | dCtlEnableMask | dNeedLockMask
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
        clr.l   IOParam.ioActCount(a0)             ; clear the bytes processed value
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
        ;rts                                       ; close is always immediate, must return via RTS
    }
}

/********** Completion and VBL Routines **********/

static void fujiStartVBL (DCtlEntry *devCtlEnt);

static VBLTask   *getVBLTask (void);
static DCtlEntry *getMainDCE (void);

static void schedVBLTask (void); // Schedule the VBL task to run ASAP

// Completion routines

static void complFlushOut (void);  // calls fujiFlushDone
static void complReadIn (void);    // calls fujiReadDone

static void fujiFlushDone (IOParam *pb);
static void fujiReadDone  (IOParam *pb);
static void fujiVBLTask   (VBLTask *vbl);

// When I/O is done, dispatch to JIODone

static void ioIsComplete (DCtlEntry *devCtlEnt, OSErr result);

// Mutexes

static Boolean    takeVblMutex (void);
static void       releaseVblMutex (void);

static Boolean    takeWakeMutex (void);
static void       releaseWakeMutex (void);

static void _vblRoutines (void) {
    asm {
        // Use extern entry point to keep Symantec C++ from adding a stack frame.

        extern fujiStartVBL:
            lea      @dcePtr, a0
            tst.l    (a0)                              ; already installed?
            bne      @skipInstall
            lea      @dcePtr, a0
            move.l   4(sp), (a0)+                      ; set dcePtr to devCtlPtr
            lea      @callFujiVBL,a1                   ; address to entry
            move.l   a1, VBLTask.vblAddr(a0)           ; update task address
            move.w   #VBL_TICKS,VBLTask.vblCount(a0)   ; reset vblCount
            _VInstall
        skipInstall:
            rts

        extern getVBLTask:
            lea      @vblTask, a0
            move.l   a0, d0
            rts

        extern schedVBLTask:
            lea      @vblTask, a0
            move.w   #1,VBLTask.vblCount(a0)           ; reset vblCount
            rts

        extern getMainDCE:
            move.l (@dcePtr), d0                       ; load DCE ptr
            rts

        dcePtr:
            dc.l     0  // Placeholder for dcePtr (4 bytes)

        vblTask:
            // VBLTask Structure (14 bytes)
            dc.l     0                                 ; qLink
            dc.w vType                                 ; qType
            dc.l     0                                 ; vblAddr
            dc.w     0                                 ; vblCount
            dc.w     0                                 ; vblPhase

        mutexFlags:
            dc.w     0

            // VBL Requirements: One entry, a0 will point to the VBLTask;
            // this routine must preserve registers other than a0-a3/d0-d3

            // ioCompletion Requirements: One entry, a0 will point to
            // parameter block and d0 contain the result; this routine
            // must preserve registers other than a0-a1/d0-d2

        extern complFlushOut:
            lea     fujiFlushDone,a1                   ; address of C function
            bra.s   @callRoutineC

        extern complReadIn:
            lea     fujiReadDone,a1                    ; address of C function
            bra.s   @callRoutineC

        callFujiVBL:
            lea     fujiVBLTask,a1                     ; address of C function
            ;bra.s   @callRoutineC

            // callRoutineC saves registers and passes control to the C
            // function whose address is a1 with a0 as the 1st argument
        callRoutineC:
            movem.l a2-a7/d3-d7,-(sp)                  ; save registers
            move.l a0,-(sp)                            ; push a0 for C
            jsr     (a1)                               ; call C function
            addq    #4,sp                              ; clean up the stack
            movem.l (sp)+,a2-a7/d3-d7                  ; restore registers
            rts

        extern ioIsComplete:
            move.w 8(sp),d0                            ; load result code into d0
            move.l 4(sp),a1                            ; load DCtlPtr into a1
            move.l  JIODone,-(sp)                      ; push IODone jump vector onto stack
            rts

        extern takeVblMutex:
            moveq #0, d0
            bra.s @takeMutex

        extern takeWakeMutex:
            moveq #1, d0
            ;bra.s @takeMutex

        takeMutex:
            lea @mutexFlags, a0
            bset d0, (a0)
            seq d0
            rts

        extern releaseVblMutex:
            moveq #0, d0
            bra.s @releaseMutex

        extern releaseWakeMutex:
            moveq #1, d0
            ;bra.s @releaseMutex

        releaseMutex:
            lea @mutexFlags, a0
            bclr d0, (a0)
            ;rts
    }
}

/* Given a driver unit number, checks the unit to table to determine whether it is
 * a FujiNet driver and the inspects the I/O queue to see whether the driver has
 * incomplete I/O. If so, it calls the prime routine to complete the request and
 * then calls JIODone to inform the Device Manager the request is finished.
 */

static void wakeUpDriver (short unitNum) {
    Handle *table = (Handle*) UTableBase;
    if (table [unitNum]) {
        DCtlEntry *dce = (DCtlEntry*) *table[unitNum];

        #define CANDIDATE_DRIVER_FLAGS dRAMBasedMask | dOpenedMask | drvrActiveMask

        if ((dce->dCtlFlags & CANDIDATE_DRIVER_FLAGS) == CANDIDATE_DRIVER_FLAGS) {
            if ((dce->dCtlStorage != NULL) && ((*(FujiSerDataHndl)dce->dCtlStorage)->id == 'FUJI')) {
                IOParam *pb = (IOParam *) dce->dCtlQHdr.qHead;

                // The following code is problematic, as there is a likelihood
                // of the VBL interrupting the Device Manager while it is
                // inserting an I/O request into the queue. The following checks
                // seem to prevent a crash, but it is advisable to find a
                // better way (maybe saving the dce and pb pointers for incomplete
                // calls).

                if (pb && (pb->ioResult == ioInProgress)) {
                    OSErr err = doPrime (pb, dce);
                    if (err != ioInProgress) {
                        ioIsComplete (dce, err);
                    }
                }
            }
        }
    }
}

/* Wakes up all "FujiNet" drivers to give them a chance to complete queued I/O */

static void wakeUpDrivers (void) {
    if (takeWakeMutex()) {
        releaseVblMutex();
        wakeUpDriver (5); // Serial port A input
        wakeUpDriver (6); // Serial port A output
        releaseWakeMutex();
    } else {
        releaseVblMutex();
    }
}

/* Main VBL Task for the FujiNet serial driver. This task must run periodically
 * to:
 *
 *   1) check for outgoing data that needs to be written to the FujiNet device
 *   2) poll for incoming data once the read buffer is exhausted
 *   3) wake up FujiNet drivers to process queued I/O
 */

static void fujiVBLTask (VBLTask *vbl) {
    const DCtlEntry *devCtlEnt = getMainDCE();
    struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;
    long rdIndicator = -1, wrIndicator = -1;

#if USE_VBL_INDICATOR
    long tkIndicator = LED_IDLE;
#endif

    vbl->vblCount    = data->vblCount;

    if (takeVblMutex()) {
        if (data->conn.iopb.ioResult == noErr) {
            if ((data->writePos > 0) && (!data->scheduleDriverWake)) {

                /* Figure out the source value:
                 * -6 or -7  => 1
                 * -8 or -9  => 2
                 * otherwise => 3
                 */
                //short src = ((~devCtlEnt->dCtlRefNum) - 5) >> 1;
                //if (src > 1) src = 3;

                // Write out data that has been pending for a while
                data->conn.iopb.ioBuffer     = (Ptr) &data->writeData;
                data->conn.iopb.ioCompletion = (IOCompletionUPP)complFlushOut;

                data->writeData.id           = MAC_FUJI_REQUEST_TAG;
                data->writeData.src          = 0;
                data->writeData.dst          = 0;
                data->writeData.reserved     = 0;
                data->writeData.length       = data->writePos;

                wrIndicator = LED_ASYNC_IO;
                PBWriteAsync ((ParmBlkPtr)&data->conn.iopb);
            }

            else if ((data->readLeft == 0) && (!data->scheduleDriverWake)) {
                // Poll for new data

                data->conn.iopb.ioBuffer     = (Ptr) &data->readData;
                data->conn.iopb.ioCompletion = (IOCompletionUPP)complReadIn;

                wrIndicator = LED_IDLE;
                rdIndicator = LED_ASYNC_IO;
                PBReadAsync((ParmBlkPtr)&data->conn.iopb);
            }

            else {
                // Unblock drivers
                wakeUpDrivers();
                data->scheduleDriverWake = false;
            }
        } // data->conn.iopb.ioResult == noErr
        else {
#if USE_VBL_INDICATOR
            tkIndicator   = LED_ERROR;
#endif
            // On error, keep waking the drivers so they can
            // report the error but also slow the VBL Task
            wakeUpDrivers();
        }
    } // takeVblMutex

#if USE_VBL_INDICATOR
    else {
        tkIndicator = LED_BLKED_IO;
    }
    VBL_TASK_INDICATOR (tkIndicator);
#endif

    if (rdIndicator >= 0) VBL_READ_INDICATOR (rdIndicator);
    if (wrIndicator >= 0) VBL_WRIT_INDICATOR (wrIndicator);
}

/* Called after an asynchronous write to the FujiNet device has completed */

static void fujiFlushDone (IOParam *pb) {
    long wrIndicator = LED_ERROR;
    if (pb->ioResult == noErr) {

        const DCtlEntry *devCtlEnt = getMainDCE();
        struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

        data->writePos       = 0;
        wrIndicator          = LED_IDLE;

        // After writing data, immediately do a read if the buffer is empty

        if (data->readLeft == 0) {
            pb->ioBuffer     = (Ptr) &data->readData;
            pb->ioCompletion = (IOCompletionUPP)complReadIn;

            VBL_READ_INDICATOR (LED_ASYNC_IO);
            PBReadAsync((ParmBlkPtr)pb);
            goto asyncExit;
        }
    }
    releaseVblMutex();
    schedVBLTask();
asyncExit:
    VBL_WRIT_INDICATOR (wrIndicator);
}

/* Called after an asynchronous read from the FujiNet device has completed */

static void fujiReadDone (IOParam *pb) {
    long indicator = LED_ERROR;
    if (pb->ioResult == noErr) {

        const DCtlEntry *devCtlEnt = getMainDCE();
        struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

        if (data->readData.id == MAC_FUJI_REPLY_TAG) {
            data->readPos        = 0;
            data->readAvail      = 0;
            data->readLeft       = data->readData.avail;

            // The Pico will always report the total available bytes, even
            // when the maximum message size is 500. Store the number of bytes
            // in the read buffer in readLeft, with the overflow in readAvail.

            if (data->readLeft > NELEMENTS(data->readData.payload)) {
                data->readAvail  = data->readLeft - NELEMENTS(data->readData.payload);
                data->readLeft   = NELEMENTS(data->readData.payload);
            }

            data->scheduleDriverWake = true;
            indicator = LED_IDLE;
        }
        else {
            indicator = LED_WRONG_TAG;
            pb->ioResult = -1;
        }
    }
    releaseVblMutex();
    schedVBLTask();
    VBL_READ_INDICATOR (indicator);
}

/********** Device driver routines **********/

static OSErr doControl (CntrlParam *pb, DCtlEntry *devCtlEnt) {
    //struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

    //HUnlock (data->fuji);
    //HUnlock (devCtlEnt->dCtlStorage);
    //HUnlock ((Handle)devCtlEnt->dCtlDriver);
    return noErr;
}

static OSErr doStatus (CntrlParam *pb, DCtlEntry *devCtlEnt) {
    struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

    if (pb->csCode == 2) {

        // SetGetBuff: Return how much data is available

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
    //HUnlock ((Handle)devCtlEnt->dCtlDriver);
    return noErr;
}

static OSErr doPrime (IOParam *pb, DCtlEntry *devCtlEnt) {
    OSErr err = ioInProgress;

    if (takeVblMutex()) {
        struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;
        const short cmd = pb->ioTrap & 0x00FF;
        long bytesToProcess = pb->ioReqCount - pb->ioActCount;

        #if SANITY_CHECK
            if (pb->ioReqCount < 0) {
                SysBeep(10);
                bytesToProcess = 0;
            }

            if (pb->ioActCount < 0) {
                SysBeep(10);
                bytesToProcess = 0;
            }

            if (bytesToProcess < 0) {
                SysBeep(10);
                bytesToProcess = 0;
            }
        #endif

        if (cmd == aWrCmd) {
            const long writeLeft = NELEMENTS(data->writeData.payload) - data->writePos;
            if (bytesToProcess > writeLeft) {
                bytesToProcess = writeLeft;
            }
            if (bytesToProcess > 0) {
                BlockMove (
                    pb->ioBuffer + pb->ioActCount,
                    data->writeData.payload + data->writePos,
                    bytesToProcess
                );
                data->writePos     += bytesToProcess;
                data->bytesWritten += bytesToProcess;
            }
        } // aWrCmd

        else if (cmd == aRdCmd) {
            if (bytesToProcess > data->readLeft) {
                bytesToProcess = data->readLeft;
            }
            if (bytesToProcess > 0) {
                BlockMove (
                    data->readData.payload + data->readPos,
                    pb->ioBuffer + pb->ioActCount,
                    bytesToProcess
                );
                data->readPos   += bytesToProcess;
                data->readLeft  -= bytesToProcess;
                data->bytesRead += bytesToProcess;
            }
        } // aRdCmd

        else {
            // Unknown command
            goto error;
        }

        pb->ioActCount += bytesToProcess;

        if (data->conn.iopb.ioResult != noErr) {
            err = data->conn.iopb.ioResult;
        } else if (pb->ioActCount == pb->ioReqCount) {
            err = noErr;
        } else {
            // We are blocked because the buffers are either
            // full or empty, schedule the VBL task ASAP to
            // remedy this.
            schedVBLTask();
        }

    error:
        pb->ioResult = err;
        releaseVblMutex();
    } // takeVblMutex

    return err;
}

static OSErr doOpen (IOParam *pb, DCtlEntry *dce) {
    struct FujiSerData *data;

    // Make sure the dCtlStorage was populated by the FujiNet DA

    if (dce->dCtlStorage == 0L) {
        return openErr;
    }

    HLock (dce->dCtlStorage);

    // Make sure the port is configured correctly

    data = *(FujiSerDataHndl)dce->dCtlStorage;
    if (data->conn.iopb.ioRefNum == 0L) {
        return portNotCf;
    }

    // Figure out which driver we are opening
    //if (data->mainDrvrRefNum == dce->dCtlRefNum) {
    //  dce->dCtlFlags |= dNeedLockMask;
    //}

    // Start the VBL task
    data->conn.iopb.ioResult = noErr;

    if (data->vblCount == 0) {
        data->vblCount = VBL_TICKS;
    }

    fujiStartVBL (dce);

    return noErr;
}

static OSErr doClose (IOParam *pb, DCtlEntry *devCtlEnt) {
    return noErr;
}