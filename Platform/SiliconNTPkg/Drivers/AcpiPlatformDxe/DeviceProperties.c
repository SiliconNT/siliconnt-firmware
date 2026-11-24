/** @file
  The properties of a device's _DSD, copied from the Apple device tree. See
  DeviceProperties.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "DeviceProperties.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define STREAM_KEY  "iommu-stream"

//
// Text: each of these adds to what is there and returns FALSE, with part of it
// added, when there is no room for all. Nothing is ended by a zero but by EndText().
//

STATIC
BOOLEAN
AddText (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     CONST CHAR8        *Text
  )
{
  for ( ; *Text != 0; Text++) {
    if (Properties->TextUsed == DEVICE_TEXT_SIZE) {
      return FALSE;
    }

    Properties->Text[Properties->TextUsed++] = *Text;
  }

  return TRUE;
}

STATIC
BOOLEAN
AddNumber (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     UINTN              Number
  )
{
  CHAR8  Digit[2] = { (CHAR8)('0' + Number % 10), 0 };

  return (Number < 10 || AddNumber (Properties, Number / 10)) && AddText (Properties, Digit);
}

STATIC
BOOLEAN
EndText (
  IN OUT DEVICE_PROPERTIES  *Properties
  )
{
  if (Properties->TextUsed == DEVICE_TEXT_SIZE) {
    return FALSE;
  }

  Properties->Text[Properties->TextUsed++] = 0;
  return TRUE;
}

BOOLEAN
AddName (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     CONST CHAR8        *Block,
  IN     UINT32             Entry
  )
{
  return AddText (Properties, Block) &&
         (Entry == APPLE_DEVICE_NO_ENTRY || (AddText (Properties, "-") && AddNumber (Properties, Entry))) &&
         EndText (Properties);
}

BOOLEAN
AddNames (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     CONST CHAR8        *Key,
  IN     CONST CHAR8        *First,
  IN     UINTN              Count
  )
{
  DSDT_PROPERTY  *Added = &Properties->Properties[Properties->Count];

  if (Properties->Count == DSDT_MAX_ELEMENTS || Count > DSDT_MAX_ELEMENTS) {
    return FALSE;
  }

  ZeroMem (Added, sizeof (*Added));
  Added->Key         = Key;
  Added->Kind        = DsdtPackage;
  Added->Strings     = First;
  Added->StringCount = Count;
  Properties->Count++;
  return TRUE;
}

//
// How many strings a value holds: each ended by a zero, of the characters an AML
// string may have, none of them empty; more zeros at the end do not count. 0 when
// it is not that.
//
STATIC
UINTN
CountStrings (
  IN CONST UINT8  *Value,
  IN UINTN        Length
  )
{
  UINTN  End = Length;
  UINTN  Count;
  UINTN  Index;

  while (End > 0 && Value[End - 1] == 0) {
    End--;
  }

  if (End == 0 || End == Length) {
    return 0;
  }

  Count = 1;
  for (Index = 0; Index < End; Index++) {
    if (Value[Index] > 0x7F || (Value[Index] == 0 && (Index == 0 || Value[Index - 1] == 0))) {
      return 0;
    }

    if (Value[Index] == 0) {
      Count++;
    }
  }

  return Count;
}

// The value of a property that is one string and not an empty one; NULL when it is not there or not that
STATIC
CONST CHAR8 *
ReadString (
  IN dt_node_t    *Node,
  IN CONST CHAR8  *Name
  )
{
  size_t  Length = 0;
  VOID    *Value = dt_prop (Node, Name, &Length);

  return Value != NULL && CountStrings (Value, Length) == 1 ? Value : NULL;
}

typedef struct {
  UINT32       Phandle;
  dt_node_t    *Node;
} PHANDLE_WALK;

STATIC
int
PhandleWalkProperty (
  void        *a,
  dt_node_t   *Node,
  int         Depth,
  const char  *Key,
  void        *Value,
  size_t      Length
  )
{
  PHANDLE_WALK  *Walk = a;
  UINT32        Phandle;

  if (Length != sizeof (Phandle) || AsciiStrnCmp (Key, "AAPL,phandle", DT_KEY_LEN) != 0) {
    return 0;
  }

  CopyMem (&Phandle, Value, sizeof (Phandle));
  if (Phandle != Walk->Phandle) {
    return 0;
  }

  Walk->Node = Node;
  return 1;
}

// The node the phandle at the start of Value is of; NULL when there is none
STATIC
dt_node_t *
FindPhandle (
  IN dt_node_t    *Tree,
  IN CONST UINT8  *Value
  )
{
  PHANDLE_WALK  Walk = { 0, NULL };

  CopyMem (&Walk.Phandle, Value, sizeof (Walk.Phandle));
  dt_parse (Tree, 0, NULL, NULL, NULL, &PhandleWalkProperty, &Walk);
  return Walk.Node;
}

typedef struct {
  UINTN        Left;    // how many children to pass over
  dt_node_t    *Child;
} CHILD_WALK;

STATIC
int
ChildWalkNode (
  void       *a,
  dt_node_t  *Node,
  int        Depth
  )
{
  CHILD_WALK  *Walk = a;

  if (Depth != 1 || Walk->Left-- != 0) {
    return 0;
  }

  Walk->Child = Node;
  return 1;
}

// Child number Index of a node, from 0; NULL when it has no more
STATIC
dt_node_t *
GetChild (
  IN dt_node_t  *Node,
  IN UINTN      Index
  )
{
  CHILD_WALK  Walk = { Index, NULL };

  dt_parse (Node, 0, NULL, &ChildWalkNode, &Walk, NULL, NULL);
  return Walk.Child;
}

//
// What became of a property
//
typedef enum {
  Copied,
  Nothing,            // there is nothing to say
  SkippedValue,
  SkippedReference,
  SkippedRoom
} COPY_OUTCOME;

// The names of the children, as many as are strings
STATIC
COPY_OUTCOME
ReadChildren (
  IN     dt_node_t          *Node,
  IN OUT DEVICE_PROPERTIES  *Properties,
  OUT    DSDT_PROPERTY      *Property
  )
{
  UINTN        Index;
  dt_node_t    *Child;
  CONST CHAR8  *Name;

  Property->Kind    = DsdtPackage;
  Property->Strings = Properties->Text + Properties->TextUsed;

  for (Index = 0; (Child = GetChild (Node, Index)) != NULL; Index++) {
    Name = ReadString (Child, "name");
    if (Name == NULL) {
      continue;
    }

    if (Property->StringCount == DSDT_MAX_ELEMENTS || !AddText (Properties, Name) || !EndText (Properties)) {
      return SkippedRoom;
    }

    Property->StringCount++;
  }

  return Property->StringCount != 0 ? Copied : Nothing;
}

// A value of the tree as a property but for the key. WordCount may be more than a package holds.
STATIC
COPY_OUTCOME
ReadValue (
  IN  dt_node_t      *Tree,
  IN  PROPERTY_TYPE  Type,
  IN  CONST UINT8    *Value,
  IN  UINTN          Length,
  OUT DSDT_PROPERTY  *Property
  )
{
  dt_node_t  *Target;
  size_t     TargetLength = 0;
  VOID       *TargetValue;

  switch (Type) {
    case PropertyInteger:
      if (Length != sizeof (UINT32)) {
        return SkippedValue;
      }

      Property->Kind = DsdtInteger;
      CopyMem (&Property->Integer, Value, sizeof (UINT32));
      return Copied;

    case PropertyPresent:
      if (Length != 0) {
        return SkippedValue;
      }

      Property->Kind    = DsdtInteger;
      Property->Integer = 1;
      return Copied;

    case PropertyString:
      // An empty string is one too: nothing but zeros, at least one
      if (CountStrings (Value, Length) != 1 && (Length == 0 || Value[0] != 0 || CountStrings (Value, Length) != 0)) {
        return SkippedValue;
      }

      Property->Kind        = DsdtString;
      Property->Strings     = (CONST CHAR8 *)Value;
      Property->StringCount = 1;
      return Copied;

    case PropertyStrings:
      Property->Kind        = DsdtPackage;
      Property->Strings     = (CONST CHAR8 *)Value;
      Property->StringCount = CountStrings (Value, Length);
      return Property->StringCount != 0 && Property->StringCount <= DSDT_MAX_ELEMENTS ? Copied : SkippedValue;

    case PropertyList:
      if (Length == 0) {
        return Nothing;
      }

    // fall through
    case PropertyWords:
      if (Length == 0 || Length % sizeof (UINT32) != 0) {
        return SkippedValue;
      }

      Property->Kind      = DsdtPackage;
      Property->Words     = Value;
      Property->WordCount = Length / sizeof (UINT32);
      return Copied;

    case PropertyFunction:
      if (Length % sizeof (UINT32) != 0 || Length < 2 * sizeof (UINT32) || Length / sizeof (UINT32) > DSDT_MAX_ELEMENTS) {
        return SkippedValue;
      }

      Target = FindPhandle (Tree, Value);
      if (Target == NULL) {
        return SkippedReference;
      }

      Property->Kind        = DsdtPackage;
      Property->Strings     = ReadString (Target, "name");
      Property->StringCount = 1;
      Property->Words       = Value + sizeof (UINT32);
      Property->WordCount   = Length / sizeof (UINT32) - 1;
      return Property->Strings != NULL ? Copied : SkippedReference;

    case PropertyStream:
      if (Length != sizeof (UINT32)) {
        return SkippedValue;
      }

      Target      = FindPhandle (Tree, Value);
      TargetValue = Target != NULL ? dt_prop (Target, "reg", &TargetLength) : NULL;
      if (TargetValue == NULL || TargetLength != sizeof (UINT32)) {
        return SkippedReference;
      }

      Property->Kind = DsdtInteger;
      CopyMem (&Property->Integer, TargetValue, sizeof (UINT32));
      return Copied;

    case PropertyChildren:    // not a value of the tree: ReadChildren()
      break;
  }

  return Nothing;
}

// "NodeName/Name", with Part not 0 "NodeName/Name+Part", in Text; NULL when there is no room
STATIC
CONST CHAR8 *
MakeKey (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     CONST CHAR8        *NodeName,
  IN     CONST CHAR8        *Name,
  IN     UINTN              Part
  )
{
  UINTN  Start = Properties->TextUsed;

  if (AddText (Properties, NodeName) && AddText (Properties, "/") && AddText (Properties, Name) &&
      (Part == 0 || (AddText (Properties, "+") && AddNumber (Properties, Part))) &&
      EndText (Properties))
  {
    return Properties->Text + Start;
  }

  Properties->TextUsed = Start;
  return NULL;
}

//
// Adds a property whose list of words may be longer than a package; it is added
// whole or not at all
//
STATIC
COPY_OUTCOME
AddProperty (
  IN OUT DEVICE_PROPERTIES    *Properties,
  IN     CONST CHAR8          *NodeName,
  IN     CONST CHAR8          *Name,
  IN     CONST DSDT_PROPERTY  *Property
  )
{
  UINTN          Parts = Property->WordCount == 0 ? 1 : (Property->WordCount + Property->StringCount + DSDT_MAX_ELEMENTS - 1) / DSDT_MAX_ELEMENTS;
  UINTN          First = Properties->Count;
  UINTN          Part;
  DSDT_PROPERTY  *Added;

  if (Parts > DSDT_MAX_ELEMENTS - Properties->Count) {
    return SkippedRoom;
  }

  for (Part = 0; Part < Parts; Part++) {
    Added      = &Properties->Properties[Properties->Count];
    *Added     = *Property;
    Added->Key = MakeKey (Properties, NodeName, Name, Part);
    if (Added->Key == NULL) {
      Properties->Count = First;
      return SkippedRoom;
    }

    if (Parts > 1) {
      Added->Words    += Part * DSDT_MAX_ELEMENTS * sizeof (UINT32);
      Added->WordCount = MIN (DSDT_MAX_ELEMENTS, Property->WordCount - Part * DSDT_MAX_ELEMENTS);
    }

    Properties->Count++;
  }

  return Copied;
}

STATIC
VOID
ListSkipped (
  IN OUT DEVICE_PROPERTIES        *Properties,
  IN     CONST CHAR8              *NodeName,
  IN     CONST CHAR8              *Name,
  IN     UINTN                    Length,
  IN     PROPERTY_SKIPPED_REASON  Reason
  )
{
  SKIPPED_PROPERTY  *Skipped;

  if (Properties->SkippedCount < DEVICE_MAX_SKIPPED) {
    Skipped         = &Properties->Skipped[Properties->SkippedCount];
    Skipped->Key    = MakeKey (Properties, NodeName, Name, 0);
    Skipped->Length = Length;
    Skipped->Reason = Reason;
    if (Skipped->Key == NULL) {
      Skipped->Key = "(no room for the name)";
    }
  }

  Properties->SkippedCount++;
}

// The property of Node that the tree calls Name
STATIC
VOID
CopyProperty (
  IN     dt_node_t          *Tree,
  IN     dt_node_t          *Node,
  IN     CONST CHAR8        *NodeName,
  IN     CONST CHAR8        *Name,
  IN     PROPERTY_TYPE      Type,
  IN OUT DEVICE_PROPERTIES  *Properties
  )
{
  DSDT_PROPERTY  Property;
  COPY_OUTCOME   Outcome;
  size_t         Length = 0;
  VOID           *Value;
  UINTN          TextUsed = Properties->TextUsed;

  ZeroMem (&Property, sizeof (Property));

  if (Type == PropertyChildren) {
    Outcome = ReadChildren (Node, Properties, &Property);
  } else {
    Value = dt_prop (Node, Name, &Length);
    if (Value == NULL) {
      return;
    }

    Outcome = ReadValue (Tree, Type, Value, Length, &Property);
  }

  if (Outcome == Copied) {
    Outcome = AddProperty (Properties, NodeName, Type == PropertyStream ? STREAM_KEY : Name, &Property);
  }

  if (Outcome == Copied) {
    return;
  }

  Properties->TextUsed = TextUsed;
  switch (Outcome) {
    case SkippedValue:
      ListSkipped (Properties, NodeName, Name, Length, PropertySkippedValue);
      break;
    case SkippedReference:
      ListSkipped (Properties, NodeName, Name, Length, PropertySkippedReference);
      break;
    case SkippedRoom:
      ListSkipped (Properties, NodeName, Name, Length, PropertySkippedRoom);
      break;
    case Copied:
    case Nothing:
      break;
  }
}

STATIC
VOID
CopyNode (
  IN     dt_node_t            *Tree,
  IN     dt_node_t            *Node,
  IN     CONST PROPERTY_NODE  *Table,
  IN OUT DEVICE_PROPERTIES    *Properties
  )
{
  CONST CHAR8  *NodeName = ReadString (Node, "name");
  CHAR8        Name[DT_KEY_LEN];
  UINTN        Length;
  UINTN        Row;
  UINTN        Number;

  if (NodeName == NULL) {
    return;   // a child without a name there could be a key of
  }

  for (Row = 0; Row < Table->RowCount; Row++) {
    if (!Table->Rows[Row].Numbered) {
      CopyProperty (Tree, Node, NodeName, Table->Rows[Row].Name, Table->Rows[Row].Type, Properties);
      continue;
    }

    Length = AsciiStrLen (Table->Rows[Row].Name);
    CopyMem (Name, Table->Rows[Row].Name, Length);
    for (Number = 0; Number < PROPERTY_NUMBER_LIMIT; Number++) {
      // One digit or two
      Name[Length]     = (CHAR8)('0' + (Number < 10 ? Number : Number / 10));
      Name[Length + 1] = Number < 10 ? 0 : (CHAR8)('0' + Number % 10);
      Name[Length + 2] = 0;
      CopyProperty (Tree, Node, NodeName, Name, Table->Rows[Row].Type, Properties);
    }
  }
}

VOID
CopyProperties (
  IN     dt_node_t            *Tree,
  IN     CONST PROPERTY_NODE  *Nodes,
  IN     UINTN                NodeCount,
  IN OUT DEVICE_PROPERTIES    *Properties
  )
{
  dt_node_t    *Node;
  dt_node_t    *Child;
  CONST CHAR8  *Type;
  UINTN        Index;
  UINTN        ChildIndex;

  for (Index = 0; Index < NodeCount; Index++) {
    Node = dt_find (Tree, Nodes[Index].Path);
    if (Node == NULL) {
      continue;
    }

    if (Nodes[Index].ChildType == NULL) {
      CopyNode (Tree, Node, &Nodes[Index], Properties);
      continue;
    }

    for (ChildIndex = 0; (Child = GetChild (Node, ChildIndex)) != NULL; ChildIndex++) {
      Type = ReadString (Child, "device_type");
      if (Type != NULL && AsciiStrCmp (Type, Nodes[Index].ChildType) == 0) {
        CopyNode (Tree, Child, &Nodes[Index], Properties);
      }
    }
  }
}
