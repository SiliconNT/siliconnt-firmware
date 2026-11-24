/** @file
  Writes ACPI Machine Language (ACPI 6.6, chapter 20) into a buffer of the caller,
  in the encodings the ASL compiler chooses. The opcodes are in
  IndustryStandard/AcpiAml.h.

  A writer without a buffer counts: the same calls give the size of what they would
  write. So one function of a user both sizes and writes its table.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef AML_WRITER_H_
#define AML_WRITER_H_

#include <Base.h>

typedef struct {
  UINT8    *Buffer;   // NULL: count only
  UINTN    Used;      // bytes written, or counted
} AML_WRITER;

VOID
AmlPutByte (
  IN OUT AML_WRITER  *Writer,
  IN     UINT8       Value
  );

VOID
AmlPutBytes (
  IN OUT AML_WRITER  *Writer,
  IN     CONST VOID  *Data,
  IN     UINTN       Size
  );

// A NameSeg: the four characters of Name
VOID
AmlPutNameSeg (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *Name
  );

// Name (XXXX, up to the object
VOID
AmlPutName (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *Name
  );

// The shortest form: Zero, One, or a prefix and 1, 2 or 4 bytes (20.2.3, ComputationalData)
VOID
AmlPutInteger (
  IN OUT AML_WRITER  *Writer,
  IN     UINT32      Value
  );

// A string of characters 0x01-0x7F, zero-terminated
VOID
AmlPutString (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *String
  );

/**
  What has a PkgLength goes between these two, after its opcode: the PkgLength
  itself is put in front of it by AmlEnd(), which knows the length. They nest. What
  is between them must not be longer than a PkgLength of four bytes can say,
  0x0FFFFFFF with the four.

  @return  What AmlEnd() wants to be told.
**/
UINTN
AmlBegin (
  IN CONST AML_WRITER  *Writer
  );

VOID
AmlEnd (
  IN OUT AML_WRITER  *Writer,
  IN     UINTN       Start
  );

#endif // AML_WRITER_H_
