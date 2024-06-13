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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <Files.h>
#include <Disks.h>
#include <Devices.h>
#include <Resources.h>
#include <Serial.h>
#include <Retrace.h>

#define DEBUG                1
#define BENCH_CHECK_MESSAGES 1
#define BENCH_SHOW_OPERATION 0

#include "FujiNet.h"
#include "FujiDebugMacros.h"
#include "FujiInterfaces.h"

#include "FujiTests.h"

char *errorStr(OSErr err);

short chosenDriveNum;
short chosenDrvrRefNum;

static void printHexDump (const unsigned char *ptr, unsigned short len) {
    short i, n = MIN(15, len);
    printf("'");
    for (i = 0; i < n; i++) printf("%c"  , isprint (ptr[i]) ? ptr[i] : '.');
    printf("' ");
    for (i = 0; i < n; i++) printf("%02x ", ptr[i] & 0xFF);
    printf("\n");
}

static OSErr printDriveVolumes(int driveNum) {
    VCB *qe;
    const QHdrPtr qh = GetVCBQHdr();
    for(qe = (VCB*) qh->qHead; qe; qe = (VCB*) qe->qLink) {
        if (driveNum == qe->vcbDrvNum)
        printf(" %#27.27s ", qe->vcbVN);
    }
    return noErr;
}

static OSErr printDriveQueue() {
    DrvQElPtr qe;
    const QHdrPtr qh = GetDrvQHdr();
    for(qe = (DrvQElPtr) qh->qHead; qe; qe = (DrvQElPtr) qe->qLink) {
        size_t size = (size_t)(qe->dQDrvSz) | ((qe->qType == 1) ? (size_t)(qe->dQDrvSz2) << 16 : 0);
        printf("\n%4d: [%7.2f MBs]  ", qe->dQDrive, (float)(size)/2/1024);
        printDriveVolumes(qe->dQDrive);
    }
    printf("\n");
    return noErr;
}

static OSErr findDrive(short drive) {
    OSErr err;
    DrvQElPtr qe;
    const QHdrPtr qh = GetDrvQHdr();
    for(qe = (DrvQElPtr) qh->qHead; qe; qe = (DrvQElPtr) qe->qLink) {
        if (qe->dQDrive == drive) {
            chosenDriveNum   = qe->dQDrive;
            chosenDrvrRefNum = qe->dQRefNum;
            return noErr;
        }
    }
    err = -1;
    printf("Can't find drive\n");
    return err;
}

static OSErr chooseDrive() {
    short drive;
    printf("Please select drive: ");
    scanf("%d", &drive);
    findDrive(drive);
}

static OSErr openFujiNet() {
    const short BootDrive = *((short *)0x210); // BootDrive low-memory global
    OSErr err = fujiSerialOpen (BootDrive); CHECK_ERR;
    return err;
}

