/** @file
  Host test of Library/AppleUartSerialPortLib: what it answers with a serial port
  and without one.

  Without a port (the device tree has no /arm-io/uart0, as when m1n1 runs the
  guest without a host) SerialPortInitialize() must succeed: the debug library
  of every module and the status code handler ASSERT on anything else.
  SerialPortSetAttributes() must answer RETURN_DEVICE_ERROR, on which SerialDxe
  does not start. Nothing may touch a register. With a port the library answers
  as it did before there could be none.

  Run it with ./run.sh; it needs the host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "HostTest.h"

#include <Library/IoLib.h>
#include <Library/SerialPortLib.h>

//
// What AppleUartSerialPortLib.c takes from the firmware: the address of the
// port (AppleUartBase.h), the registers, the interrupt mask (ArmLib.h, which
// run.sh keeps out)
//
STATIC UINT64  mBase;
STATIC UINTN   mAccesses;           // reads and writes of a register
STATIC UINT32  mWritten[16][2];     // offset from mBase, value
STATIC UINTN   mWriteCount;

UINT64 AppleUartBase(VOID)
{
    return mBase;
}

// Every status bit set: the transmitter is empty, a loop that waits for it ends
UINT32 EFIAPI MmioRead32(IN UINTN Address)
{
    mAccesses++;
    return MAX_UINT32;
}

UINT32 EFIAPI MmioWrite32(IN UINTN Address, IN UINT32 Value)
{
    mAccesses++;
    if(mWriteCount < ARRAY_SIZE(mWritten)) {
        mWritten[mWriteCount][0] = (UINT32)(Address - mBase);
        mWritten[mWriteCount][1] = Value;
        mWriteCount++;
    }
    return Value;
}

VOID EFIAPI ArmDisableInterrupts(VOID)
{
}

VOID EFIAPI ArmEnableInterrupts(VOID)
{
}

#include "../../Library/AppleUartSerialPortLib/AppleUartSerialPortLib.c"

STATIC VOID Reset(IN UINT64 Base)
{
    mBase       = Base;
    mAccesses   = 0;
    mWriteCount = 0;
}

// The attributes SerialDxe asks for when it starts: its defaults
STATIC RETURN_STATUS SetDefaultAttributes(VOID)
{
    UINT64             BaudRate         = 115200;
    UINT32             ReceiveFifoDepth = 1;
    UINT32             Timeout          = 1000000;
    EFI_PARITY_TYPE    Parity           = NoParity;
    UINT8              DataBits         = 8;
    EFI_STOP_BITS_TYPE StopBits         = OneStopBit;

    return SerialPortSetAttributes(&BaudRate, &ReceiveFifoDepth, &Timeout, &Parity, &DataBits, &StopBits);
}

int main(void)
{
    STATIC UINT8 Text[] = { 'a', 'b', 'c' };
    UINT8        Byte;

    //
    // No port
    //
    Reset(0);
    Expect(SerialPortInitialize() == RETURN_SUCCESS, "no port: SerialPortInitialize() succeeds, so no debug library ASSERTs");
    Expect(SetDefaultAttributes() == RETURN_DEVICE_ERROR, "no port: SerialPortSetAttributes() answers a device error, so SerialDxe does not start");
    Expect(SerialPortWrite(Text, sizeof(Text)) == 0, "no port: SerialPortWrite() writes nothing and says so");
    Expect(SerialPortRead(&Byte, 1) == 0 && !SerialPortPoll() && SerialPortFlush() == 0, "no port: nothing to read, nothing to flush");
    Expect(mAccesses == 0, "no port: no register is touched");

    //
    // The port of the M4 MacBook Pro
    //
    Reset(0x3ad200000ULL);
    Expect(SerialPortInitialize() == RETURN_SUCCESS && mWriteCount == 1 && mWritten[0][0] == UART_BAUD_RATE_CONFIG &&
           mWritten[0][1] == AppleSerialPortCalculateBaudRateConfig(), "a port: SerialPortInitialize() sets 115200 baud");
    Reset(0x3ad200000ULL);
    Expect(SetDefaultAttributes() == RETURN_UNSUPPORTED && mAccesses == 0,
           "a port: SerialPortSetAttributes() answers \"unsupported\", which SerialDxe takes as keeping the defaults");
    Reset(0x3ad200000ULL);
    Expect(SerialPortWrite(Text, sizeof(Text)) == sizeof(Text) && mWriteCount == 3 &&
           mWritten[0][0] == UART_TX_BYTE && mWritten[0][1] == 'a' && mWritten[1][1] == 'b' && mWritten[2][0] == UART_TX_BYTE && mWritten[2][1] == 'c',
           "a port: SerialPortWrite() writes the three bytes and says so");
    Reset(0x3ad200000ULL);
    Expect(SerialPortRead(&Byte, 1) == 0 && !SerialPortPoll() && mAccesses == 0, "a port: nothing is read, the receive direction is left alone");

    return Verdict();
}
