// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->Emu.cpp
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

// prevent name collisions
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "CxbxKrnl.h"
#define COMPILE_MULTIMON_STUBS
#include "Emu.h"
#include "EmuFS.h"
#include "EmuAlloc.h"

// prevent name collisions
namespace NtDll
{
    #include "EmuNtDll.h"
};

#include "EmuXTL.h"
#include "EmuShared.h"
#include "HLEDataBase.h"
#include "HLEIntercept.h"

// Global Variable(s)
HANDLE           g_hCurDir    = NULL;
CHAR            *g_strCurDrive= NULL;
HANDLE           g_hTDrive    = NULL;
CHAR            *g_strTDrive  = NULL;
HANDLE           g_hUDrive    = NULL;
CHAR            *g_strUDrive  = NULL;
HANDLE           g_hZDrive    = NULL;
CHAR            *g_strZDrive  = NULL;
volatile bool    g_bEmuException = false;
volatile bool    g_bEmuSuspended = false;
volatile bool    g_bPrintfOn = true;

// global exception patching address
uint32 g_HaloHack[4] = {0};

// Static Function(s)
static int ExitException(LPEXCEPTION_POINTERS e);

// Dll entry point, exit point, ...
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    static HINSTANCE hInitInstance = NULL;

    if(fdwReason == DLL_PROCESS_ATTACH)
    {
        EmuShared::Init();
        hInitInstance = hinstDLL;
    }

    if(fdwReason == DLL_PROCESS_DETACH)
    {
        if(hInitInstance == hinstDLL)
            EmuShared::Cleanup();
    }

    return TRUE;
}

// print out a warning message to the kernel debug log file
#ifdef _DEBUG_WARNINGS
extern "C" CXBXKRNL_API void NTAPI EmuWarning(const char *szWarningMessage, ...)
{
    if(szWarningMessage == NULL)
        return;

    char szBuffer1[255];
    char szBuffer2[255];

    va_list argp;

    sprintf(szBuffer1, "EmuWarn (0x%X): ", GetCurrentThreadId());

    va_start(argp, szWarningMessage);

    vsprintf(szBuffer2, szWarningMessage, argp);

    va_end(argp);

    strcat(szBuffer1, szBuffer2);

    if(g_bPrintfOn)
    {
        printf("%s\n", szBuffer1);
    }

    fflush(stdout);

    return;
}
#endif