static OSErr printUnitTable() {
    short i, lines = 0;
    Handle *table = (Handle*) UTableBase;
    DCtlEntry *dce;
    for(i = 0; i < UnitNtryCnt; i++) {
        if (table[i]) {
            unsigned long drvrSize = 0, dataSize = 0;
            char drvrZone = '-', dataZone = '-';
            Boolean dRamBased;
            DRVRHeader *header;
            //char dceState = (HGetState (table[i]) & 0x80) ? 'L' : 'u';
            char dceState = ' ';

            dce = (DCtlEntry*) *table[i];

            dRamBased    = dce->dCtlFlags & dRAMBasedMask;
            header       = (DRVRHeader*) (dRamBased ? *(Handle)dce->dCtlDriver : dce->dCtlDriver);

            if (dRamBased) {
                Handle drvrHand = (Handle)dce->dCtlDriver;
                drvrSize = GetHandleSize(drvrHand);
                drvrZone = (HandleZone(drvrHand) == SystemZone()) ? 's' : 'a';
                if (dce->dCtlStorage) {
                    Handle dataHand = (Handle)dce->dCtlStorage;
                    dataSize = GetHandleSize(dataHand);
                    dataZone = (HandleZone(dataHand) == SystemZone()) ? 's' : 'a';
                }
            }

            printf("\n%4d: %3d %#10.10s %c%s %s %s %c%c%c%c%c%c %c%c%c%c%c%c %3ld %3ld %c%c", i, dce->dCtlRefNum, header->drvrName,
                dceState,
                (dce->dCtlFlags & dRAMBasedMask)   ? "    RAM" : "    ROM",
                (dce->dCtlFlags & dOpenedMask)     ? "    open" : "  closed",
                (dce->dCtlFlags & drvrActiveMask)  ? "  active" : "inactive",

                (dce->dCtlFlags & dNeedLockMask)   ? 'L' : '-',
                (dce->dCtlFlags & dNeedTimeMask)   ? 'T' : '-',
                (dce->dCtlFlags & dStatEnableMask) ? 'S' : '-',
                (dce->dCtlFlags & dCtlEnableMask)  ? 'C' : '-',
                (dce->dCtlFlags & dWritEnableMask) ? 'W' : '-',
                (dce->dCtlFlags & dReadEnableMask) ? 'R' : '-',

                (header->drvrFlags & dNeedLockMask)    ? 'L' : '-',
                (header->drvrFlags & dNeedTimeMask)    ? 'T' : '-',
                (header->drvrFlags & dStatEnableMask)  ? 'S' : '-',
                (header->drvrFlags & dCtlEnableMask)   ? 'C' : '-',
                (header->drvrFlags & dWritEnableMask)  ? 'W' : '-',
                (header->drvrFlags & dReadEnableMask)  ? 'R' : '-',
                drvrSize, dataSize, drvrZone, dataZone
            );
            if (lines++ % 22 == 0) {
                printf("\n\n==== MORE ===="); getchar();
            }
        }
    }
}

static OSErr printDriverStatus() {
    unsigned long bytesRead, bytesWritten;

    printf("\n");
    printf("Fuji status:          %s\n", isFujiConnected()       ? "connected" : "not connected");
    printf("Modem driver:         %s\n", isFujiModemRedirected() ? "installed" : "not installed");
    printf("Printer driver:       %s\n\n", isFujiPrinterRedirected() ? "installed" : "not installed");

    if (fujiSerialStats (&bytesRead, &bytesWritten)) {
        FujiSerDataHndl data = getFujiSerialDataHndl ();
        if (data) {
            printf("Internal bytes avail: %d\n", (*data)->readAvail);
            printf("Driver ref number     %d\n", (*data)->conn.iopb.ioRefNum);
            printf("Drive number:         %d\n", (*data)->conn.iopb.ioVRefNum);
            printf("Magic sector:         %ld\n", (*data)->conn.iopb.ioPosOffset / 512);
        }

        printf("Total bytes read:     %ld\n", bytesRead);
        printf("Total bytes written:  %ld\n", bytesWritten);
    } else {
        printf("Cannot get status\n");
    }
}

static OSErr setVBLFrequency() {
    if (isFujiModemRedirected()) {
        OSErr err;
        short sInputRefNum, sOutputRefNum;
        FujiSerDataHndl data = getFujiSerialDataHndl ();

        err = OpenDriver("\p.AOut",  &sOutputRefNum); CHECK_ERR;
        err = OpenDriver("\p.AIn",   &sInputRefNum); CHECK_ERR;

        if (data) {
            short count;
            printf("Current VBL interval: %d\n", (*data)->vblCount);
            printf("Please enter new VBL interval (1-255): ");
            scanf("%d", &count);
            (*data)->vblCount = count;
        }

        CloseDriver(sInputRefNum);
        CloseDriver(sOutputRefNum);
    } else {
        printf("Please connect to the FujiNet and redirect the serial port first\n");
    }
}


