/** @file
  The properties of a device's _DSD, copied from the Apple device tree: which
  properties of which nodes is a table of the caller; what they mean is not looked
  at. How a value is written, and why so: SiliconNT docs/findings/0095, conclusion 6.

  The key is "node/property". Integers have 32 bits. What does not fit its type is
  skipped and listed, the rest is copied all the same.

  Nothing here calls a firmware service; Test/AcpiTablesHostTest compiles this code
  for the host.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef DEVICE_PROPERTIES_H_
#define DEVICE_PROPERTIES_H_

#include "AcpiTableBuilder.h"

typedef enum {
  PropertyInteger,    // 4 bytes: an integer
  PropertyPresent,    // empty: the integer 1
  PropertyString,     // one string: a string
  PropertyStrings,    // one or more strings: a package of strings
  PropertyWords,      // one or more times 4 bytes: a package of integers
  PropertyList,       // the same; nothing when it is empty
  PropertyFunction,   // a phandle and one or more times 4 bytes: a package, the name of the node the
                      // phandle is of, then the integers
  PropertyStream,     // "iommu-parent", the phandle of a mapper: "iommu-stream", its "reg", an integer
  PropertyChildren    // not a property of the tree: a package of the names of the node's children;
                      // nothing without children
} PROPERTY_TYPE;

//
// A property of a node. Numbered: not Name but Name and a decimal number, every one
// there is from 0 to PROPERTY_NUMBER_LIMIT - 1.
//
typedef struct {
  CONST CHAR8      *Name;
  PROPERTY_TYPE    Type;
  BOOLEAN          Numbered;
} PROPERTY_ROW;

#define PROPERTY_NUMBER_LIMIT  64

//
// The properties of a node; with ChildType not NULL not of the node but of each of
// its children whose "device_type" that is. A node or a property that is not in the
// tree is passed over.
//
typedef struct {
  CONST CHAR8           *Path;
  CONST CHAR8           *ChildType;
  CONST PROPERTY_ROW    *Rows;
  UINTN                 RowCount;
} PROPERTY_NODE;

//
// A property that is in the tree and not in the _DSD
//
typedef enum {
  PropertySkippedValue,       // the value does not fit the type
  PropertySkippedReference,   // the node it points to is not there, or lacks what is needed of it
  PropertySkippedRoom         // more than there is room for
} PROPERTY_SKIPPED_REASON;

typedef struct {
  CONST CHAR8                *Key;      // "node/property", the property as the tree calls it
  UINTN                      Length;    // of the value in the tree
  PROPERTY_SKIPPED_REASON    Reason;
} SKIPPED_PROPERTY;

#define DEVICE_MAX_SKIPPED  16
#define DEVICE_TEXT_SIZE    16384

//
// The properties point into the tree and into Text, so the structure must not be
// copied. All 0 is an empty one.
//
typedef struct {
  DSDT_PROPERTY       Properties[DSDT_MAX_ELEMENTS];
  UINTN               Count;
  SKIPPED_PROPERTY    Skipped[DEVICE_MAX_SKIPPED];    // the first ones
  UINTN               SkippedCount;                   // all of them
  CHAR8               Text[DEVICE_TEXT_SIZE];
  UINTN               TextUsed;
} DEVICE_PROPERTIES;

/**
  Adds the properties the tables name, in their order. A package holds at most
  DSDT_MAX_ELEMENTS: a list of more integers goes into several properties, as many
  as fit in each, the key of the second with "+1" at its end, of the third with "+2"
  and so on.
**/
VOID
CopyProperties (
  IN     dt_node_t            *Tree,
  IN     CONST PROPERTY_NODE  *Nodes,
  IN     UINTN                NodeCount,
  IN OUT DEVICE_PROPERTIES    *Properties
  );

/**
  For a package of names: AddName() adds "Block-Entry", or "Block" with
  APPLE_DEVICE_NO_ENTRY, to Text, one after the other; AddNames() makes the property
  of the Count names that start at First, what Text + TextUsed was before. FALSE
  when there is no room.
**/
BOOLEAN
AddName (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     CONST CHAR8        *Block,
  IN     UINT32             Entry
  );

BOOLEAN
AddNames (
  IN OUT DEVICE_PROPERTIES  *Properties,
  IN     CONST CHAR8        *Key,
  IN     CONST CHAR8        *First,
  IN     UINTN              Count
  );

#endif // DEVICE_PROPERTIES_H_
