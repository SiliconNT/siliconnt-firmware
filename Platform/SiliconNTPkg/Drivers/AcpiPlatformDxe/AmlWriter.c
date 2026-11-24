/** @file
  Writes ACPI Machine Language. See AmlWriter.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AmlWriter.h"

#include <IndustryStandard/AcpiAml.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define MAX_PKG_LENGTH_BYTES  4

VOID
AmlPutBytes (
  IN OUT AML_WRITER  *Writer,
  IN     CONST VOID  *Data,
  IN     UINTN       Size
  )
{
  if (Writer->Buffer != NULL) {
    CopyMem (Writer->Buffer + Writer->Used, Data, Size);
  }

  Writer->Used += Size;
}

VOID
AmlPutByte (
  IN OUT AML_WRITER  *Writer,
  IN     UINT8       Value
  )
{
  AmlPutBytes (Writer, &Value, sizeof (Value));
}

VOID
AmlPutNameSeg (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *Name
  )
{
  AmlPutBytes (Writer, Name, 4);
}

VOID
AmlPutName (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *Name
  )
{
  AmlPutByte (Writer, AML_NAME_OP);
  AmlPutNameSeg (Writer, Name);
}

VOID
AmlPutInteger (
  IN OUT AML_WRITER  *Writer,
  IN     UINT32      Value
  )
{
  if (Value <= 1) {
    AmlPutByte (Writer, Value == 0 ? AML_ZERO_OP : AML_ONE_OP);
  } else if (Value <= MAX_UINT8) {
    AmlPutByte (Writer, AML_BYTE_PREFIX);
    AmlPutBytes (Writer, &Value, 1);    // little-endian, as all of them
  } else if (Value <= MAX_UINT16) {
    AmlPutByte (Writer, AML_WORD_PREFIX);
    AmlPutBytes (Writer, &Value, 2);
  } else {
    AmlPutByte (Writer, AML_DWORD_PREFIX);
    AmlPutBytes (Writer, &Value, 4);
  }
}

VOID
AmlPutString (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *String
  )
{
  AmlPutByte (Writer, AML_STRING_PREFIX);
  AmlPutBytes (Writer, String, AsciiStrLen (String) + 1);
}

UINTN
AmlBegin (
  IN CONST AML_WRITER  *Writer
  )
{
  return Writer->Used;
}

//
// A PkgLength counts its own bytes (20.2.4). How many those are for ContentSize
// bytes after them: one up to 63 in all, two up to 0xFFF, three up to 0xFFFFF.
//
STATIC
UINTN
PkgLengthBytes (
  IN UINTN  ContentSize
  )
{
  UINTN  Bytes;

  if (ContentSize + 1 <= 0x3F) {
    return 1;
  }

  for (Bytes = 2; Bytes < MAX_PKG_LENGTH_BYTES; Bytes++) {
    if (ContentSize + Bytes < (UINTN)1 << (4 + 8 * (Bytes - 1))) {
      break;
    }
  }

  return Bytes;
}

VOID
AmlEnd (
  IN OUT AML_WRITER  *Writer,
  IN     UINTN       Start
  )
{
  UINTN  ContentSize = Writer->Used - Start;
  UINTN  Bytes       = PkgLengthBytes (ContentSize);
  UINTN  Length      = ContentSize + Bytes;
  UINT8  *At;
  UINTN  Index;

  Writer->Used += Bytes;
  if (Writer->Buffer == NULL) {
    return;
  }

  At = Writer->Buffer + Start;
  CopyMem (At + Bytes, At, ContentSize);    // the two overlap; CopyMem() allows it

  if (Bytes == 1) {
    At[0] = (UINT8)Length;
    return;
  }

  //
  // Lead byte: the number of bytes that follow in bits 6-7, the low 4 bits of the
  // length in bits 0-3. Each byte that follows holds the next 8 bits.
  //
  At[0] = (UINT8)(((Bytes - 1) << 6) | (Length & 0x0F));
  for (Index = 1; Index < Bytes; Index++) {
    At[Index] = (UINT8)(Length >> (4 + 8 * (Index - 1)));
  }
}