static OSErr testSerialDriver() {
    const int kInputBufSIze = 1024;
    SerShk mySerShkRec;
    short sInputRefNum, sOutputRefNum;
    long readCount;
    ParamBlockRec pb;
    Str255 myBuffer;
    Handle gInputBufHandle;
    OSErr err;
    unsigned char *msg = "\pThe Eagle has landed";

    // Open the serial drivers

    DEBUG_STAGE("Opening serial driver");

    err = OpenDriver("\p.AOut",  &sOutputRefNum); CHECK_ERR;
    err = OpenDriver("\p.AIn",   &sInputRefNum); CHECK_ERR;

    // Replace the default input buffer

    DEBUG_STAGE("Setting the buffer");

    gInputBufHandle = NewHandle(kInputBufSIze);
    HLock(gInputBufHandle);
    SerSetBuf(sInputRefNum, *gInputBufHandle, kInputBufSIze); CHECK_ERR;

    // Set the handshaking options

    DEBUG_STAGE("Setting the handshaking");

    mySerShkRec.fXOn = 0;
    mySerShkRec.fCTS = 0;
    mySerShkRec.errs = 0;
    mySerShkRec.evts = 0;
    mySerShkRec.fInX = 0;
    mySerShkRec.fDTR = 0;
    err = Control(sOutputRefNum, 14, &mySerShkRec);

    // Configure the port

    DEBUG_STAGE("Configuring the baud");

    err = SerReset(sOutputRefNum, baud2400 + data8 + noParity + stop10);

    // Send a message

    DEBUG_STAGE("Sending a message");

    pb.ioParam.ioRefNum = sOutputRefNum;
    pb.ioParam.ioBuffer  = (Ptr) &msg[1];
    pb.ioParam.ioReqCount = msg[0];
    pb.ioParam.ioCompletion = 0;
    pb.ioParam.ioVRefNum = 0;
    pb.ioParam.ioPosMode = 0;
    err = PBWrite(&pb, false); CHECK_ERR;

    // Receive a message

    DEBUG_STAGE("Checking bytes available");

    err = SerGetBuf(sInputRefNum, &readCount);

    printf("Bytes avail %ld\n", readCount);

    if (readCount > 0) {

        DEBUG_STAGE("Reading bytes");

        myBuffer[0] = readCount;

        // Read a message
        pb.ioParam.ioRefNum = sInputRefNum;
        pb.ioParam.ioBuffer  = (Ptr) &myBuffer[1];
        pb.ioParam.ioReqCount = readCount;
        pb.ioParam.ioCompletion = 0;
        pb.ioParam.ioVRefNum = 0;
        pb.ioParam.ioPosMode = 0;
        err = PBRead(&pb, false); CHECK_ERR;

        printf("%#s\n", myBuffer);
    }

    DEBUG_STAGE("Restoring buffer");

    SerSetBuf(sInputRefNum, *gInputBufHandle, 0); CHECK_ERR;
    DisposeHandle(gInputBufHandle);

    // Close Serial port
    DEBUG_STAGE("Killing IO");

    KillIO(sOutputRefNum);

    DEBUG_STAGE("Closing driver");

    CloseDriver(sInputRefNum);
    CloseDriver(sOutputRefNum);
}

static OSErr readSectorAndTags() {
    ParamBlockRec pb;
    OSErr         err;
    TagBuffer     tag;
    SectorBuffer  sector;
    int           i;
    int           sectorNum;

    printf("Please type in sector: ");
    scanf("%d", &sectorNum);

    memset(tag.bytes,    0xAA, sizeof(tag.bytes));
    memset(sector.bytes, 0xAA, sizeof(sector.bytes));

    pb.ioParam.ioRefNum     = chosenDrvrRefNum;
    pb.ioParam.ioCompletion = 0;
    pb.ioParam.ioBuffer     = sector.bytes;
    pb.ioParam.ioReqCount   = 512;
    pb.ioParam.ioPosMode    = fsFromStart;
    pb.ioParam.ioPosOffset  = sectorNum * 512;
    pb.ioParam.ioVRefNum    = chosenDriveNum;

    printf("Setting tag buffer\n");

    err = SetTagBuffer(tag.bytes); CHECK_ERR;

    printf("Calling .Sony driver with offset of %d\n", sectorNum * 512);

    err = PBReadSync(&pb); CHECK_ERR;

    SetTagBuffer(NULL);

    printf("All values initialized to AA prior to read.\n");

    printf("Block (initialized to AA): ");
    for(i = 0; i < 20; i++) {
        printf("%02x ", (unsigned char)sector.bytes[i]);
        if (i % 24 == 0) {
            printf("\n");
        }
    }
    printf("\n");

    printf("Sector Tags (initialized to AA):\n");
    for(i = 0; i < NELEMENTS(tag.bytes); i++) {
        printf("%02x ", (unsigned char)tag.bytes[i]);
    }
    printf("\n");

    return err;
}

