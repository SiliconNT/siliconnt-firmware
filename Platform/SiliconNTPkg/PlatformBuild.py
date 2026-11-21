# @file
# Builds the SiliconNT firmware for the M4 MacBook Pro with stuart.
#
# Derived from Platform/MacBookProEarly2023Pkg/PlatformBuild.py of
# NT-for-ASi/apple_silicon_platforms_mu (861dcc490c56), which is itself based on
# WOA-Project/SurfaceDuoPkg PlatformBuild.py.
#
# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##
import logging
import os

from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_setup import RequiredSubmodule, SetupSettingsManager
from edk2toolext.invocables.edk2_update import UpdateSettingsManager


class CommonPlatform:
    """Settings shared by stuart_setup, stuart_update and stuart_build."""

    PackagesSupported = ("SiliconNTPkg",)
    ArchSupported = ("AARCH64",)
    TargetsSupported = ("DEBUG", "RELEASE")
    # No "edk2-build" scope: it only adds the nasm download, which AARCH64 does not need
    Scopes = ("SiliconNT",)
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    PackagesPath = (
        "Platform",
        "Silicon/Apple",
        "MU_BASECORE",
        "Common/TIANO",
        "Silicon/ARM/TIANO",
    )


class SettingsManager(UpdateSettingsManager, SetupSettingsManager):
    def GetPackagesSupported(self):
        return CommonPlatform.PackagesSupported

    def GetArchitecturesSupported(self):
        return CommonPlatform.ArchSupported

    def GetTargetsSupported(self):
        return CommonPlatform.TargetsSupported

    def GetRequiredSubmodules(self):
        # The second argument is "recursive". The nested submodules of the Mu
        # repositories (brotli, libfdt, test frameworks ...) are not needed.
        return [
            RequiredSubmodule("MU_BASECORE", False),
            RequiredSubmodule("Common/TIANO", False),
            RequiredSubmodule("Silicon/ARM/TIANO", False),
        ]

    def SetArchitectures(self, list_of_requested_architectures):
        unsupported = set(list_of_requested_architectures) - set(self.GetArchitecturesSupported())
        if unsupported:
            raise Exception("Unsupported architecture requested: " + " ".join(unsupported))
        self.ActualArchitectures = list_of_requested_architectures

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def GetName(self):
        return "SiliconNT"

    def GetPackagesPath(self):
        return CommonPlatform.PackagesPath


class PlatformBuilder(UefiBuilder, BuildSettingsManager):
    def __init__(self):
        UefiBuilder.__init__(self)

    def AddCommandLineOptions(self, parserObj):
        parserObj.add_argument(
            "-a", "--arch", dest="build_arch", type=str, default="AARCH64",
            help="Only AARCH64 is valid for this platform.")

    def RetrieveCommandLineOptions(self, args):
        if args.build_arch.upper() != "AARCH64":
            raise Exception("Only AARCH64 is valid for this platform.")

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetPackagesPath(self):
        return CommonPlatform.PackagesPath

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def GetName(self):
        return "SiliconNT"

    def GetLoggingLevel(self, loggerType):
        return logging.INFO if loggerType == "con" else logging.DEBUG

    def SetPlatformEnv(self):
        self.env.SetValue("PRODUCT_NAME", "SiliconNT", "Platform Hardcoded")
        self.env.SetValue("ACTIVE_PLATFORM", "SiliconNTPkg/SiliconNT.dsc", "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "AARCH64", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANGPDB", "Only clang has been tried")
        return 0

    def PlatformPreBuild(self):
        return 0

    def PlatformPostBuild(self):
        return 0

    def FlashRomImage(self):
        return 0
