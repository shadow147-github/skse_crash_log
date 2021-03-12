#include "modlist.h"

ModList::ModList()
{
	this->Modules = NULL;
	HANDLE process = GetCurrentProcess();
	HMODULE handles[1024];
	DWORD needed;

	if (EnumProcessModules(process, handles, sizeof(handles), &needed))
	{
		this->ModuleCount = needed / sizeof(HMODULE);
		this->Modules = (MODULE_INFO *)malloc(sizeof(MODULE_INFO) * this->ModuleCount);
		DWORD i;

		for (i = 0; i < this->ModuleCount; i++)
		{
			GetModuleInfo(process, handles[i], &this->Modules[i]);
		}
	}
}

ModList::~ModList()
{
	if (this->Modules) free(this->Modules);
}

bool ModList::GetModuleInfo(HANDLE process, HMODULE handle, MODULE_INFO * out)
{
	MODULEINFO info;

	if (!GetModuleInformation(process, handle, &info, sizeof(MODULEINFO)))
	{
		return false;
	}

	out->BaseAddress = (DWORD)info.lpBaseOfDll;
	out->ImageSize = info.SizeOfImage;
	char buffer[MAX_PATH];

	if (!GetModuleFileNameEx(process, handle, buffer, sizeof(buffer)))
	{
		return false;
	}

	strcpy_s(out->FileName, MAX_PATH, buffer);
	char * start = strrchr(buffer, '\\'); // Documentation says this is more reliable than GetModuleBaseName

	if (start)
	{
		strcpy_s(out->ModuleName, MAX_PATH, start + 1); // This crashes if strlen(start) > either the destination or the max value
	}
	else
	{
		strcpy_s(out->ModuleName, MAX_PATH, buffer);
	}

	return true;
}

void ModList::Resolve(DWORD address, size_t size, char * pretty)
{
	DWORD i;

	for (i = 0; i < this->ModuleCount; i++)
	{
		auto module = this->Modules[i];
		DWORD end = module.BaseAddress + module.ImageSize;

		if (module.BaseAddress <= address && end >= address)
		{
			DWORD64 displacement = address - module.BaseAddress;
			sprintf_s(pretty, size, "%s+0x%08x", module.ModuleName, displacement);
			return;
		}
	}

	sprintf_s(pretty, size, "<unknown>+0x%08x", address); // Fallback
}