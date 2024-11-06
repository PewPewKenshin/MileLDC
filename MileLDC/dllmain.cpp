// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <GWCA/Utilities/Scanner.h>
#include "MileLDC.h"

DWORD WINAPI Init(HMODULE hModule) noexcept
{
    GW::Scanner::Initialize();

    Entry(hModule);

    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        {
            HANDLE hTread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Init, hModule, 0, 0);
            if (hTread != NULL)
            {
                CloseHandle(hTread);
            }
            break;
        }
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

