#ifndef MAIN_H
#define MAIN_H

#include <Windows.h>
#include <time.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#include "modlist.h"

#define CALL_LAST 0
#define CALL_FIRST 1

void CreateExceptionHandler();
void CreateWorkerThread();
LONG WINAPI CheckFilter(EXCEPTION_POINTERS * info);
LONG WINAPI ExceptionHandler(EXCEPTION_POINTERS * info);
void PrintException(ModList * modlist, EXCEPTION_POINTERS * info);
void PrintModules(ModList * modlist, FILE * file);
void PrintStack(ModList * modlist, FILE * file, HANDLE process, HANDLE thread, CONTEXT * context);
bool TryReadCString(DWORD64 address, size_t size, char * out);
void AddressToModOffset(ModList * modlist, DWORD64 address, size_t size, char * pretty);

// praxis22
DWORD WINAPI WorkerThreadProc(LPVOID param);
void DumpStacksForAllThreads();

#endif // MAIN_H