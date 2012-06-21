// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuNtDll.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_DEFEXTRN_

#include <windows.h>

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace NtDll
{
    #ifdef __WINE__
    #define _EMUNTDLL_NODEFEXTERN_
    #endif
    #include "EmuNtDll.h"
};

// ******************************************************************
// * Loaded at run-time to avoid linker conflicts
// ******************************************************************
#ifndef __WINE__
static HMODULE hNtDll = GetModuleHandle("ntdll");

NtDll::FPTR_RtlInitAnsiString              NtDll::_RtlInitAnsiString            = (NtDll::FPTR_RtlInitAnsiString)GetProcAddress(hNtDll, "RtlInitAnsiString");
NtDll::FPTR_RtlInitUnicodeString           NtDll::_RtlInitUnicodeString         = (NtDll::FPTR_RtlInitUnicodeString)GetProcAddress(hNtDll, "RtlInitUnicodeString");
NtDll::FPTR_RtlAnsiStringToUnicodeString   NtDll::_RtlAnsiStringToUnicodeString = (NtDll::FPTR_RtlAnsiStringToUnicodeString)GetProcAddress(hNtDll, "RtlAnsiStringToUnicodeString");
NtDll::FPTR_RtlUnicodeStringToAnsiString   NtDll::_RtlUnicodeStringToAnsiString = (NtDll::FPTR_RtlUnicodeStringToAnsiString)GetProcAddress(hNtDll, "RtlUnicodeStringToAnsiString");
NtDll::FPTR_RtlNtStatusToDosError          NtDll::_RtlNtStatusToDosError        = (NtDll::FPTR_RtlNtStatusToDosError)GetProcAddress(hNtDll, "RtlNtStatusToDosError");
NtDll::FPTR_RtlTimeFieldsToTime            NtDll::_RtlTimeFieldsToTime          = (NtDll::FPTR_RtlTimeFieldsToTime)GetProcAddress(hNtDll, "RtlTimeFieldsToTime");
NtDll::FPTR_RtlTimeToTimeFields            NtDll::_RtlTimeToTimeFields          = (NtDll::FPTR_RtlTimeToTimeFields)GetProcAddress(hNtDll, "RtlTimeToTimeFields");
NtDll::FPTR_RtlTryEnterCriticalSection     NtDll::_RtlTryEnterCriticalSection   = (NtDll::FPTR_RtlTryEnterCriticalSection)GetProcAddress(hNtDll, "RtlTryEnterCriticalSection");
NtDll::FPTR_RtlInitializeCriticalSection   NtDll::_RtlInitializeCriticalSection = (NtDll::FPTR_RtlInitializeCriticalSection)GetProcAddress(hNtDll, "RtlInitializeCriticalSection");
NtDll::FPTR_RtlEnterCriticalSection        NtDll::_RtlEnterCriticalSection      = (NtDll::FPTR_RtlEnterCriticalSection)GetProcAddress(hNtDll, "RtlEnterCriticalSection");
NtDll::FPTR_RtlLeaveCriticalSection        NtDll::_RtlLeaveCriticalSection      = (NtDll::FPTR_RtlLeaveCriticalSection)GetProcAddress(hNtDll, "RtlLeaveCriticalSection");
NtDll::FPTR_RtlCreateHeap                  NtDll::_RtlCreateHeap                = (NtDll::FPTR_RtlCreateHeap)GetProcAddress(hNtDll, "RtlCreateHeap");
NtDll::FPTR_RtlAllocateHeap                NtDll::_RtlAllocateHeap              = (NtDll::FPTR_RtlAllocateHeap)GetProcAddress(hNtDll, "RtlAllocateHeap");
NtDll::FPTR_RtlFreeHeap                    NtDll::_RtlFreeHeap                  = (NtDll::FPTR_RtlFreeHeap)GetProcAddress(hNtDll, "RtlFreeHeap");
NtDll::FPTR_RtlReAllocateHeap              NtDll::_RtlReAllocateHeap            = (NtDll::FPTR_RtlReAllocateHeap)GetProcAddress(hNtDll, "RtlReAllocateHeap");
NtDll::FPTR_RtlSizeHeap                    NtDll::_RtlSizeHeap                  = (NtDll::FPTR_RtlSizeHeap)GetProcAddress(hNtDll, "RtlSizeHeap");
NtDll::FPTR_RtlDestroyHeap                 NtDll::_RtlDestroyHeap               = (NtDll::FPTR_RtlDestroyHeap)GetProcAddress(hNtDll, "RtlDestroyHeap");
NtDll::FPTR_RtlEqualString                 NtDll::_RtlEqualString               = (NtDll::FPTR_RtlEqualString)GetProcAddress(hNtDll, "RtlEqualString");
NtDll::FPTR_NtWaitForSingleObject          NtDll::_NtWaitForSingleObject        = (NtDll::FPTR_NtWaitForSingleObject)GetProcAddress(hNtDll, "NtWaitForSingleObject");
NtDll::FPTR_NtWaitForMultipleObjects       NtDll::_NtWaitForMultipleObjects     = (NtDll::FPTR_NtWaitForMultipleObjects)GetProcAddress(hNtDll, "NtWaitForMultipleObjects");
NtDll::FPTR_NtAllocateVirtualMemory        NtDll::_NtAllocateVirtualMemory      = (NtDll::FPTR_NtAllocateVirtualMemory)GetProcAddress(hNtDll, "NtAllocateVirtualMemory");
NtDll::FPTR_NtFreeVirtualMemory            NtDll::_NtFreeVirtualMemory          = (NtDll::FPTR_NtFreeVirtualMemory)GetProcAddress(hNtDll, "NtFreeVirtualMemory");
NtDll::FPTR_NtQueryVirtualMemory           NtDll::_NtQueryVirtualMemory         = (NtDll::FPTR_NtQueryVirtualMemory)GetProcAddress(hNtDll, "NtQueryVirtualMemory");
NtDll::FPTR_NtClearEvent                   NtDll::_NtClearEvent                 = (NtDll::FPTR_NtClearEvent)GetProcAddress(hNtDll, "NtClearEvent");
NtDll::FPTR_NtClose                        NtDll::_NtClose                      = (NtDll::FPTR_NtClose)GetProcAddress(hNtDll, "NtClose");
NtDll::FPTR_NtDelayExecution               NtDll::_NtDelayExecution             = (NtDll::FPTR_NtDelayExecution)GetProcAddress(hNtDll, "NtDelayExecution");
NtDll::FPTR_NtDuplicateObject              NtDll::_NtDuplicateObject            = (NtDll::FPTR_NtDuplicateObject)GetProcAddress(hNtDll, "NtDuplicateObject");
NtDll::FPTR_NtFlushBuffersFile             NtDll::_NtFlushBuffersFile           = (NtDll::FPTR_NtFlushBuffersFile)GetProcAddress(hNtDll, "NtFlushBuffersFile");
NtDll::FPTR_NtQueryInformationFile         NtDll::_NtQueryInformationFile       = (NtDll::FPTR_NtQueryInformationFile)GetProcAddress(hNtDll, "NtQueryInformationFile");
NtDll::FPTR_NtQueryDirectoryFile           NtDll::_NtQueryDirectoryFile         = (NtDll::FPTR_NtQueryDirectoryFile)GetProcAddress(hNtDll, "NtQueryDirectoryFile");
NtDll::FPTR_NtQueryFullAttributesFile      NtDll::_NtQueryFullAttributesFile    = (NtDll::FPTR_NtQueryFullAttributesFile)GetProcAddress(hNtDll, "NtQueryFullAttributesFile");
NtDll::FPTR_NtQueryVolumeInformationFile   NtDll::_NtQueryVolumeInformationFile = (NtDll::FPTR_NtQueryVolumeInformationFile)GetProcAddress(hNtDll, "NtQueryVolumeInformationFile");
NtDll::FPTR_NtCreateEvent                  NtDll::_NtCreateEvent                = (NtDll::FPTR_NtCreateEvent)GetProcAddress(hNtDll, "NtCreateEvent");
NtDll::FPTR_NtCreateMutant                 NtDll::_NtCreateMutant               = (NtDll::FPTR_NtCreateMutant)GetProcAddress(hNtDll, "NtCreateMutant");
NtDll::FPTR_NtReleaseMutant                NtDll::_NtReleaseMutant              = (NtDll::FPTR_NtReleaseMutant)GetProcAddress(hNtDll, "NtReleaseMutant");
NtDll::FPTR_NtCreateSemaphore              NtDll::_NtCreateSemaphore            = (NtDll::FPTR_NtCreateSemaphore)GetProcAddress(hNtDll, "NtCreateSemaphore");
NtDll::FPTR_NtReleaseSemaphore             NtDll::_NtReleaseSemaphore           = (NtDll::FPTR_NtReleaseSemaphore)GetProcAddress(hNtDll, "NtReleaseSemaphore");
NtDll::FPTR_NtCreateFile                   NtDll::_NtCreateFile                 = (NtDll::FPTR_NtCreateFile)GetProcAddress(hNtDll, "NtCreateFile");
NtDll::FPTR_NtReadFile                     NtDll::_NtReadFile                   = (NtDll::FPTR_NtReadFile)GetProcAddress(hNtDll, "NtReadFile");
NtDll::FPTR_NtWriteFile                    NtDll::_NtWriteFile                  = (NtDll::FPTR_NtWriteFile)GetProcAddress(hNtDll, "NtWriteFile");
NtDll::FPTR_NtYieldExecution               NtDll::_NtYieldExecution             = (NtDll::FPTR_NtYieldExecution)GetProcAddress(hNtDll, "NtYieldExecution");
NtDll::FPTR_NtSetInformationFile           NtDll::_NtSetInformationFile         = (NtDll::FPTR_NtSetInformationFile)GetProcAddress(hNtDll, "NtSetInformationFile");
NtDll::FPTR_NtSetEvent                     NtDll::_NtSetEvent                   = (NtDll::FPTR_NtSetEvent)GetProcAddress(hNtDll, "NtSetEvent");
NtDll::FPTR_NtSuspendThread                NtDll::_NtSuspendThread              = (NtDll::FPTR_NtSuspendThread)GetProcAddress(hNtDll, "NtSuspendThread");
NtDll::FPTR_NtResumeThread                 NtDll::_NtResumeThread               = (NtDll::FPTR_NtResumeThread)GetProcAddress(hNtDll, "NtResumeThread");
NtDll::FPTR_NtSetLdtEntries                NtDll::_NtSetLdtEntries              = (NtDll::FPTR_NtSetLdtEntries)GetProcAddress(hNtDll, "NtSetLdtEntries");
NtDll::FPTR_NtQueueApcThread               NtDll::_NtQueueApcThread             = (NtDll::FPTR_NtQueueApcThread)GetProcAddress(hNtDll, "NtQueueApcThread");
#else
extern "C" void InitializeNtDll(void)
{
    HMODULE hNtDll = GetModuleHandle("ntdll");

#define LOADSYMBOL(x, y) NtDll::_##y = (NtDll::FPTR_##y)GetProcAddress(x, #y)
    LOADSYMBOL(hNtDll, RtlInitAnsiString);
    LOADSYMBOL(hNtDll, RtlInitUnicodeString);
    LOADSYMBOL(hNtDll, RtlAnsiStringToUnicodeString);
    LOADSYMBOL(hNtDll, RtlUnicodeStringToAnsiString);
    LOADSYMBOL(hNtDll, RtlNtStatusToDosError);
    LOADSYMBOL(hNtDll, RtlTimeFieldsToTime);
    LOADSYMBOL(hNtDll, RtlTimeToTimeFields);
    LOADSYMBOL(hNtDll, RtlTryEnterCriticalSection);
    LOADSYMBOL(hNtDll, RtlInitializeCriticalSection);
    LOADSYMBOL(hNtDll, RtlEnterCriticalSection);
    LOADSYMBOL(hNtDll, RtlLeaveCriticalSection);
    LOADSYMBOL(hNtDll, RtlCreateHeap);
    LOADSYMBOL(hNtDll, RtlAllocateHeap);
    LOADSYMBOL(hNtDll, RtlFreeHeap);
    LOADSYMBOL(hNtDll, RtlReAllocateHeap);
    LOADSYMBOL(hNtDll, RtlSizeHeap);
    LOADSYMBOL(hNtDll, RtlDestroyHeap);
    LOADSYMBOL(hNtDll, RtlEqualString);
    LOADSYMBOL(hNtDll, NtWaitForSingleObject);
    LOADSYMBOL(hNtDll, NtWaitForMultipleObjects);
    LOADSYMBOL(hNtDll, NtAllocateVirtualMemory);
    LOADSYMBOL(hNtDll, NtFreeVirtualMemory);
    LOADSYMBOL(hNtDll, NtQueryVirtualMemory);
    LOADSYMBOL(hNtDll, NtClearEvent);
    LOADSYMBOL(hNtDll, NtClose);
    LOADSYMBOL(hNtDll, NtDelayExecution);
    LOADSYMBOL(hNtDll, NtDuplicateObject);
    LOADSYMBOL(hNtDll, NtFlushBuffersFile);
    LOADSYMBOL(hNtDll, NtQueryInformationFile);
    LOADSYMBOL(hNtDll, NtQueryDirectoryFile);
    LOADSYMBOL(hNtDll, NtQueryFullAttributesFile);
    LOADSYMBOL(hNtDll, NtQueryVolumeInformationFile);
    LOADSYMBOL(hNtDll, NtCreateEvent);
    LOADSYMBOL(hNtDll, NtCreateMutant);
    LOADSYMBOL(hNtDll, NtReleaseMutant);
    LOADSYMBOL(hNtDll, NtCreateSemaphore);
    LOADSYMBOL(hNtDll, NtReleaseSemaphore);
    LOADSYMBOL(hNtDll, NtCreateFile);
    LOADSYMBOL(hNtDll, NtReadFile);
    LOADSYMBOL(hNtDll, NtWriteFile);
    LOADSYMBOL(hNtDll, NtYieldExecution);
    LOADSYMBOL(hNtDll, NtSetInformationFile);
    LOADSYMBOL(hNtDll, NtSetEvent);
    LOADSYMBOL(hNtDll, NtSuspendThread);
    LOADSYMBOL(hNtDll, NtResumeThread);
    LOADSYMBOL(hNtDll, NtSetLdtEntries);
    LOADSYMBOL(hNtDll, NtQueueApcThread);
#undef LOADSYMBOL
}
#endif