static OSErr testPortLoopback() {
    ParamBlockRec pb;
    FujiSerDataHndl data;
    OSErr err;
    const short messageSize = 512;
    char msg[512];

    DEBUG_STAGE("Getting FujiNet handle");

    data = getFujiSerialDataHndl ();
    if (data && *data && (*data)->conn.iopb.ioPosOffset) {

        pb.ioParam.ioRefNum     = (*data)->conn.iopb.ioRefNum;
        pb.ioParam.ioPosMode    = fsFromStart;
        pb.ioParam.ioPosOffset  = (*data)->conn.iopb.ioPosOffset;
        pb.ioParam.ioVRefNum    = (*data)->conn.iopb.ioVRefNum;
        pb.ioParam.ioBuffer     = (Ptr)msg;
        pb.ioParam.ioReqCount   = 512;
        pb.ioParam.ioCompletion = 0;

        printf("Driver ref number     %d\n", pb.ioParam.ioRefNum);
        printf("Drive number:         %d\n", pb.ioParam.ioVRefNum);
        printf("Magic sector:         %ld\n",pb.ioParam.ioPosOffset / 512);

        DEBUG_STAGE("Writing block");

        FUJI_TAG_ID  = MAC_FUJI_REQUEST_TAG;
        FUJI_TAG_SRC = 0;
        FUJI_TAG_LEN = messageSize;

        err = PBWriteSync(&pb); CHECK_ERR;

        DEBUG_STAGE("Reading block");

        err = PBReadSync(&pb);  CHECK_ERR;
    } else {
        DEBUG_STAGE("Unable to get FujiNet handle");
    }
}

static void printThroughput(long bytesTransfered, long timeElapsed) {
    long bytesPerSecond = (timeElapsed == 0) ? 0 : (bytesTransfered * 60 / timeElapsed);
    if (bytesPerSecond > 1024) {
        printf( "   %3ld Kbytes/sec\n", bytesPerSecond / 1024);
    } else {
        printf( "   %3ld bytes/sec\n", bytesPerSecond);
    }
}

static OSErr testPortThroughput() {
    ParamBlockRec pb;
    FujiSerDataHndl data;
    OSErr err;
    const short messageSize = 512;
    long bytesRead = 0, bytesWritten = 0;
    long startTicks, endTicks;
    char msg[512];

    DEBUG_STAGE("Getting FujiNet handle");

    data = getFujiSerialDataHndl ();
    if (data && *data && (*data)->conn.iopb.ioPosOffset) {

        pb.ioParam.ioRefNum     = (*data)->conn.iopb.ioRefNum;
        pb.ioParam.ioPosMode    = fsFromStart;
        pb.ioParam.ioPosOffset  = (*data)->conn.iopb.ioPosOffset;
        pb.ioParam.ioVRefNum    = (*data)->conn.iopb.ioVRefNum;
        pb.ioParam.ioBuffer     = (Ptr)msg;
        pb.ioParam.ioReqCount   = 512;
        pb.ioParam.ioCompletion = 0;

        printf("Driver ref number     %d\n", pb.ioParam.ioRefNum);
        printf("Drive number:         %d\n", pb.ioParam.ioVRefNum);
        printf("Magic sector:         %ld\n",pb.ioParam.ioPosOffset / 512);

        DEBUG_STAGE("Testing floppy throughput...\n");

        for (startTicks = Ticks; Ticks - startTicks < 1200;) {
            FUJI_TAG_ID  = MAC_FUJI_REQUEST_TAG;
            FUJI_TAG_SRC = 0;
            FUJI_TAG_LEN = messageSize;

            err = PBWriteSync(&pb); CHECK_ERR;
            bytesWritten += pb.ioParam.ioActCount;

            err = PBReadSync(&pb);  CHECK_ERR;
            bytesRead += pb.ioParam.ioActCount;

        }
        endTicks = Ticks;
        printf(" out: %6ld ; in %6ld ... ", bytesWritten, bytesRead);
        printThroughput (bytesRead + bytesWritten, endTicks - startTicks);

    } else {
        DEBUG_STAGE("Unable to get FujiNet handle");
    }
}