// exception handler
extern int EmuException(LPEXCEPTION_POINTERS e)
{
    if(EmuIsXboxFS())
        EmuSwapFS();

    g_bEmuException = true;

    // check for Halo hack
    {
        if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
        {
            // Halo Access Adjust 1
            if(e->ContextRecord->Eip == 0x0003394C)
            {
                if(e->ContextRecord->Ecx == 0x803BD800)
                {
                    // Halo BINK skip
                    {
                        // nop sled over bink calls
                        /*
                        memset((void*)0x2CBA4, 0x90, 0x2CBAF - 0x2CBA4);
                        memset((void*)0x2CBBD, 0x90, 0x2CBD5 - 0x2CBBD);
                        //*/
                        memset((void*)0x2CAE0, 0x90, 0x2CE1E - 0x2CAE0);
                    }

                    uint32 fix = g_HaloHack[1] + (e->ContextRecord->Eax - 0x803A6000);

                    e->ContextRecord->Eax = e->ContextRecord->Ecx = fix;

                    *(uint32*)e->ContextRecord->Esp = fix;

                    ((XTL::X_D3DResource*)fix)->Data = g_HaloHack[1] + (((XTL::X_D3DResource*)fix)->Data - 0x803A6000);

                    // go through and fix any other pointers in the ESI allocation chunk
                    {
                        DWORD dwESI = e->ContextRecord->Esi;
                        DWORD dwSize = EmuCheckAllocationSize((PVOID)dwESI, false);

                        // dword aligned
                        dwSize -= 4 - dwSize%4;

                        for(DWORD v=0;v<dwSize;v+=4)
                        {
                            DWORD dwCur = *(DWORD*)(dwESI+v);

                            if(dwCur >= 0x803A6000 && dwCur < 0x819A6000)
                                *(DWORD*)(dwESI+v) = g_HaloHack[1] + (dwCur - 0x803A6000);
                        }
                    }

                    // fix this global pointer
                    {
                        DWORD dwValue = *(DWORD*)0x39CE24;

                        *(DWORD*)0x39CE24 = g_HaloHack[1] + (dwValue - 0x803A6000);
                    }

                    DbgPrintf("EmuMain (0x%X): Halo Access Adjust 1 was applied!\n", GetCurrentThreadId());

                    g_bEmuException = false;

                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
            // Halo Access Adjust 2
            else if(e->ContextRecord->Eip == 0x00058D8C)
            {
                if(e->ContextRecord->Eax == 0x819A5818)
                {
                    uint32 fix = g_HaloHack[1] + (e->ContextRecord->Eax - 0x803A6000);

                    *(DWORD*)0x0039BE58 = e->ContextRecord->Eax = fix;

                    // go through and fix any other pointers in the 0x2DF1C8 allocation chunk
                    {
                        DWORD dwPtr = *(DWORD*)0x2DF1C8;
                        DWORD dwSize = EmuCheckAllocationSize((PVOID)dwPtr, false);

                        // dword aligned
                        dwSize -= 4 - dwSize%4;

                        for(DWORD v=0;v<dwSize;v+=4)
                        {
                            DWORD dwCur = *(DWORD*)(dwPtr+v);

                            if(dwCur >= 0x803A6000 && dwCur < 0x819A6000)
                                *(DWORD*)(dwPtr+v) = g_HaloHack[1] + (dwCur - 0x803A6000);
                        }
                    }

                    DbgPrintf("EmuMain (0x%X): Halo Access Adjust 2 was applied!\n", GetCurrentThreadId());

                    g_bEmuException = false;

                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
        }
    }

    // check for Battlestar Galactica hack *PAL Version*
    {
        if(e->ExceptionRecord->ExceptionCode == 0xC0000096)
        {
            // Battlestar Galactica Hack 1
            if(e->ContextRecord->Eip == 0x000CB580)
            {
                //if(e->ContextRecord->Ecx == 0x00000200 || e->ContextRecord->Ecx == 0x00000100)
                //{
                    // Battlestar Galactica WBINVD skip
					e->ContextRecord->Eip += 2;

                    DbgPrintf("EmuMain (0x%X): Battlestar Galactica Hack 1 was applied!\n", GetCurrentThreadId());

                    g_bEmuException = false;

                    return EXCEPTION_CONTINUE_EXECUTION;
                //}
            }
        }
    }

	// blueshogun96 9/27/07
	// check for Myst III hack (NTSC)
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000094)
		{
			if(e->ContextRecord->Eip == 0x536BC)
			{
//				 Do Myst III hack!
//				uint32 dword_11CD58 = *(uint32*) 0x11CD58;

				// Insert NOP to replace IDIV (which causes divide by zero exception)
//				dword_11CD58 = 0x90;	
				// Skip IDIV instruction
				e->ContextRecord->Eip += 1;

				DbgPrintf("EmuMain (0x%X): Myst III divide by zero hack applied!\n", GetCurrentThreadId());

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// blueshogun96 9/27/07
	// check for Unreal Championship hack (NTSC)
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			// Unreal Championship hack 1
			if(e->ContextRecord->Eip == 0x2EA31)
			{	
				// Skip REP STOSD (causes exception)
				e->ContextRecord->Eip += 2;

				DbgPrintf("EmuMain (0x%X): Unreal Championship hack 1 applied!\n", GetCurrentThreadId());

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}

			// Unreal Championship hack 2
			if(e->ContextRecord->Eip == 0x412B1B)
			{
				// Attempt to skip video (atari.wmv)
				// Fill function with NOPs
				/*memset( (void*)0x2F000, 0x90, 0x2F3BE - 0x2F000 );

				e->ContextRecord->Eip = 0x2F4B1;*/

				// sub_320E0 plays videos
				memset((void*)0x3886C, 0x90, 0x38871 - 0x3886C);

				e->ContextRecord->Eip = 0x3886C;

				DbgPrintf("EmuMain (0x%X): Unreal Championship hack 2 applied!\n", GetCurrentThreadId());

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}

			// Unreal Championship hack 3
			if(/*e->ContextRecord->Eip == 0x260D5 || */e->ContextRecord->Eip == 0x262B4 /*||
				e->ContextRecord->Eip == 0x262FC || e->ContextRecord->Eip == 0x2632B ||
				e->ContextRecord->Eip == 0x26158*/)
			{
				memset((void*)0x2734C, 0x90, 0x27351 - 0x2734C);

				e->ContextRecord->Eip = 0x2734C + (0x27351 - 0x2734C);

				DbgPrintf("EmuMain (0x%X): Unreal Championship hack 3 applied!\n", GetCurrentThreadId());

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// blueshogun96 5/28/07
	// Yup, another one of my crappy hacks.  This one is for Azurik, Rize of Perathia.
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			// There's a D3D function not being hijacked here, I don't have the 3911 XDK
			// so this is the best I could come up with atm.  Based off of the Halo BINK
			// skip hack.
			if(e->ContextRecord->Eip == 0x11D695)
			{
				DbgPrintf("EmuMain (0x%X): Azurik: Rize of Perathia hack 1 applied!\n", GetCurrentThreadId());
				memset((void*) 0x11D690, 0x90, 0x11D6B7 - 0x11D690);

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}

			// Ditto...
			if(e->ContextRecord->Eip == 0x11D63B)
			{
				DbgPrintf("EmuMain (0x%X): Azurik: Rize of Perathia hack 2 applied!\n", GetCurrentThreadId());
				memset((void*) 0x11D630, 0x90, 0x11D679 - 0x11D630);

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// Check for Dr. Muto hack
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			if(e->ContextRecord->Eip == 0xCF147)
			{
				// Skip movzx edx, byte ptr [esi]
			//	e->ContextRecord->Eip += 3;

				DbgPrintf("EmuMain (0x%X): Dr. Muto hack applied!\n", GetCurrentThreadId());
				memset((void*) 0xCF0E0, 0x90, 0xCF175 - 0xCF0E0);

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// Genma Onimusha
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			// There's a D3D function not being hijacked here, I don't have the 3911 XDK
			// so this is the best I could come up with atm.  Based off of the Halo BINK
			// skip hack.
			if(e->ContextRecord->Eip == 0x185485)
			{
				DbgPrintf("EmuMain (0x%X): Genma Onimusha hack 1 applied!\n", GetCurrentThreadId());
				memset((void*) 0x185480, 0x90, 0x18549D - 0x185480);

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// blueshogun96 3/03/08
	// It looks like alot of 4831 games are crashing from the same code in the .text segment
	// (movzx ecx, word ptr [ecx+4]) and it's in a function located in many 4831 games.  Just
	// skip the instruction for now.
	//{
	//	if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
	//	{
	//		if(e->ContextRecord->Eip == 0x1E355E || e->ContextRecord->Eip == 0x5A2882)
	//		{
	//			DbgPrintf("EmuMain (0x%X): XDK 4831 Hack 1 applied!\n", GetCurrentThreadId());
	//		//	memset((void*) e->ContextRecord->Eip, 0x90, 0x4);
	//			e->ContextRecord->Eip += 4;

	//			g_bEmuException = false;

	//			return EXCEPTION_CONTINUE_EXECUTION;
	//		}
	//	}
	//}

	// blueshogun96 4/6/08 7:33 PM
	// An app that uses DirectMusic for audio usually shows an exception dialog
	// and emulator keeps on running.  I'm putting a stop to that right now :P
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			if(e->ContextRecord->Eip == 0x77F7E306)
			{
				// Floods the console...
				// DbgPrintf( "EmuMain (0x%X): DirectMusic hack applied!\n", GetCurrentThreadId() );

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// blueshogun96 4/6/08 9:12 PM
	// Robotech Battlecry
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			if(e->ContextRecord->Eip == 0x16CB95)
			{
				// Fill CMcpxBuffer_Stop with nops (because Cxbx seems not to want to HLE this function)
				memset( (void*) 0x16CB91, 0x90, 0x16CC21-0x16CB91 );

				DbgPrintf( "EmuMain (0x%X): Robotech Battlecry hack 1 applied!\n", GetCurrentThreadId() );
				__asm int 3;

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

	// blueshogun96 4/26/08
	// Blade II (4627)
	{
		if(e->ExceptionRecord->ExceptionCode == 0xC0000005)
		{
			if(e->ContextRecord->Eip == 0x7C34FEDC)
			{
			//	DbgPrintf( "EmuMain (0x%X): Blade II Hack applied!\n", GetCurrentThreadId() );

				g_bEmuException = false;

				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}
	}

    // print debug information
    {
        if(e->ExceptionRecord->ExceptionCode == 0x80000003)
            printf("EmuMain (0x%X): Recieved Breakpoint Exception (int 3)\n", GetCurrentThreadId());
        else
            printf("EmuMain (0x%X): Recieved Exception (Code := 0x%.08X)\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode);

        printf("\n"
            " EIP := 0x%.08X EFL := 0x%.08X\n"
            " EAX := 0x%.08X EBX := 0x%.08X ECX := 0x%.08X EDX := 0x%.08X\n"
            " ESI := 0x%.08X EDI := 0x%.08X ESP := 0x%.08X EBP := 0x%.08X\n"
            "\n",
            e->ContextRecord->Eip, e->ContextRecord->EFlags,
            e->ContextRecord->Eax, e->ContextRecord->Ebx, e->ContextRecord->Ecx, e->ContextRecord->Edx,
            e->ContextRecord->Esi, e->ContextRecord->Edi, e->ContextRecord->Esp, e->ContextRecord->Ebp);
    }

    fflush(stdout);

    // notify user
    {
        char buffer[256];

        if(e->ExceptionRecord->ExceptionCode == 0x80000003)
        {
            sprintf(buffer, 
                "Recieved Breakpoint Exception (int 3) @ EIP := 0x%.08X\n"
                "\n"
                "  Press Abort to terminate emulation.\n"
                "  Press Retry to debug.\n"
                "  Press Ignore to continue emulation.",
                e->ContextRecord->Eip, e->ContextRecord->EFlags);

            e->ContextRecord->Eip += 1;

            int ret = MessageBox(g_hEmuWindow, buffer, "Cxbx", MB_ICONSTOP | MB_ABORTRETRYIGNORE);

            if(ret == IDABORT)
            {
                printf("EmuMain (0x%X): Aborting Emulation\n", GetCurrentThreadId());
                fflush(stdout);

                if(CxbxKrnl_hEmuParent != NULL)
                    SendMessage(CxbxKrnl_hEmuParent, WM_PARENTNOTIFY, WM_DESTROY, 0);

                ExitProcess(1);
            }
            else if(ret == IDIGNORE)
            {
                printf("EmuMain (0x%X): Ignored Breakpoint Exception\n", GetCurrentThreadId());

                g_bEmuException = false;

                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        else
        {
            sprintf(buffer, 
                "Recieved Exception Code 0x%.08X @ EIP := 0x%.08X\n"
                "\n"
                "  Press \"OK\" to terminate emulation.\n"
                "  Press \"Cancel\" to debug.",
                e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip, e->ContextRecord->EFlags);

            if(MessageBox(g_hEmuWindow, buffer, "Cxbx", MB_ICONSTOP | MB_OKCANCEL) == IDOK)
            {
                printf("EmuMain (0x%X): Aborting Emulation\n", GetCurrentThreadId());
                fflush(stdout);

                if(CxbxKrnl_hEmuParent != NULL)
                    SendMessage(CxbxKrnl_hEmuParent, WM_PARENTNOTIFY, WM_DESTROY, 0);

                ExitProcess(1);
            }
        }
    }

    g_bEmuException = false;

    return EXCEPTION_CONTINUE_SEARCH;
}

// check how many bytes were allocated for a structure
extern int EmuCheckAllocationSize(LPVOID pBase, bool largeBound)
{
    MEMORY_BASIC_INFORMATION MemoryBasicInfo;

    DWORD dwRet;
#ifdef _DEBUG_ALLOC
    dwRet = CxbxVirtualQueryDebug(pBase, &MemoryBasicInfo, sizeof(MemoryBasicInfo));
    if (dwRet == -1)
#endif
    dwRet = VirtualQuery(pBase, &MemoryBasicInfo, sizeof(MemoryBasicInfo));

    if(dwRet == 0)
        return 0;

    if(MemoryBasicInfo.State != MEM_COMMIT)
        return 0;

    // this is a hack in order to determine when pointers come from a large write-combined database
    if(largeBound && MemoryBasicInfo.RegionSize > 5*1024*1024)
        return -1;

    return MemoryBasicInfo.RegionSize - ((DWORD)pBase - (DWORD)MemoryBasicInfo.BaseAddress);
}

// exception handle for that tough final exit :)
int ExitException(LPEXCEPTION_POINTERS e)
{
    if(EmuIsXboxFS())
        EmuSwapFS();

    static int count = 0;

    // debug information
    printf("EmuMain (0x%X): * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());
    printf("EmuMain (0x%X): Recieved Exception [0x%.08X]@0x%.08X\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
    printf("EmuMain (0x%X): * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());

    fflush(stdout);

    MessageBox(g_hEmuWindow, "Warning: Could not safely terminate process!", "Cxbx", MB_OK);

    count++;

    if(count > 1)
    {
        MessageBox(g_hEmuWindow, "Warning: Multiple Problems!", "Cxbx", MB_OK);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if(CxbxKrnl_hEmuParent != NULL)
        SendMessage(CxbxKrnl_hEmuParent, WM_PARENTNOTIFY, WM_DESTROY, 0);

    ExitProcess(1);

    return EXCEPTION_CONTINUE_SEARCH;
}
