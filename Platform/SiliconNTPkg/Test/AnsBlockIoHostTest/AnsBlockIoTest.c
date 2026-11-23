/** @file
  Host test of the internal disk driver, Drivers/AnsBlockIoDxe: the driver's
  own code as the firmware builds it, the disk controller's core of
  siliconnt-drivers (ans/core) under it, and under that the fake controller of
  that repository's model test (ans/core/test/fake_ans.c), which checks every
  access the core makes. On top, the device tree of the M4 MacBook Pro from
  HostTest.h.

  What the firmware would provide is stood in for here: the boot services the
  driver calls, NonCoherentDmaLib, TimerLib, IoLib and DebugLib. The stand-ins
  send register accesses and memory to the fake, move its clock, and record what
  the driver did.

  The block device:
    - It comes up on the M4's tree and publishes the protocol and a device path
      that is the same on every boot; the ExitBootServices event is there.
    - It publishes nothing when the controller does not come up, when the
      event or the protocols cannot be installed, or when the tree has no
      controller (then it touches no register and allocates nothing).
    - Reads: one block; reads that must be split; the last blocks; past the
      end; a length that is not whole blocks; a buffer that is not aligned;
      DmaMap() mapping less than asked; 512-byte blocks. The data must be what
      the fake's disk holds, and every mapping must be unmapped after the
      controller is done with it.
    - A read the controller fails, or never finishes: an error, the controller
      stopped before the memory is unmapped, and no read after it.
    - Writes are refused as write-protected, whatever is asked.
    - ExitBootServices brings the controller down, and nothing is allocated,
      freed, mapped or unmapped inside it.
  The environment (AnsEnv.c):
    - Register accesses outside their region, or not aligned, are refused.
    - Device memory is aligned, zeroed, and refused when the device address is
      not aligned or while ExitBootServices runs; freeing it then does nothing.
    - The core's log formats come out right through the firmware's print
      functions.

  Usage: AnsBlockIoTest [-v]; -v also prints the log. Run it with ./run.sh.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../../../../Silicon/Apple/AppleSiliconPkg/Test/AppleDTLibHostTest/HostTest.h"

#include "../../Drivers/AnsBlockIoDxe/AnsEnv.c"
#include "../../Drivers/AnsBlockIoDxe/AnsBlockIoDxe.c"

#include "../../Drivers/AnsBlockIoDxe/siliconnt-drivers/ans/core/test/fake_ans.h"

#include <stdarg.h>
#include <stdlib.h>

//
// The SART version of the M4's tree, which the fake must have too
//
#define SART_VERSION  3

//
// Blocks per read the driver hands to the controller, with 4 KiB blocks
//
#define BLOCKS_PER_READ  (ANS_DISK_MAX_TRANSFER / SIZE_4KB)

//
// 100 characters, for a format too long for a line of the log
//
#define X10   "xxxxxxxxxx"
#define X100  X10 X10 X10 X10 X10 X10 X10 X10 X10 X10

STATIC BOOLEAN  mVerbose;
STATIC CHAR8    mWhat[512];

//
// The fake controller of the test that runs. NULL: no register is expected to
// be reached, and the register stand-ins only record what was asked.
//
STATIC struct fake  mFakeStore;
STATIC struct fake  *mFake;

// ---------------------------------------------------------------------------
// GUIDs the driver uses
// ---------------------------------------------------------------------------

EFI_GUID  gEfiBlockIoProtocolGuid       = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID  gEfiDevicePathProtocolGuid    = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID  gEfiEventExitBootServicesGuid = EFI_EVENT_GROUP_EXIT_BOOT_SERVICES;

// ---------------------------------------------------------------------------
// DebugLib: the log, kept for the checks
// ---------------------------------------------------------------------------

#define LOG_LINES  4096
#define LOG_WIDTH  256

STATIC CHAR8  mLog[LOG_LINES][LOG_WIDTH];
STATIC UINTN  mLogLines;
STATIC UINTN  mAssertions;

STATIC CONST CHAR8 *
StatusName (
  IN RETURN_STATUS  Status
  )
{
  switch (Status) {
    case RETURN_SUCCESS:           return "Success";
    case RETURN_NOT_FOUND:         return "Not Found";
    case RETURN_INVALID_PARAMETER: return "Invalid Parameter";
    case RETURN_DEVICE_ERROR:      return "Device Error";
    case RETURN_OUT_OF_RESOURCES:  return "Out of Resources";
    case RETURN_UNSUPPORTED:       return "Unsupported";
    default:                       return "another status";
  }
}

//
// The conversions of the firmware's print functions (MdePkg BasePrintLib) the
// driver uses: %a is an ASCII string, l makes d, u and x 64-bit, %r is a
// status. %s is a CHAR16 string there; any %s that reaches this is printed as
// a marker, so that a format the environment did not translate shows.
//
STATIC VOID
LogLine (
  IN CONST CHAR8  *Format,
  IN va_list      Marker
  )
{
  CHAR8    Line[LOG_WIDTH];
  UINTN    Used;
  CHAR8    Spec[16];
  UINTN    SpecUsed;
  BOOLEAN  Long;
  int      Wrote;

  Used = 0;
  while ((*Format != '\0') && (Used + 1 < sizeof (Line))) {
    if (*Format != '%') {
      Line[Used++] = *Format++;
      continue;
    }

    Format++;
    Spec[0]  = '%';
    SpecUsed = 1;
    Long     = FALSE;
    while (((*Format >= '0') && (*Format <= '9')) || (*Format == 'l') || (*Format == 'L')) {
      if ((*Format == 'l') || (*Format == 'L')) {
        Long = TRUE;
      } else if (SpecUsed < sizeof (Spec) - 4) {
        Spec[SpecUsed++] = *Format;
      }

      Format++;
    }

    Wrote = 0;
    switch (*Format) {
      case 'a':
        Spec[SpecUsed++] = 's';
        Spec[SpecUsed]   = '\0';
        Wrote            = snprintf (Line + Used, sizeof (Line) - Used, Spec, va_arg (Marker, CHAR8 *));
        break;
      case 'd':
        if (Long) {
          Spec[SpecUsed++] = 'l';
          Spec[SpecUsed++] = 'l';
        }

        Spec[SpecUsed++] = 'd';
        Spec[SpecUsed]   = '\0';
        Wrote            = Long ? snprintf (Line + Used, sizeof (Line) - Used, Spec, va_arg (Marker, long long))
                                : snprintf (Line + Used, sizeof (Line) - Used, Spec, va_arg (Marker, int));
        break;
      case 'u':
      case 'x':
      case 'X':
        if (Long) {
          Spec[SpecUsed++] = 'l';
          Spec[SpecUsed++] = 'l';
        }

        Spec[SpecUsed++] = *Format;
        Spec[SpecUsed]   = '\0';
        Wrote            = Long ? snprintf (Line + Used, sizeof (Line) - Used, Spec, va_arg (Marker, unsigned long long))
                                : snprintf (Line + Used, sizeof (Line) - Used, Spec, va_arg (Marker, unsigned int));
        break;
      case 'p':
        Wrote = snprintf (Line + Used, sizeof (Line) - Used, "%p", va_arg (Marker, VOID *));
        break;
      case 'r':
        Wrote = snprintf (Line + Used, sizeof (Line) - Used, "%s", StatusName (va_arg (Marker, RETURN_STATUS)));
        break;
      case 's':
        (VOID)va_arg (Marker, VOID *);
        Wrote = snprintf (Line + Used, sizeof (Line) - Used, "<CHAR16 string>");
        break;
      case '%':
        Wrote = snprintf (Line + Used, sizeof (Line) - Used, "%%");
        break;
      default:
        Wrote = snprintf (Line + Used, sizeof (Line) - Used, "<%%%c?>", *Format);
        break;
    }

    if (*Format != '\0') {
      Format++;
    }

    if (Wrote > 0) {
      Used = MIN (Used + (UINTN)Wrote, sizeof (Line) - 1);
    }
  }

  Line[Used] = '\0';
  while ((Used > 0) && (Line[Used - 1] == '\n')) {
    Line[--Used] = '\0';
  }

  if (mLogLines < LOG_LINES) {
    memcpy (mLog[mLogLines], Line, sizeof (Line));
  }

  mLogLines++;
  if (mVerbose) {
    printf ("      | %s\n", Line);
  }
}

VOID
EFIAPI
DebugVPrint (
  IN  UINTN        ErrorLevel,
  IN  CONST CHAR8  *Format,
  IN  VA_LIST      VaListMarker
  )
{
  LogLine (Format, VaListMarker);
}

VOID
EFIAPI
DebugPrint (
  IN  UINTN        ErrorLevel,
  IN  CONST CHAR8  *Format,
  ...
  )
{
  va_list  Marker;

  va_start (Marker, Format);
  LogLine (Format, Marker);
  va_end (Marker);
}

VOID
EFIAPI
DebugAssert (
  IN CONST CHAR8  *FileName,
  IN UINTN        LineNumber,
  IN CONST CHAR8  *Description
  )
{
  mAssertions++;
  printf ("      ASSERT %s:%u: %s\n", FileName, (unsigned)LineNumber, Description);
}

BOOLEAN EFIAPI DebugAssertEnabled (VOID)
{
  return TRUE;
}

BOOLEAN EFIAPI DebugPrintEnabled (VOID)
{
  return TRUE;
}

BOOLEAN EFIAPI DebugCodeEnabled (VOID)
{
  return TRUE;
}

BOOLEAN EFIAPI DebugClearMemoryEnabled (VOID)
{
  return FALSE;
}

BOOLEAN EFIAPI DebugPrintLevelEnabled (IN CONST UINTN ErrorLevel)
{
  return TRUE;
}

STATIC BOOLEAN
Logged (
  IN CONST CHAR8  *Needle
  )
{
  UINTN  Index;

  for (Index = 0; Index < MIN (mLogLines, LOG_LINES); Index++) {
    if (strstr (mLog[Index], Needle) != NULL) {
      return TRUE;
    }
  }

  return FALSE;
}

// ---------------------------------------------------------------------------
// The two barriers: AnsBarrier.c is ARM code. Counted.
// ---------------------------------------------------------------------------

STATIC UINTN  mWmb;
STATIC UINTN  mRmb;

void
ans_env_wmb (
  struct ans_env  *env
  )
{
  mWmb++;
}

void
ans_env_rmb (
  struct ans_env  *env
  )
{
  mRmb++;
}

// ---------------------------------------------------------------------------
// TimerLib: the fake's clock
// ---------------------------------------------------------------------------

STATIC UINT64  mDelayed;

UINTN
EFIAPI
MicroSecondDelay (
  IN UINTN  MicroSeconds
  )
{
  mDelayed += MicroSeconds;
  if (mFake != NULL) {
    fake_advance (mFake, (uint32_t)MicroSeconds);
    fake_tick (mFake);
  }

  return MicroSeconds;
}

// ---------------------------------------------------------------------------
// IoLib: registers go to the fake, by the region the address falls in
// ---------------------------------------------------------------------------

STATIC UINTN   mMmio;
STATIC UINTN   mStrayMmio;
STATIC UINTN   mLastMmioAddress;
STATIC UINT32  mLastMmioWidth;

STATIC BOOLEAN
FakeRegister (
  IN  UINTN   Address,
  IN  UINT32  Width,
  OUT int     *Region,
  OUT UINT32  *Offset
  )
{
  CONST ANS_ENV_REGION  *Range;
  int                   Index;

  mMmio++;
  mLastMmioAddress = Address;
  mLastMmioWidth   = Width;
  if (mFake == NULL) {
    return FALSE;
  }

  for (Index = 0; Index < ANS_RGN_COUNT; Index++) {
    Range = &mAnsDisk.Env.Region[Index];
    if ((Range->Base != 0) && (Address >= Range->Base) && (Address - Range->Base + Width <= Range->Size)) {
      *Region = Index;
      *Offset = (UINT32)(Address - Range->Base);
      return TRUE;
    }
  }

  mStrayMmio++;
  return FALSE;
}

UINT32
EFIAPI
MmioRead32 (
  IN UINTN  Address
  )
{
  int     Region;
  UINT32  Offset;

  if (!FakeRegister (Address, 4, &Region, &Offset)) {
    return 0x11223344;
  }

  return (UINT32)fake_reg_read (mFake, Region, Offset, 4);
}

UINT32
EFIAPI
MmioWrite32 (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  int     Region;
  UINT32  Offset;

  if (FakeRegister (Address, 4, &Region, &Offset)) {
    fake_reg_write (mFake, Region, Offset, 4, Value);
  }

  return Value;
}

UINT64
EFIAPI
MmioRead64 (
  IN UINTN  Address
  )
{
  int     Region;
  UINT32  Offset;

  if (!FakeRegister (Address, 8, &Region, &Offset)) {
    return 0x1122334455667788ULL;
  }

  return fake_reg_read (mFake, Region, Offset, 8);
}

UINT64
EFIAPI
MmioWrite64 (
  IN UINTN   Address,
  IN UINT64  Value
  )
{
  int     Region;
  UINT32  Offset;

  if (FakeRegister (Address, 8, &Region, &Offset)) {
    fake_reg_write (mFake, Region, Offset, 8, Value);
  }

  return Value;
}

// ---------------------------------------------------------------------------
// NonCoherentDmaLib: memory the fake controller reaches
//
// An allocation is a block of the fake's memory. A mapping of a caller's buffer
// for the device to write into is a block of its own, copied into the buffer
// at DmaUnmap() and freed: the data reaches the caller only through the unmap,
// as with the real library, which invalidates the cache lines there.
// ---------------------------------------------------------------------------

typedef struct {
  BOOLEAN              Live;
  BOOLEAN              Allocation;    // TRUE: DmaAllocateAlignedBuffer(); FALSE: a mapping
  BOOLEAN              Mapped;        // an allocation mapped as a common buffer
  VOID                 *Host;         // the caller's address
  VOID                 *Bounce;       // a mapping: what the device writes
  UINT64               Device;
  UINTN                Bytes;
  UINTN                Pages;
} DMA_RECORD;

#define DMA_RECORDS  256

STATIC DMA_RECORD  mDma[DMA_RECORDS];
STATIC UINTN       mDmaRecords;

STATIC UINTN    mAllocs;
STATIC UINTN    mFrees;
STATIC UINTN    mMaps;              // for the device to write into
STATIC UINTN    mUnmaps;
STATIC UINTN    mCallsInExit;       // DmaLib calls inside ExitBootServices
STATIC UINTN    mUnmapInFlight;     // a mapping unmapped while the read was in flight
STATIC UINTN    mFreeWhileMapped;
STATIC BOOLEAN  mInExitBootServices;
STATIC EFI_TPL  mTpl;
STATIC EFI_TPL  mTplAtMap;

//
// Knobs
//
STATIC BOOLEAN  mFailAlloc;
STATIC UINT64   mCommonBufferSkew;  // added to the device address of a common buffer
STATIC BOOLEAN  mShortenMaps;       // map about half of what is asked

STATIC DMA_RECORD *
NewDmaRecord (
  VOID
  )
{
  if (mDmaRecords == DMA_RECORDS) {
    printf ("out of DMA records\n");
    exit (2);
  }

  ZeroMem (&mDma[mDmaRecords], sizeof (mDma[0]));
  return &mDma[mDmaRecords++];
}

EFI_STATUS
EFIAPI
DmaAllocateAlignedBuffer (
  IN  EFI_MEMORY_TYPE  MemoryType,
  IN  UINTN            Pages,
  IN  UINTN            Alignment,
  OUT VOID             **HostAddress
  )
{
  DMA_RECORD  *Record;
  void        *Va;
  uint64_t    Dva;

  if (mInExitBootServices) {
    mCallsInExit++;
  }

  if (mFailAlloc || (Alignment != SIZE_16KB) || (MemoryType != EfiBootServicesData)) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (!fake_mem_alloc (mFake, EFI_PAGES_TO_SIZE (Pages), true, &Va, &Dva)) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Not zeroed: the environment must zero it
  memset (Va, 0xa5, EFI_PAGES_TO_SIZE (Pages));

  Record             = NewDmaRecord ();
  Record->Live       = TRUE;
  Record->Allocation = TRUE;
  Record->Host       = Va;
  Record->Device     = Dva;
  Record->Pages      = Pages;
  *HostAddress       = Va;
  mAllocs++;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DmaFreeBuffer (
  IN  UINTN  Pages,
  IN  VOID   *HostAddress
  )
{
  UINTN  Index;

  if (mInExitBootServices) {
    mCallsInExit++;
  }

  for (Index = 0; Index < mDmaRecords; Index++) {
    if (mDma[Index].Live && mDma[Index].Allocation && (mDma[Index].Host == HostAddress) && (mDma[Index].Pages == Pages)) {
      if (mDma[Index].Mapped) {
        mFreeWhileMapped++;
      }

      fake_mem_free (mFake, mDma[Index].Device);
      mDma[Index].Live = FALSE;
      mFrees++;
      return EFI_SUCCESS;
    }
  }

  printf ("      DmaFreeBuffer of memory never allocated\n");
  mFreeWhileMapped++;
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
EFIAPI
DmaMap (
  IN     DMA_MAP_OPERATION     Operation,
  IN     VOID                  *HostAddress,
  IN OUT UINTN                 *NumberOfBytes,
  OUT    PHYSICAL_ADDRESS      *DeviceAddress,
  OUT    VOID                  **Mapping
  )
{
  DMA_RECORD  *Record;
  UINTN       Index;
  void        *Va;
  uint64_t    Dva;

  if (mInExitBootServices) {
    mCallsInExit++;
  }

  if (Operation == MapOperationBusMasterCommonBuffer) {
    for (Index = 0; Index < mDmaRecords; Index++) {
      if (mDma[Index].Live && mDma[Index].Allocation && (mDma[Index].Host == HostAddress)) {
        mDma[Index].Mapped = TRUE;
        *NumberOfBytes     = MIN (*NumberOfBytes, EFI_PAGES_TO_SIZE (mDma[Index].Pages));
        *DeviceAddress     = mDma[Index].Device + mCommonBufferSkew;
        *Mapping           = &mDma[Index];
        return EFI_SUCCESS;
      }
    }

    return EFI_UNSUPPORTED;
  }

  if (Operation != MapOperationBusMasterWrite) {
    printf ("      DmaMap for the device to read from memory\n");
    return EFI_UNSUPPORTED;
  }

  if (mShortenMaps && (*NumberOfBytes > SIZE_4KB)) {
    *NumberOfBytes = *NumberOfBytes / 2 + 100;
  }

  if (!fake_mem_alloc (mFake, *NumberOfBytes, false, &Va, &Dva)) {
    return EFI_OUT_OF_RESOURCES;
  }

  Record         = NewDmaRecord ();
  Record->Live   = TRUE;
  Record->Host   = HostAddress;
  Record->Bounce = Va;
  Record->Device = Dva;
  Record->Bytes  = *NumberOfBytes;
  *DeviceAddress = Dva;
  *Mapping       = Record;
  mTplAtMap      = mTpl;
  mMaps++;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DmaUnmap (
  IN  VOID  *Mapping
  )
{
  DMA_RECORD  *Record;

  if (mInExitBootServices) {
    mCallsInExit++;
  }

  Record = Mapping;
  if (Record->Allocation) {
    Record->Mapped = FALSE;
    return EFI_SUCCESS;
  }

  if (mAnsDisk.Core.nvme.inflight != 0) {
    mUnmapInFlight++;
  }

  memcpy (Record->Host, Record->Bounce, Record->Bytes);
  fake_mem_free (mFake, Record->Device);
  Record->Live = FALSE;
  mUnmaps++;
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Boot services
// ---------------------------------------------------------------------------

STATIC UINTN             mEventsCreated;
STATIC UINTN             mEventsClosed;
STATIC EFI_EVENT_NOTIFY  mExitNotify;
STATIC VOID              *mExitContext;
STATIC EFI_TPL           mExitTpl;
STATIC UINT32            mExitType;
STATIC BOOLEAN           mExitGroupRight;
STATIC UINTN             mInstalls;
STATIC EFI_BLOCK_IO_PROTOCOL     *mBlockIo;
STATIC EFI_DEVICE_PATH_PROTOCOL  *mDevicePath;
STATIC UINT8             mEventToken;
STATIC UINT8             mHandleToken;

//
// Knobs
//
STATIC BOOLEAN  mFailCreateEvent;
STATIC BOOLEAN  mFailInstall;

STATIC
EFI_TPL
EFIAPI
StubRaiseTpl (
  IN EFI_TPL  NewTpl
  )
{
  EFI_TPL  Old;

  Old  = mTpl;
  mTpl = NewTpl;
  return Old;
}

STATIC
VOID
EFIAPI
StubRestoreTpl (
  IN EFI_TPL  OldTpl
  )
{
  mTpl = OldTpl;
}

STATIC
EFI_STATUS
EFIAPI
StubCreateEventEx (
  IN       UINT32            Type,
  IN       EFI_TPL           NotifyTpl,
  IN       EFI_EVENT_NOTIFY  NotifyFunction OPTIONAL,
  IN CONST VOID              *NotifyContext OPTIONAL,
  IN CONST EFI_GUID          *EventGroup OPTIONAL,
  OUT      EFI_EVENT         *Event
  )
{
  if (mFailCreateEvent) {
    return EFI_OUT_OF_RESOURCES;
  }

  mEventsCreated++;
  mExitNotify     = NotifyFunction;
  mExitContext    = (VOID *)NotifyContext;
  mExitTpl        = NotifyTpl;
  mExitType       = Type;
  mExitGroupRight = (EventGroup != NULL) && (memcmp (EventGroup, &gEfiEventExitBootServicesGuid, sizeof (EFI_GUID)) == 0);
  *Event          = &mEventToken;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
StubCloseEvent (
  IN EFI_EVENT  Event
  )
{
  mEventsClosed++;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
StubInstallMultipleProtocolInterfaces (
  IN OUT EFI_HANDLE  *Handle,
  ...
  )
{
  va_list   Marker;
  EFI_GUID  *Guid;
  VOID      *Interface;

  if (mFailInstall) {
    return EFI_OUT_OF_RESOURCES;
  }

  va_start (Marker, Handle);
  for (Guid = va_arg (Marker, EFI_GUID *); Guid != NULL; Guid = va_arg (Marker, EFI_GUID *)) {
    Interface = va_arg (Marker, VOID *);
    if (memcmp (Guid, &gEfiBlockIoProtocolGuid, sizeof (EFI_GUID)) == 0) {
      mBlockIo = Interface;
    } else if (memcmp (Guid, &gEfiDevicePathProtocolGuid, sizeof (EFI_GUID)) == 0) {
      mDevicePath = Interface;
    } else {
      printf ("      a protocol the test does not know is installed\n");
    }
  }

  va_end (Marker);
  *Handle = &mHandleToken;
  mInstalls++;
  return EFI_SUCCESS;
}

//
// Only what the driver may call is there; anything else is a NULL pointer
//
STATIC EFI_BOOT_SERVICES  mBootServices = {
  .RaiseTPL                           = StubRaiseTpl,
  .RestoreTPL                         = StubRestoreTpl,
  .CreateEventEx                      = StubCreateEventEx,
  .CloseEvent                         = StubCloseEvent,
  .InstallMultipleProtocolInterfaces  = StubInstallMultipleProtocolInterfaces,
};

EFI_BOOT_SERVICES  *gBS = &mBootServices;

// ---------------------------------------------------------------------------
// One boot
// ---------------------------------------------------------------------------

STATIC VOID
NewWorld (
  VOID
  )
{
  UINT32  Index;

  for (Index = 0; Index < mFakeStore.nblocks; Index++) {
    free (mFakeStore.blocks[Index].va);
  }

  fake_init (&mFakeStore, SART_VERSION);
  mFake = &mFakeStore;

  ZeroMem (mDma, sizeof (mDma));
  mDmaRecords         = 0;
  mAllocs             = 0;
  mFrees              = 0;
  mMaps               = 0;
  mUnmaps             = 0;
  mCallsInExit        = 0;
  mUnmapInFlight      = 0;
  mFreeWhileMapped    = 0;
  mInExitBootServices = FALSE;
  mTpl                = TPL_APPLICATION;
  mTplAtMap           = TPL_APPLICATION;
  mFailAlloc          = FALSE;
  mCommonBufferSkew   = 0;
  mShortenMaps        = FALSE;

  mEventsCreated   = 0;
  mEventsClosed    = 0;
  mExitNotify      = NULL;
  mExitContext     = NULL;
  mExitTpl         = 0;
  mExitType        = 0;
  mExitGroupRight  = FALSE;
  mInstalls        = 0;
  mBlockIo         = NULL;
  mDevicePath      = NULL;
  mFailCreateEvent = FALSE;
  mFailInstall     = FALSE;

  mMmio       = 0;
  mStrayMmio  = 0;
  mDelayed    = 0;
  mLogLines   = 0;
  mAssertions = 0;
  ZeroMem (&mAnsDisk, sizeof (mAnsDisk));
}

STATIC EFI_STATUS
Boot (
  IN CONST TEST_MACHINE  *Machine
  )
{
  BuildMachine (Machine);
  return AnsDiskPublish (BuiltTree (), &mAnsDisk);
}

STATIC VOID
ExitBootServices (
  VOID
  )
{
  mInExitBootServices = TRUE;
  mExitNotify (&mEventToken, mExitContext);
  mInExitBootServices = FALSE;
}

STATIC UINT32
IoReads (
  VOID
  )
{
  UINT32  Count;
  UINT32  Index;

  Count = 0;
  for (Index = 0; Index < mFake->ncmds; Index++) {
    if ((mFake->cmds[Index].queue == 1) && (mFake->cmds[Index].opcode == 0x02)) {
      Count++;
    }
  }

  return Count;
}

//
// What must hold after every boot: nothing the fake objects to, every register
// access inside a region, memory unmapped and freed only once the controller is
// done with it, no assertion, nothing in ExitBootServices
//
STATIC VOID
CheckClean (
  IN CONST CHAR8  *When
  )
{
  snprintf (
    mWhat,
    sizeof (mWhat),
    "%.60s: the fake controller saw nothing wrong%s%.300s",
    When,
    mFake->violations != 0 ? " -- it saw: " : "",
    mFake->violations != 0 ? mFake->first_violation : ""
    );
  Expect (mFake->violations == 0, mWhat);
  snprintf (mWhat, sizeof (mWhat), "%s: every register access inside its region", When);
  Expect (mStrayMmio == 0 && mAnsDisk.Env.Refused == 0, mWhat);
  snprintf (mWhat, sizeof (mWhat), "%s: memory unmapped only after the controller is done with it, freed only unmapped", When);
  Expect (mUnmapInFlight == 0 && mFreeWhileMapped == 0, mWhat);
  snprintf (mWhat, sizeof (mWhat), "%s: no assertion failed, no DMA call inside ExitBootServices", When);
  Expect (mAssertions == 0 && mCallsInExit == 0, mWhat);
}

STATIC BOOLEAN
DataOk (
  IN CONST UINT8  *Data,
  IN UINT64       Lba,
  IN UINTN        Bytes,
  IN UINT32       BlockSize
  )
{
  UINTN  Index;

  for (Index = 0; Index < Bytes; Index++) {
    if (Data[Index] != fake_disk_byte (Lba + Index / BlockSize, BlockSize, (UINT32)(Index % BlockSize))) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC EFI_STATUS
Read (
  IN EFI_LBA  Lba,
  IN UINTN    Bytes,
  IN VOID     *Buffer
  )
{
  return mBlockIo->ReadBlocks (mBlockIo, mBlockIo->Media->MediaId, Lba, Bytes, Buffer);
}

//
// A buffer aligned to a page, with room behind it
//
STATIC UINT8 *
NewBuffer (
  IN UINTN  Bytes
  )
{
  UINT8  *Buffer;

  Buffer = aligned_alloc (SIZE_4KB, ALIGN_VALUE (Bytes + SIZE_4KB, SIZE_4KB));
  if (Buffer == NULL) {
    printf ("out of memory\n");
    exit (2);
  }

  memset (Buffer, 0x5a, Bytes + SIZE_4KB);
  return Buffer;
}

// ---------------------------------------------------------------------------
// The environment
// ---------------------------------------------------------------------------

STATIC VOID
TestEnvRegisters (
  VOID
  )
{
  struct ans_env  *Env;
  UINTN           Before;

  NewWorld ();
  mFake = NULL;   // only what is asked is recorded
  Env   = &mAnsDisk.Env;
  Env->Region[ANS_RGN_NVME].Base = 0x4c5cc0000ULL;
  Env->Region[ANS_RGN_NVME].Size = 0x60000;
  Env->Region[ANS_RGN_PMGR].Base = 0x380700538ULL;
  Env->Region[ANS_RGN_PMGR].Size = 4;

  Expect (
    ans_env_read32 (Env, ANS_RGN_PMGR, 0) == 0x11223344 && mMmio == 1 && mLastMmioAddress == 0x380700538ULL && mLastMmioWidth == 4,
    "env: a 32-bit read of the 4-byte power state register reaches it"
    );
  Expect (
    ans_env_read64 (Env, ANS_RGN_NVME, 0x5fff8) == 0x1122334455667788ULL && mMmio == 2 && mLastMmioAddress == 0x4c5cc0000ULL + 0x5fff8 && mLastMmioWidth == 8,
    "env: a 64-bit read of the last 8 bytes of a region is one 64-bit access"
    );

  Before = mMmio;
  Expect (ans_env_read32 (Env, ANS_RGN_PMGR, 4) == MAX_UINT32, "env: a 32-bit read just past the end of a region reads all ones");
  Expect (ans_env_read64 (Env, ANS_RGN_PMGR, 0) == MAX_UINT64, "env: a 64-bit read of a 4-byte region reads all ones");
  Expect (ans_env_read64 (Env, ANS_RGN_NVME, 0x5fffc) == MAX_UINT64, "env: a 64-bit read across the end of a region reads all ones");
  Expect (ans_env_read32 (Env, ANS_RGN_NVME, 0x2) == MAX_UINT32, "env: a 32-bit read not aligned to 4 bytes reads all ones");
  Expect (ans_env_read32 (Env, ANS_RGN_NVME, 0xfffffffc) == MAX_UINT32, "env: an offset near 4 GiB does not wrap into the region");
  Expect (ans_env_read32 (Env, ANS_RGN_ASC, 0) == MAX_UINT32, "env: a region the device tree did not give is refused");
  Expect (ans_env_read32 (Env, ANS_RGN_COUNT, 0) == MAX_UINT32, "env: a region number past the last is refused");
  ans_env_write32 (Env, ANS_RGN_PMGR, 4, 1);
  ans_env_write64 (Env, ANS_RGN_NVME, 0x60000, 1);
  Expect (mMmio == Before, "env: none of the refused accesses reached the hardware");
  Expect (Env->Refused == 9, "env: each refused access is counted");
  Expect (Logged ("AnsBlockIoDxe: env: refused 4-byte access to region 3 offset 0x4"), "env: a refused access is logged");
}

STATIC VOID
TestEnvMemory (
  VOID
  )
{
  struct ans_env  *Env;
  struct ans_dma  Dma;
  struct ans_dma  Other;
  BOOLEAN         Zero;
  UINTN           Index;

  NewWorld ();
  Env = &mAnsDisk.Env;

  Expect (ans_env_dma_alloc (Env, 100, &Dma), "env: 100 bytes of device memory");
  Zero = TRUE;
  for (Index = 0; Index < SIZE_4KB; Index++) {
    Zero = Zero && ((UINT8 *)Dma.va)[Index] == 0;
  }

  Expect (Zero, "env: the whole page is zeroed (the library hands it out unzeroed)");
  Expect ((Dma.dva & (SIZE_16KB - 1)) == 0 && Dma.size == 100 && Dma.env_priv != NULL, "env: at a device address aligned to 16 KiB, mapped");
  Expect (mDma[0].Mapped, "env: the device address comes from DmaMap() as a common buffer");
  ans_env_dma_free (Env, &Dma);
  Expect (mFrees == 1 && !mDma[0].Mapped && Dma.va == NULL, "env: freeing unmaps and frees it");
  Expect (fake_live_env_blocks (mFake) == 0, "env: nothing is left allocated");

  mCommonBufferSkew = SIZE_4KB;
  Expect (!ans_env_dma_alloc (Env, SIZE_4KB, &Dma) && Dma.va == NULL, "env: memory whose device address is not aligned to 16 KiB is refused");
  Expect (fake_live_env_blocks (mFake) == 0 && mFreeWhileMapped == 0, "env: and is unmapped and freed again");
  mCommonBufferSkew = 0;

  mFailAlloc = TRUE;
  Expect (!ans_env_dma_alloc (Env, SIZE_4KB, &Dma), "env: an allocation the library fails fails");
  mFailAlloc = FALSE;

  Index = mAllocs;
  Expect (!ans_env_dma_alloc (Env, 0, &Dma), "env: 0 bytes are refused");
  Expect (!ans_env_dma_alloc (Env, ANS_ENV_DMA_MAX_SIZE + 1, &Dma), "env: more than 16 MiB is refused");
  Expect (mAllocs == Index, "env: neither reaches the library");

  Expect (ans_env_dma_alloc (Env, SIZE_8KB, &Dma), "env: 8 KiB of device memory");
  Env->Exiting = TRUE;
  Index        = mAllocs;
  Expect (!ans_env_dma_alloc (Env, SIZE_4KB, &Other) && mAllocs == Index, "env: inside ExitBootServices nothing is allocated");

  Index = mFrees;
  ans_env_dma_free (Env, &Dma);
  Expect (mFrees == Index && Dma.va == NULL, "env: inside ExitBootServices freeing leaves the memory as it is");
  Expect (fake_live_env_blocks (mFake) == 1, "env: it stays allocated, for the operating system to take over");
}

STATIC VOID
TestEnvLog (
  VOID
  )
{
  struct ans_env  *Env;

  NewWorld ();
  Env = &mAnsDisk.Env;

  ans_env_log (
    Env,
    "nvme: %s %d %u %x %02x %08x %llu %llx %016llx end",
    "abc",
    -5,
    7u,
    0xabu,
    5u,
    0x1234u,
    123456789012ULL,
    0xdeadbeefcafeULL,
    0x12ULL
    );
  Expect (
    Logged ("AnsBlockIoDxe: nvme: abc -5 7 ab 05 00001234 123456789012 deadbeefcafe 0000000000000012 end"),
    "env: every conversion the core uses comes out as in C, %s as an ASCII string"
    );

  ans_env_log (Env, "nvme: " X100 X100 X100 " %u", 1u);
  Expect (Logged ("AnsBlockIoDxe: a line of the core's log is too long to print"), "env: a format too long for the line is not cut off silently");

  Expect (ans_env_wait (Env, 0) == 1 && ans_env_wait (Env, 50) == 50 && mDelayed == 51, "env: a wait spins as long as asked, at least 1 us, and says so");
}

// ---------------------------------------------------------------------------
// The block device
// ---------------------------------------------------------------------------

STATIC VOID
TestNoController (
  VOID
  )
{
  TEST_MACHINE  Machine;
  TEST_ANS      Ans;

  NewWorld ();
  Expect (Boot (&mM4MacBookPro) == EFI_NOT_FOUND, "a tree without /arm-io/ans: not found");
  Expect (mInstalls == 0 && mEventsCreated == 0 && mMmio == 0 && mAllocs == 0, "  nothing published, no register touched, nothing allocated");
  Expect (Logged ("no internal disk: /arm-io/ans or /arm-io/sart-ans in the device tree cannot be read: Not Found"), "  the log says why");

  NewWorld ();
  Machine      = mM4MacBookProWithAns;
  Machine.Pmgr = NULL;
  Expect (Boot (&Machine) == EFI_NOT_FOUND && mInstalls == 0 && mMmio == 0, "a tree without the power manager: not found, nothing touched");

  NewWorld ();
  Ans                    = mAnsOfM4MacBookPro;
  Ans.Omit              |= OMIT_SART_VERSION;
  Ans.SartCompatible     = NULL;
  Ans.SartCompatibleLength = 0;
  Machine                = mM4MacBookProWithAns;
  Machine.Ans            = &Ans;
  Expect (Boot (&Machine) == EFI_NOT_FOUND && mInstalls == 0 && mMmio == 0, "a SART without a version: not found, nothing touched");
}

STATIC VOID
TestStart (
  VOID
  )
{
  STATIC CONST UINT8  ExpectedPath[] = {
    0x01, 0x04, 0x14, 0x00,                             // VenHw
    0x08, 0x68, 0x20, 0x28, 0xa0, 0xfe, 0xd8, 0x41,
    0xb2, 0x9b, 0x5c, 0x34, 0x77, 0x3e, 0xaa, 0x21,
    0x03, 0x17, 0x10, 0x00,                             // NVMe
    0x01, 0x00, 0x00, 0x00,                             // namespace 1
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // EUI-64 0
    0x7f, 0xff, 0x04, 0x00                              // end
  };
  UINT8  FirstPath[sizeof (ExpectedPath)];

  NewWorld ();
  Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "the M4's controller comes up");
  if (!Expect (mInstalls == 1 && mBlockIo != NULL && mDevicePath != NULL, "  the block device and its device path are published")) {
    CheckClean ("first boot");
    return;
  }

  Expect (
    mEventsCreated == 1 && mExitType == EVT_NOTIFY_SIGNAL && mExitTpl == TPL_NOTIFY && mExitGroupRight && mExitContext == &mAnsDisk,
    "  with a notification for ExitBootServices"
    );
  Expect (mFake->cp == CP_RUNNING && mFake->iocq_made && mFake->iosq_made, "  the fake's coprocessor runs, the I/O queues are made");
  Expect (mAnsDisk.Core.config.ioq_registers && mAnsDisk.Core.config.sart_version == SART_VERSION, "  with nvme-secure-bar and the SART version of the tree");
  Expect (
    mBlockIo->Media->BlockSize == 4096 && mBlockIo->Media->LastBlock == mFake->nsze - 1 && mBlockIo->Media->MediaPresent &&
    mBlockIo->Media->ReadOnly && !mBlockIo->Media->RemovableMedia && !mBlockIo->Media->LogicalPartition &&
    mBlockIo->Media->IoAlign == 4 && mBlockIo->Revision == EFI_BLOCK_IO_PROTOCOL_REVISION2,
    "  the media: the namespace's blocks, read-only, fixed, buffers aligned to 4 bytes"
    );
  Expect (memcmp (mDevicePath, ExpectedPath, sizeof (ExpectedPath)) == 0, "  the device path is VenHw(28206808-...)/NVMe(0x1,0-0-0-0-0-0-0-0)");
  memcpy (FirstPath, mDevicePath, sizeof (FirstPath));
  CheckClean ("first boot");

  NewWorld ();
  mFake->nsze = 1000;
  Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS && mBlockIo->Media->LastBlock == 999, "a second boot, with another disk");
  Expect (mDevicePath != NULL && memcmp (mDevicePath, FirstPath, sizeof (FirstPath)) == 0, "  the device path is the same");
  CheckClean ("second boot");
}

STATIC VOID
TestStartFails (
  VOID
  )
{
  NewWorld ();
  mFake->silent = true;
  Expect (Boot (&mM4MacBookProWithAns) == EFI_DEVICE_ERROR, "a coprocessor that never answers: the controller does not come up");
  Expect (mInstalls == 0 && mEventsCreated == 0, "  nothing is published");
  Expect (mFake->run_sets == 1 && ans_get_state (&mAnsDisk.Core) == ANS_FAILED, "  started once, not tried again");
  Expect (fake_live_env_blocks (mFake) == 0, "  its memory is given back");
  Expect (Logged ("AnsBlockIoDxe: no internal disk: the controller did not come up"), "  the log says why");
  CheckClean ("silent coprocessor");

  NewWorld ();
  mFake->mdts = 5;    // 128 KiB per command
  Expect (Boot (&mM4MacBookProWithAns) == EFI_DEVICE_ERROR && mInstalls == 0, "a controller that takes less than 252 KiB a read: not published");
  Expect (mFake->cp == CP_OFF && fake_live_env_blocks (mFake) == 0, "  brought down again, its memory given back");
  CheckClean ("small MDTS");

  NewWorld ();
  mFailAlloc = TRUE;
  Expect (Boot (&mM4MacBookProWithAns) == EFI_DEVICE_ERROR && mInstalls == 0, "no device memory: not published");
  CheckClean ("no device memory");
}

STATIC VOID
TestPublishFails (
  VOID
  )
{
  NewWorld ();
  mFailCreateEvent = TRUE;
  Expect (Boot (&mM4MacBookProWithAns) == EFI_OUT_OF_RESOURCES && mInstalls == 0, "no ExitBootServices event: not published");
  Expect (mFake->cp == CP_OFF && ans_get_state (&mAnsDisk.Core) == ANS_OFF && fake_live_env_blocks (mFake) == 0, "  the controller is brought down, its memory given back");
  CheckClean ("no event");

  NewWorld ();
  mFailInstall = TRUE;
  Expect (Boot (&mM4MacBookProWithAns) == EFI_OUT_OF_RESOURCES && mInstalls == 0, "the protocols cannot be installed: not published");
  Expect (mEventsCreated == 1 && mEventsClosed == 1, "  the ExitBootServices event is closed again");
  Expect (mFake->cp == CP_OFF && fake_live_env_blocks (mFake) == 0, "  the controller is brought down, its memory given back");
  CheckClean ("no protocols");
}

STATIC VOID
TestReads (
  VOID
  )
{
  UINT8    *Buffer;
  UINTN    Bytes;
  EFI_LBA  Last;
  UINT32   Reads;
  UINTN    Maps;

  NewWorld ();
  if (!Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "reads: the controller comes up")) {
    return;
  }

  Last   = mBlockIo->Media->LastBlock;
  Buffer = NewBuffer (SIZE_4MB);

  mWmb = 0;
  mRmb = 0;
  Expect (Read (0, SIZE_4KB, Buffer) == EFI_SUCCESS && DataOk (Buffer, 0, SIZE_4KB, 4096), "one block at 0");
  Expect (mWmb > 0 && mRmb > 0, "  the core goes through the environment's barriers");
  Expect (IoReads () == 1 && mMaps == 1 && mUnmaps == 1, "  one command, its buffer mapped once and unmapped");
  Expect (mTplAtMap == TPL_CALLBACK && mTpl == TPL_APPLICATION, "  at TPL_CALLBACK, and back after");
  Expect (Buffer[SIZE_4KB] == 0x5a, "  nothing written past the buffer");

  Bytes = (3 * BLOCKS_PER_READ + 2) * SIZE_4KB;
  Reads = IoReads ();
  Expect (Read (12345, Bytes, Buffer) == EFI_SUCCESS && DataOk (Buffer, 12345, Bytes, 4096), "3 x 63 + 2 blocks at 12345");
  Expect (IoReads () - Reads == 4 && mMaps == 5 && mUnmaps == 5, "  four commands, each mapped and unmapped");
  Expect (mFake->cmds[mFake->ncmds - 1].cdw10 == 12345 + 3 * BLOCKS_PER_READ, "  the last of them at the right block");
  Expect (Buffer[Bytes] == 0x5a, "  nothing written past the buffer");

  Expect (Read (Last, SIZE_4KB, Buffer) == EFI_SUCCESS && DataOk (Buffer, Last, SIZE_4KB, 4096), "the last block");
  Expect (Read (Last - 1, 2 * SIZE_4KB, Buffer) == EFI_SUCCESS && DataOk (Buffer, Last - 1, 2 * SIZE_4KB, 4096), "the last two blocks");

  Reads = IoReads ();
  Maps  = mMaps;
  Expect (Read (Last + 1, SIZE_4KB, Buffer) == EFI_INVALID_PARAMETER, "a block past the end: invalid parameter");
  Expect (Read (Last, 2 * SIZE_4KB, Buffer) == EFI_INVALID_PARAMETER, "two blocks across the end: invalid parameter");
  Expect (Read (MAX_UINT64, SIZE_4KB, Buffer) == EFI_INVALID_PARAMETER, "block 2^64-1: invalid parameter");
  Expect (Read (1, MAX_UINTN - SIZE_4KB + 1, Buffer) == EFI_INVALID_PARAMETER, "2^52 - 1 blocks: invalid parameter");
  Expect (Read (0, SIZE_4KB + 512, Buffer) == EFI_BAD_BUFFER_SIZE, "a length that is not whole blocks: bad buffer size");
  Expect (Read (0, SIZE_4KB, Buffer + 2) == EFI_INVALID_PARAMETER, "a buffer not aligned to 4 bytes: invalid parameter");
  Expect (Read (0, SIZE_4KB, NULL) == EFI_INVALID_PARAMETER, "no buffer: invalid parameter");
  Expect (mBlockIo->ReadBlocks (mBlockIo, 1, 0, SIZE_4KB, Buffer) == EFI_MEDIA_CHANGED, "another media ID: media changed");
  Expect (Read (0, 0, Buffer) == EFI_SUCCESS, "0 bytes: success");
  Expect (IoReads () == Reads && mMaps == Maps, "  none of these reached the controller or mapped memory");

  Expect (Read (7, 3 * SIZE_4KB, Buffer + 4) == EFI_SUCCESS && DataOk (Buffer + 4, 7, 3 * SIZE_4KB, 4096), "a buffer aligned to 4 bytes but not to a page");

  mShortenMaps = TRUE;
  Reads        = IoReads ();
  Bytes        = 2 * BLOCKS_PER_READ * SIZE_4KB;
  Expect (Read (100, Bytes, Buffer) == EFI_SUCCESS && DataOk (Buffer, 100, Bytes, 4096), "DmaMap() mapping about half of each piece");
  Expect (IoReads () - Reads > 2 && mMaps == mUnmaps, "  more commands, each of whole blocks, all unmapped");
  mShortenMaps = FALSE;

  Expect (mBlockIo->FlushBlocks (mBlockIo) == EFI_SUCCESS && mBlockIo->Reset (mBlockIo, TRUE) == EFI_SUCCESS, "flush and reset do nothing and succeed");

  CheckClean ("reads");
  free (Buffer);
}

STATIC VOID
TestReadsSmallBlocks (
  VOID
  )
{
  UINT8   *Buffer;
  UINTN   Bytes;
  UINT32  Reads;

  NewWorld ();
  mFake->lbads = 9;
  mFake->nsze  = 4096;
  if (!Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS && mBlockIo->Media->BlockSize == 512, "512-byte blocks: the controller comes up")) {
    return;
  }

  Buffer = NewBuffer (SIZE_1MB);
  Bytes  = 1000 * 512;
  Reads  = IoReads ();
  Expect (Read (3000, Bytes, Buffer) == EFI_SUCCESS && DataOk (Buffer, 3000, Bytes, 512), "1000 blocks of 512 bytes");
  Expect (IoReads () - Reads == 2, "  two commands of at most 504 blocks");
  Expect (Read (4095, 512, Buffer) == EFI_SUCCESS && DataOk (Buffer, 4095, 512, 512), "the last block");
  Expect (Read (4095, 1024, Buffer) == EFI_INVALID_PARAMETER, "two blocks across the end: invalid parameter");
  Expect (Read (0, 4096 + 256, Buffer) == EFI_BAD_BUFFER_SIZE, "half a block more: bad buffer size");
  CheckClean ("512-byte blocks");
  free (Buffer);
}

STATIC VOID
TestReadErrors (
  VOID
  )
{
  UINT8   *Buffer;
  UINT64  Before;
  UINTN   Maps;

  Buffer = NewBuffer (SIZE_64KB);

  NewWorld ();
  if (Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "read errors: the controller comes up")) {
    mFake->fail_read_status = 0x281;    // unrecovered read error
    Expect (Read (5, SIZE_4KB, Buffer) == EFI_DEVICE_ERROR, "a read the controller fails: device error");
    Expect (mMaps == 1 && mUnmaps == 1, "  its buffer is unmapped");
    Expect (Logged ("NVMe status 0x281"), "  the log has the NVMe status");
    mFake->fail_read_status = 0;
    Expect (Read (5, SIZE_4KB, Buffer) == EFI_SUCCESS && DataOk (Buffer, 5, SIZE_4KB, 4096), "  the next read works");
    CheckClean ("failed read");
  }

  NewWorld ();
  if (Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "a read that never completes: the controller comes up")) {
    mFake->hang_reads = true;
    Before            = mDelayed;
    Expect (Read (5, SIZE_4KB, Buffer) == EFI_DEVICE_ERROR, "a read that never completes: device error");
    Expect (mDelayed - Before >= ANS_DISK_READ_TIMEOUT_US && mDelayed - Before < ANS_DISK_READ_TIMEOUT_US + 1000, "  after 1 s");
    Expect (ans_get_state (&mAnsDisk.Core) == ANS_FAILED && (mFake->cpu_control & 0x10) == 0, "  the controller is stopped");
    Expect (mUnmaps == 1, "  then the buffer is unmapped");
    Maps = mMaps;
    Expect (Read (5, SIZE_4KB, Buffer) == EFI_DEVICE_ERROR && mMaps == Maps, "  later reads fail at once, nothing mapped");
    CheckClean ("read that never completes");

    ExitBootServices ();
    Expect (ans_get_state (&mAnsDisk.Core) == ANS_FAILED && mCallsInExit == 0, "  ExitBootServices after it: nothing allocated or freed");
    CheckClean ("ExitBootServices after a failed controller");
  }

  NewWorld ();
  if (Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "a coprocessor that crashes: the controller comes up")) {
    mFake->crash_now  = true;
    mFake->hang_reads = true;
    Before            = mDelayed;
    Expect (Read (5, SIZE_4KB, Buffer) == EFI_DEVICE_ERROR, "a coprocessor that crashes during a read: device error");
    Expect (mDelayed - Before < ANS_DISK_READ_TIMEOUT_US && ans_get_state (&mAnsDisk.Core) == ANS_CRASHED, "  at once, not after the timeout");
    Expect (mUnmaps == mMaps, "  the buffer is unmapped");
    CheckClean ("crash during a read");
  }

  free (Buffer);
}

STATIC VOID
TestWrites (
  VOID
  )
{
  UINT8  *Buffer;

  NewWorld ();
  if (!Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "writes: the controller comes up")) {
    return;
  }

  Buffer = NewBuffer (SIZE_8KB);
  Expect (mBlockIo->WriteBlocks (mBlockIo, 0, 0, SIZE_4KB, Buffer) == EFI_WRITE_PROTECTED, "a write: write-protected");
  Expect (mBlockIo->WriteBlocks (mBlockIo, 0, MAX_UINT64, 3, NULL) == EFI_WRITE_PROTECTED, "a write with every argument wrong: write-protected");
  Expect (mMaps == 0 && IoReads () == 0, "  nothing mapped, no command");
  CheckClean ("writes");
  free (Buffer);
}

STATIC VOID
TestExitBootServices (
  VOID
  )
{
  UINT8  *Buffer;
  UINTN  Maps;

  NewWorld ();
  if (!Expect (Boot (&mM4MacBookProWithAns) == EFI_SUCCESS, "ExitBootServices: the controller comes up")) {
    return;
  }

  Buffer = NewBuffer (SIZE_64KB);
  Expect (Read (0, SIZE_64KB, Buffer) == EFI_SUCCESS, "  a read before it");

  ExitBootServices ();
  Expect (ans_get_state (&mAnsDisk.Core) == ANS_OFF && mFake->cp == CP_OFF, "ExitBootServices: the controller is brought down cleanly");
  Expect (mCallsInExit == 0, "  nothing allocated, freed, mapped or unmapped inside it");
  Expect (fake_live_env_blocks (mFake) > 0, "  the device memory is left to the operating system");
  Expect (mDelayed > 0 && Logged ("AnsBlockIoDxe: ExitBootServices, bringing the disk controller down"), "  it waited on the timer, and says what it does");

  Maps = mMaps;
  Expect (Read (0, SIZE_4KB, Buffer) == EFI_DEVICE_ERROR && mMaps == Maps, "a read after it fails, nothing mapped");
  CheckClean ("ExitBootServices");
  free (Buffer);
}

int
main (
  int   argc,
  char  **argv
  )
{
  mVerbose = (argc > 1) && (strcmp (argv[1], "-v") == 0);

  TestEnvRegisters ();
  TestEnvMemory ();
  TestEnvLog ();
  TestNoController ();
  TestStart ();
  TestStartFails ();
  TestPublishFails ();
  TestReads ();
  TestReadsSmallBlocks ();
  TestReadErrors ();
  TestWrites ();
  TestExitBootServices ();
  return Verdict ();
}