static unsigned long nextRand (unsigned long seed) {
    return seed * 214013 + 2531011;
}

static OSErr testSerialThroughput(Boolean useSerGet) {
    #define kInputBufSIze 1024
    #define kMesgBufSIze 1536

    long bytesRead, bytesWritten, availBytes, startTicks, endTicks;
    unsigned long writeRand, readRand;
    short sInputRefNum, sOutputRefNum, i, j;
    ParamBlockRec pb;
    Handle gInputBufHandle;
    OSErr err;
    unsigned char msg[kMesgBufSIze];

    // Open the serial drivers

    DEBUG_STAGE("Opening serial driver");

    err = OpenDriver("\p.AOut",  &sOutputRefNum); CHECK_ERR;
    err = OpenDriver("\p.AIn",   &sInputRefNum); CHECK_ERR;

    // Replace the default input buffer

    DEBUG_STAGE("Setting the buffer");

    gInputBufHandle = NewHandle(kInputBufSIze);
    HLock(gInputBufHandle);
    SerSetBuf(sInputRefNum, *gInputBufHandle, kInputBufSIze); CHECK_ERR;

    DEBUG_STAGE("Flushing input data");

    for(;;) {
        err = SerGetBuf(sInputRefNum, &availBytes); CHECK_ERR;
        if (availBytes == 0) {
            break;
        }

        pb.ioParam.ioRefNum = sInputRefNum;
        pb.ioParam.ioBuffer  = (Ptr) msg;
        pb.ioParam.ioReqCount = availBytes;
        pb.ioParam.ioCompletion = 0;
        pb.ioParam.ioVRefNum = 0;
        pb.ioParam.ioPosMode = 0;
        err = PBRead(&pb, false); CHECK_ERR;
    }

    DEBUG_STAGE("Testing serial throughput");

    for (i = 0; i < 10; i++) {
        const short messageSize = (3 << i) >> 1;
        char lastOp;

        bytesRead = bytesWritten = 0;
        writeRand = readRand = 1;
        startTicks = endTicks = Ticks;

        for (;;) {
            // Send data for 20 seconds

            if (endTicks - startTicks < 1200) {
                endTicks = Ticks;

                // Fill the message with pseudo-random data

                #if BENCH_CHECK_MESSAGES
                    for (j = 0; j < messageSize; j++) {
                        writeRand = nextRand (writeRand);
                        msg[j] = writeRand & 0xFF;
                    }
                #endif

                #if BENCH_SHOW_OPERATION
                    // Send a message
                    if (lastOp != 'W') {
                        printf ("W\r");
                        lastOp = 'W';
                    }
                #endif

                pb.ioParam.ioRefNum = sOutputRefNum;
                pb.ioParam.ioBuffer  = (Ptr)msg;
                pb.ioParam.ioReqCount = messageSize;
                pb.ioParam.ioCompletion = 0;
                pb.ioParam.ioVRefNum = 0;
                pb.ioParam.ioPosMode = 0;
                err = PBWrite(&pb, false); CHECK_ERR;
                bytesWritten += pb.ioParam.ioActCount;

                #if BENCH_CHECK_MESSAGES
                    if (pb.ioParam.ioReqCount != messageSize) {
                        printf("ioReqCount changed after write! %ld != %d\n", pb.ioParam.ioReqCount, messageSize);
                    }

                    if (pb.ioParam.ioActCount != messageSize) {
                        printf("ioActCount not correct after write! %ld != %d\n", pb.ioParam.ioActCount, messageSize);
                    }
                #endif
            }

            // Keep reading data until we got back all the data we wrote

            if (bytesRead != bytesWritten) {
                // Receive a message

                if (useSerGet) {
                    err = SerGetBuf(sInputRefNum, &availBytes); CHECK_ERR;

                    if (availBytes < 0) {
                        printf("Got negative avail bytes! %ld\n", availBytes);
                    }

                    if (availBytes > kMesgBufSIze) {
                        availBytes = kMesgBufSIze;
                    }
                } else {
                    availBytes = bytesWritten - bytesRead;
                }

                if (availBytes) {
                    #if BENCH_SHOW_OPERATION
                        if (lastOp != 'R') {
                            printf ("R\r");
                            lastOp = 'R';
                        }
                    #endif

                    // Read a message
                    pb.ioParam.ioRefNum = sInputRefNum;
                    pb.ioParam.ioBuffer  = (Ptr) msg;
                    pb.ioParam.ioReqCount = availBytes;
                    pb.ioParam.ioCompletion = 0;
                    pb.ioParam.ioVRefNum = 0;
                    pb.ioParam.ioPosMode = 0;
                    err = PBRead(&pb, false); CHECK_ERR;

                    #if BENCH_CHECK_MESSAGES
                        if (pb.ioParam.ioReqCount != availBytes) {
                            printf("ioReqCount changed after read! %ld != %d\n", pb.ioParam.ioReqCount, availBytes);
                        }

                        if (pb.ioParam.ioActCount != availBytes) {
                            printf("ioActCount not correct after read! %ld != %d\n", pb.ioParam.ioActCount, availBytes);
                        }

                        // Verify the message against the pseudo-random data

                        for (j = 0; j < pb.ioParam.ioActCount; j++) {
                            unsigned char expected;
                            readRand = nextRand (readRand);
                            expected = readRand & 0xFF;
                            if (msg[j] != expected) {
                                printf("Data verification error on byte %ld: %x != %x\n", bytesRead + j, msg[j] & 0xFF, expected);
                                printHexDump (msg, pb.ioParam.ioActCount);
                                goto error;
                            }
                        }
                    #endif

                    bytesRead += pb.ioParam.ioActCount;
                } // availBytes
            } // bytesRead != bytesWritten
            else {
                break;
            }
        }
        endTicks = Ticks;

        printf("%3d byte messages: out: %6ld ; in %6ld ... ", messageSize, bytesWritten, bytesRead);
        printThroughput (bytesRead + bytesWritten, endTicks - startTicks);
    }

error:
    SerSetBuf(sInputRefNum, *gInputBufHandle, 0); CHECK_ERR;
    DisposeHandle(gInputBufHandle);

    KillIO(sOutputRefNum);
    CloseDriver(sInputRefNum);
    CloseDriver(sOutputRefNum);
}

