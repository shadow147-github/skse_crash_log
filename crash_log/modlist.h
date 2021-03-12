#ifndef MODLIST_H
#define MODLIST_H

#include <Windows.h>
#include <Psapi.h>
#include <stdio.h>

struct MODULE_INFO
{
	DWORD BaseAddress;
	DWORD ImageSize;
	char ModuleName[MAX_PATH];
	char FileName[MAX_PATH];
};

/*
typedef struct RESOLVE_INFO
{
	DWORD Displacement;
	TCHAR ModuleName[32];
} *PRESOLVE_INFO;
*/

class ModList
{
protected:
	bool GetModuleInfo(HANDLE process, HMODULE handle, MODULE_INFO * info);
public:
	MODULE_INFO * Modules;
	DWORD ModuleCount;

	ModList();
	~ModList();

	void Resolve(DWORD address, size_t size, char * pretty);
};

#endif // MODLIST_H