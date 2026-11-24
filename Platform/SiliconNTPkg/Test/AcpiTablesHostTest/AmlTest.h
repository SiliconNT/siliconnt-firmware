/** @file
  Part of AcpiTablesTest.c: how AML is written (Drivers/AcpiPlatformDxe/AmlWriter.c),
  and what AcpiTableBuilder.c makes of a device that is described to it by hand.
  Included there once, after its helpers.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// A package of ContentSize bytes: what it starts with, the opcode excepted. Lead
// byte: bits 6-7 the number of bytes that follow; with none, bits 0-5 the length,
// else bits 0-3 its low four bits and each byte that follows the next eight. The
// length counts these bytes too (ACPI 6.6, 20.2.4).
//
STATIC VOID CheckPkgLength(IN UINTN ContentSize, IN CONST CHAR8 *Expected, IN UINTN ExpectedSize)
{
    UINT8       *Buffer = malloc(ContentSize + 16);
    AML_WRITER  Counted = { NULL, 0 };
    AML_WRITER  Written = { Buffer, 0 };
    UINT8       *Content = malloc(ContentSize + 1);
    UINTN       Start;
    CHAR8       Line[256];

    memset(Buffer, 0xa5, ContentSize + 16);
    memset(Content, 0x77, ContentSize + 1);
    AmlPutByte(&Counted, 0x12);
    Start = AmlBegin(&Counted);
    AmlPutBytes(&Counted, Content, ContentSize);
    AmlEnd(&Counted, Start);
    AmlPutByte(&Written, 0x12);
    Start = AmlBegin(&Written);
    AmlPutBytes(&Written, Content, ContentSize);
    AmlEnd(&Written, Start);
    snprintf(Line, sizeof(Line), "AML: a package of %u bytes, %u of length before them", (unsigned)ContentSize, (unsigned)ExpectedSize);
    Expect(Counted.Used == 1 + ExpectedSize + ContentSize && Written.Used == Counted.Used && Buffer[0] == 0x12 &&
           memcmp(Buffer + 1, Expected, ExpectedSize) == 0 && memcmp(Buffer + 1 + ExpectedSize, Content, ContentSize) == 0 &&
           Buffer[Written.Used] == 0xa5, Line);
    free(Buffer);
    free(Content);
}

#define WORDS_OF(Count)  { "k", DsdtPackage, 0, NULL, 0, mManyWords, Count }

STATIC UINT8 mManyWords[256 * 4];

// The elements of mManyWords as ASL; the zeros, ones, bytes, words and double words the ASL compiler tells apart
STATIC UINT32 ManyWords(IN UINTN Index)
{
    switch(Index % 5) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2 + (UINT32)Index % 0xfe;
        case 3:
            return 0x100 + (UINT32)Index;
        default:
            return 0xffffffff - (UINT32)Index;
    }
}

//
// A device with these properties, as DSDT and as ASL
//
STATIC VOID WriteProperties(IN CONST CHAR8 *Name, IN CONST DSDT_PROPERTY *Properties, IN UINTN Count)
{
    STATIC CONST APPLE_MMIO_RANGE  Memory[1] = { { 0x1000, 0x1000 } };
    STATIC CONST UINT32            Gsiv[1]   = { 40 };
    STATIC CHAR8                   Asl[262144];
    DSDT_DEVICE                    Device = { "DEV0", "SLNT9999", Memory, 1, Gsiv, 1, Properties, Count };
    CONST DSDT_DEVICE              *Devices[1] = { &Device };
    UINTN                          Size  = DsdtSize(NULL, 0, Devices, 1);
    UINTN                          Used;
    UINTN                          Index;
    UINTN                          Element;
    CHAR8                          Line[256];

    memset(mTable, 0xa5, sizeof(mTable));
    snprintf(Line, sizeof(Line), "%s: the DSDT is built, %u bytes, nothing written beyond it", Name, (unsigned)Size);
    if(!Expect(Size < sizeof(mTable) && DsdtBuild(0x8132, CREATOR_REVISION, NULL, 0, Devices, 1, mTable) == RETURN_SUCCESS && mTable[Size] == 0xa5, Line)) {
        return;
    }
    WriteOutput(Name, "aml", mTable, Size);

    Used = snprintf(Asl, sizeof(Asl),
                    "DefinitionBlock (\"\", \"DSDT\", 2, \"Apple \", \"APPLEEFI\", 0x8132) {\n"
                    "  Scope (\\_SB) {\n"
                    "    Device (DEV0) {\n"
                    "      Name (_HID, \"SLNT9999\")\n"
                    "      Name (_UID, 0)\n"
                    "      Name (_CCA, 0)\n"
                    "      Name (_CRS, ResourceTemplate () {\n"
                    "        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x1000, 0x1FFF, 0x0, 0x1000)\n"
                    "        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 40 }\n"
                    "      })\n"
                    "      Name (_DSD, Package () {\n"
                    "        ToUUID (\"daffd814-6eba-4d8c-8a91-bc9bbf4aa301\"),\n"
                    "        Package () {\n");
    for(Index = 0; Index < Count; Index++) {
        CONST DSDT_PROPERTY *Property = &Properties[Index];
        CONST CHAR8         *String   = Property->Strings;
        CONST CHAR8         *Between  = "";

        Used += snprintf(Asl + Used, sizeof(Asl) - Used, "          Package (2) { \"%s\", %s", Property->Key, Property->Kind == DsdtPackage ? "Package () {" : "");
        if(Property->Kind == DsdtInteger) {
            Used += snprintf(Asl + Used, sizeof(Asl) - Used, "%u", (unsigned)Property->Integer);
        }
        for(Element = 0; Element < Property->StringCount; Element++) {
            Used += snprintf(Asl + Used, sizeof(Asl) - Used, "%s\"%s\"", Between, String);
            String += strlen(String) + 1;
            Between = ", ";
        }
        for(Element = 0; Element < Property->WordCount; Element++) {
            UINT32 Word;

            memcpy(&Word, Property->Words + 4 * Element, 4);
            Used += snprintf(Asl + Used, sizeof(Asl) - Used, "%s0x%X", Between, (unsigned)Word);
            Between = ", ";
        }
        Used += snprintf(Asl + Used, sizeof(Asl) - Used, "%s },\n", Property->Kind == DsdtPackage ? " }" : "");
    }
    Used += snprintf(Asl + Used, sizeof(Asl) - Used, "        }\n      })\n    }\n  }\n}\n");
    WriteOutput(Name, "asl", Asl, Used);
}

// Of the DSDT in mTable: how many bytes the PkgLength of the scope has, that of the package after the UUID
STATIC UINTN LengthBytesOfScope(VOID)
{
    return (mTable[36 + 1] >> 6) + 1;
}

STATIC UINTN LengthBytesOfProperties(VOID)
{
    UINTN Index;

    for(Index = 36; memcmp(mTable + Index, mDevicePropertiesUuid, sizeof(mDevicePropertiesUuid)) != 0; Index++) {
    }
    return (mTable[Index + sizeof(mDevicePropertiesUuid) + 1] >> 6) + 1;
}

STATIC VOID CheckAml(VOID)
{
    UINTN Index;

    CheckPkgLength(0, "\x01", 1);
    CheckPkgLength(62, "\x3f", 1);
    CheckPkgLength(63, "\x41\x04", 2);            // 65
    CheckPkgLength(4093, "\x4f\xff", 2);          // 4095
    CheckPkgLength(4094, "\x81\x00\x01", 3);      // 4097
    CheckPkgLength(0x12345, "\x88\x34\x12", 3);   // 0x12348
    CheckPkgLength(1048572, "\x8f\xff\xff", 3);   // 0xfffff
    CheckPkgLength(1048573, "\xc1\x00\x00\x01", 4);   // 0x100001

    for(Index = 0; Index < 256; Index++) {
        UINT32 Word = ManyWords(Index);

        memcpy(mManyWords + 4 * Index, &Word, 4);
    }

    //
    // Every kind of property, what is in a package and what is not
    //
    {
        STATIC CONST DSDT_PROPERTY Kinds[] = {
            { "zero", DsdtInteger, 0 }, { "one", DsdtInteger, 1 }, { "byte", DsdtInteger, 2 }, { "byte-max", DsdtInteger, 0xff },
            { "word", DsdtInteger, 0x100 }, { "word-max", DsdtInteger, 0xffff }, { "dword", DsdtInteger, 0x10000 }, { "dword-max", DsdtInteger, 0xffffffff },
            { "string", DsdtString, 0, "text", 1 }, { "empty-string", DsdtString, 0, "", 1 },
            { "strings", DsdtPackage, 0, "a\0bc\0def", 3 }, { "one-string", DsdtPackage, 0, "abc", 1 },
            { "one-word", DsdtPackage, 0, NULL, 0, mManyWords + 4 * 4, 1 }, { "words", DsdtPackage, 0, NULL, 0, mManyWords, 7 },
            { "both", DsdtPackage, 0, "smc-pmu", 1, mManyWords, 3 },
            { "255-words", DsdtPackage, 0, NULL, 0, mManyWords, 255 }, { "254-words-and-a-string", DsdtPackage, 0, "x", 1, mManyWords, 254 },
        };

        WriteProperties("property-kinds", Kinds, ARRAY_SIZE(Kinds));
    }

    //
    // Lengths from below 4095 to above, in steps of one, at which the scope, the
    // device, the package of _DSD and in it the package of the properties go, one
    // after the other, from two bytes of length to three. A property holds a string
    // that is one longer each time.
    //
    for(Index = 0; Index < 255; Index++) {
        STATIC CHAR8   Text[256];
        DSDT_PROPERTY  Properties[8] = {
            WORDS_OF(255), WORDS_OF(255), WORDS_OF(255), WORDS_OF(255), { "t", DsdtString, 0, Text, 1 }, WORDS_OF(255), WORDS_OF(255), WORDS_OF(45),
        };
        CHAR8          Name[32];

        memset(Text, 'x', Index);
        Text[Index] = 0;
        snprintf(Name, sizeof(Name), "length-near-4095-%03u", (unsigned)Index);
        WriteProperties(Name, Properties, ARRAY_SIZE(Properties));
        if(Index == 0) {
            Expect(LengthBytesOfScope() == 2 && LengthBytesOfProperties() == 2, "the first of these: the scope and the properties have two bytes of length");
        }
    }
    Expect(LengthBytesOfScope() == 3 && LengthBytesOfProperties() == 3, "the last of these: the scope and the properties have three bytes of length");

    //
    // What the builder refuses. Nothing is written then.
    //
    {
        STATIC CHAR8                   Strings[2 * 256];
        STATIC CONST APPLE_MMIO_RANGE  Good[2] = { { 0x1000, 0x1000 }, { 0x3000, 4 } };
        STATIC CONST APPLE_MMIO_RANGE  Empty[2] = { { 0x1000, 0x1000 }, { 0x3000, 0 } };
        STATIC CONST APPLE_MMIO_RANGE  Beyond[2] = { { 0x1000, 0x1000 }, { 0xfffffffffffffffdULL, 4 } };
        STATIC DSDT_PROPERTY           Many[256];
        DSDT_DEVICE                    Device = { "DEV0", "SLNT9999", Good, 2, NULL, 0, Many, 255 };
        CONST DSDT_DEVICE              *Devices[2] = { &Device, &Device };
        DSDT_PROPERTY                  Package = { "k", DsdtPackage, 0, NULL, 0, mManyWords, 255 };
        UINTN                          Index2;

        memset(Strings, 0, sizeof(Strings));
        for(Index2 = 0; Index2 < 256; Index2++) {
            Strings[2 * Index2] = 'a';
            Many[Index2].Key    = "k";
            Many[Index2].Kind   = DsdtInteger;
        }
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 2, mTable) == RETURN_SUCCESS, "DSDT: a device with 255 properties is taken");
        mTable[0] = 0xa5;
        Device.PropertyCount = 256;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a device with 256 properties is refused, nothing is written");
        Expect(DsdtSize(NULL, 0, Devices, 1) == 0, "DSDT: the size of what is refused is 0");

        Device.Properties    = &Package;
        Device.PropertyCount = 1;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_SUCCESS, "DSDT: a package of 255 words is taken");
        mTable[0] = 0xa5;
        Package.WordCount = 256;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a package of 256 words is refused");
        Package.WordCount   = 254;
        Package.Strings     = Strings;
        Package.StringCount = 2;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a package of 2 strings and 254 words is refused");
        Package.WordCount   = 0;
        Package.StringCount = 256;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a package of 256 strings is refused");
        Package.StringCount = 255;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_SUCCESS, "DSDT: a package of 255 strings is taken");

        mTable[0] = 0xa5;
        Device.Memory = Empty;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a device whose second range is empty is refused");
        Device.Memory = Beyond;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 1, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a device whose second range reaches past the top is refused");
        Device.Memory      = Good;
        Device.MemoryCount = 1;
        Devices[1]         = &Device;
        Expect(DsdtBuild(0x8132, 1, NULL, 0, Devices, 2, mTable) == RETURN_SUCCESS, "DSDT: the same device with one range is taken");
    }
}