char *errorStr(OSErr err) {
    switch(err) {
        case controlErr:   return "Driver can't respond to control calls";   // -17
        case readErr:      return "Driver can't respond to read calls";      // -19
        case writErr:      return "Driver can't respond to write calls";     // -20
        case eofErr:       return "End of file";                             // -39
        case nsDrvErr:     return "No such drive";
        case fnfErr:       return "File not found error";                    // -43
        case dupFNErr:     return "File already exists";                     // -48
        case opWrErr:      return "File already open with write permission"; // -49
        case paramErr:     return "Error in user param list";                // -50
        case rfNumErr:     return "Ref num error";                           // -51
        case nsvErr:       return "No such volume";                          // -56
        case noDriveErr:   return "Drive not installed";                     // -64
        case offLinErr:    return "Read/write requested for offline drive";  // -65
        case sectNFErr:    return "Sector number never found on a track";    // -81
        case portInUse:    return "Port in use";                             // -97
        case resNotFound:  return "Resource not found";                      // -192
        default:           return "";
    }
}

static OSErr printOwnedResourceId() {
    short unitNumber, subId, resId;
    printf("Please select driver: ");
    scanf("%d", &unitNumber);
    printf("Enter resource sub id: ");
    scanf("%d", &subId);
    resId = 0xC000 | (unitNumber << 5) | subId;
    printf("Owned resource id: %d\n", resId);
}

static OSErr mainHelp() {
    printf("1: Drive tests\n");
    printf("2: FujiNet interface tests\n");
    printf("3: Serial driver tests\n");
    printf("4: Miscellaneous tests\n");
    printf("q: Exit\n");
    return noErr;
}

static OSErr diskHelp() {
    printf("1: List drives (and mounted volumes)\n");
    printf("2: Select drive\n");
    printf("3: Read sector and tags\n");
    printf("q: Main menu\n");
    return noErr;
}

static OSErr diskChoice(char mode) {
    switch(mode) {
        case '1': printDriveQueue(); break;
        case '2': chooseDrive(); break;
        case '3': readSectorAndTags(); break;
        default: -1;
    }
    return noErr;
}

static OSErr drvrHelp() {
    printf("1: Print unit table\n");
    printf("2: Print status of drivers\n");
    printf("3: Install modem driver\n");
    printf("4: Install printer driver\n");
    printf("5: Test serial driver\n");
    printf("6: Test serial throughput with blocking I/O\n");
    printf("7: Test serial throughput with non-blocking I/O\n");
    printf("8: Set VBL frequency\n");
    printf("q: Main menu\n");
    return noErr;
}

static OSErr drvrChoice(char mode) {
    switch(mode) {
        case '1': printUnitTable(); break;
        case '2': printDriverStatus(); break;
        case '3': fujiSerialRedirectModem(); break;
        case '4': fujiSerialRedirectPrinter(); break;
        case '5': testSerialDriver(); break;
        case '6': testSerialThroughput (false); break;
        case '7': testSerialThroughput (true); break;
        case '8': setVBLFrequency(); break;
        default: -1;
    }
    return noErr;
}

static OSErr miscHelp() {
    printf("1: Compute owned resource id\n");
    printf("q: Main menu\n");
    return noErr;
}

static OSErr miscChoice(char mode) {
    switch(mode) {
        case '1': printOwnedResourceId(); break;
        default: -1;
    }
    return noErr;
}

static OSErr fujiHelp() {
    printf("1: Open FujiNet device\n");
    printf("2: Test floppy port read/write\n");
    printf("3: Test floppy port throughput\n");
    printf("q: Main menu\n");
    return noErr;
}

static OSErr fujiChoice(char mode) {
    switch(mode) {
        case '1': openFujiNet(); break;
        case '2': testPortLoopback(); break;
        case '3': testPortThroughput(); break;
        default: -1;
    }
    return noErr;
}

static OSErr mtcpHelp() {
    printf("1: Basic MacTCP test\n");
    printf("q: Main menu\n");
    return noErr;
}

static OSErr mtcpChoice(char mode) {
    switch(mode) {
        case '1': testBasicTCP(); break;
        default: -1;
    }
    return noErr;
}

int main() {
    OSErr   err;
    short   command, inOut = 100;
    char    buf[100], c = 0, mode = 0;

    printf("built " __DATE__ " " __TIME__ "\n\n\n");

    while (c != 'q') {
        switch(mode) {
            case '1': diskHelp(); break;
            case '2': fujiHelp(); break;
            case '3': drvrHelp(); break;
            case '4': mtcpHelp(); break;
            case '5': miscHelp(); break;
            default:  mainHelp();
        }

        printf(">");
        c = getchar();
        while (isspace(c)) {
            c = getchar();
        }

        if (mode && (c == 'q')) {
            mode = 0;
            c = ' ';
        } else {
            switch(mode) {
                case '1': err = diskChoice(c); break;
                case '2': err = fujiChoice(c); break;
                case '3': err = drvrChoice(c); break;
                case '4': err = mtcpChoice(c); break;
                case '5': err = miscChoice(c); break;
                default: mode = c;
            }
        }
        if (err == -1) {
            printf("Invalid choice!\n");
        }
        printf("\n\n");
    }

    return 0;
}