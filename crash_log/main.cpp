/*
Crash Log for Skyrim LE

Inspired by .Net Script Framework for Skyrim SE.

SetUnhandledExceptionFilter does not catch crashes in Skyrim for some reason.
It could be that Skyrim sets it own filter and fails to call the previous
handled like it should.

AddVectoredExceptionHandler does catch Skyrim crashes, but it also catches all
other exceptions, even those handled by try...catch and __try...__except.

The .Net Script Framework for Skyrim SE uses a vectored handler to re-set the
unhandled exception filter. This I'd imagine will ensure Skyrim can't
overwrite the filter.

Note that there's probably a lot of issues with this code. Everything from
minor things like using %x instead of %p when printing pointers, to larger
issues that I'm not aware of (memory leaks, crashes, ...).
*/

#include "skse/PluginAPI.h"
#include "skse/skse_version.h"
#include "skse/GameAPI.h"
#include "main.h"

#define _R AddressToModOffset // Because lazy

UInt32 g_version = 4; // Sets plugin version (required by SKSE) and log output etc.
IDebugLog gLog("crash_log.log"); // Not the log containing the dumps
PluginHandle g_pluginHandle = kPluginHandle_Invalid;
SKSESerializationInterface * g_serialization = NULL;
LPTOP_LEVEL_EXCEPTION_FILTER g_original_exception_handler = NULL;
int g_exception_counter = 0;
int g_oeh_called = 0; // How many times the original exception handler has been called
int g_oeh_returned = 0; // How many times the original exception handler has returned

void CreateExceptionHandler()
{
	AddVectoredExceptionHandler(CALL_FIRST, CheckFilter);
	_MESSAGE("Vectored exception handler registered.");
}

void CreateWorkerThread()
{
	// This thread is not used by the "official" crash_log.
	// It was originally created to support functionality requested by praxis22
	CreateThread(NULL, 0, WorkerThreadProc,  NULL, 0, NULL);
}

// This filter makes sure the exception filter is still being used
LONG WINAPI CheckFilter(EXCEPTION_POINTERS * info)
{
	LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(ExceptionHandler);

	if (previous == NULL)
	{
		_MESSAGE("Exception handler set.");
		g_original_exception_handler = NULL;
	}
	else if (previous != ExceptionHandler)
	{
		_MESSAGE("Exception handler replaced.");
		g_original_exception_handler = previous;
	}

	// Continue
	return EXCEPTION_CONTINUE_SEARCH;
}

// Actual exception handler
LONG WINAPI ExceptionHandler(EXCEPTION_POINTERS * info)
{
	/*
	if (code < 0x80000000)
	{
		_MESSAGE("Ignored.");
		return EXCEPTION_CONTINUE_SEARCH;
	}
	*/

	// Arbitrary blocker to prevent spam in case this plugin causes an exception as well
	g_exception_counter++;

	if (g_exception_counter < 7)
	{
		ModList modlist;

		// The original exception handler at CSERHelper.dll+0x00012571 which is a Steam dll seems to
		// cause exceptions as well. These exceptions can't be caught by a try...catch block for some
		// reason; even if /EHa is set.

		// My workaround is going to be to use 2 ints and making sure they are always equal.
		if (g_oeh_called != g_oeh_returned)
		{
			_MESSAGE("Original exception handler caused an exception. Setting pointer to NULL to avoid infinite recursion.");
			g_original_exception_handler = NULL;
			g_oeh_called = 0;
			g_oeh_returned = 0;
			//PrintException(&modlist, info); // Disabled because it might be misleading
		}
		else
		{
			PrintException(&modlist, info);
		}

		// Call original filter
		if (g_original_exception_handler)
		{
			char pretty[48]; // To hold pretty addresses
			_R(&modlist, (DWORD64)g_original_exception_handler, 48, pretty);
			_MESSAGE("Calling original exception handler %s", pretty);

			g_oeh_called++;
			g_original_exception_handler(info);
			g_oeh_returned++;
		}
	}

	// Crash
	_MESSAGE("Continuing code execution (i.e. continue crashing)");
	//return EXCEPTION_CONTINUE_EXECUTION; // Confused about what these actually do.
	return EXCEPTION_EXECUTE_HANDLER; // Seems like this fixed the infinite loop in version 3.
}

void PrintException(ModList * modlist, EXCEPTION_POINTERS * info)
{
	// Who am I?
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	// Open dump file
	time_t rawtime;
	struct tm * timeinfo = (struct tm *)malloc(sizeof(struct tm));
	char dumpTs[32];
	char dumpName[64];

	time(&rawtime);
	localtime_s(timeinfo, &rawtime);
	strftime(dumpTs, sizeof(dumpTs), "%Y_%m_%d_%H_%M", timeinfo);
	sprintf_s(dumpName, sizeof(dumpName), "crash_%s_%d.log", dumpTs, g_exception_counter);

	FILE * file;
	fopen_s(&file, dumpName, "w");

	if (!file)
	{
		_ERROR("ERROR: Failed to create exception dump file %s.", dumpName);
		return;
	}

	// Begin dumping
	char pretty[48]; // To hold pretty addresses
	fprintf(file, "Crash Log, version %d\n", g_version);
	//fprintf(file, "Symbols enabled: %s\n", g_symbols ? "yes" : "no");
	fprintf(file, "Code: 0x%08x.\n", info->ExceptionRecord->ExceptionCode);

	_R(modlist, (DWORD64)info->ExceptionRecord->ExceptionAddress, 48, pretty);
	fprintf(file, "Address: %s.\n", pretty);

	// Some codes have additional information that can be printed
	if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
	{
		ULONG readWrite = info->ExceptionRecord->ExceptionInformation[0];
		ULONG address = info->ExceptionRecord->ExceptionInformation[1];
		_R(modlist, (DWORD64)address, 48, pretty);

		if (readWrite == 0)
		{
			fprintf(file, "Memory at %s could not be read.\n", pretty);
		}
		else if (readWrite == 1)
		{
			fprintf(file, "Memory at %s could not be written.\n", pretty);
		}
	}

	// TODO: Print more information about the exception
	fflush(file);

	PrintModules(modlist, file);
	fflush(file);

	PrintStack(modlist, file, process, thread, info->ContextRecord);
	fflush(file);

	// Done
	fclose(file);
	_MESSAGE("Exception dump created: %s", dumpName);
}

void PrintModules(ModList * modlist, FILE * file)
{
	int i;
	fprintf(file, "\nLoaded modules:\n");
	fprintf(file, "\nBaseAddress EndAddress FileName\n");

	for (i = 0; i < modlist->ModuleCount; i++)
	{
		auto mod = modlist->Modules[i];
		DWORD end = mod.BaseAddress + mod.ImageSize;
		fprintf(file, "0x%08x 0x%08x %s\n", mod.BaseAddress, end, mod.FileName);
	}
}

void PrintStack(ModList * modlist, FILE * file, HANDLE process, HANDLE thread, CONTEXT * context)
{
	fprintf(file, "\nStack trace:\n");

	BOOL result;
	STACKFRAME64 frame;
	int frameNum;
	int paramNum;
	char pretty[48]; // To hold pretty addresses

	memset(&frame, 0, sizeof(frame));
	frame.AddrPC.Offset = context->Eip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = context->Ebp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = context->Esp;
	frame.AddrStack.Mode = AddrModeFlat;

	for (frameNum = 0; ; frameNum++)
	{
		result = StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);

		if (!result)
		{
			//DWORD e = GetLastError(); // Not set by StackWalk64
			//_MESSAGE("StackWalk64 failed with error %d\n", e);
			break;
		}

		_R(modlist, frame.AddrPC.Offset, 48, pretty);
		fprintf(file, "\nFrame: %d, FP: %s\n", frameNum, pretty);

		if (frame.AddrPC.Offset == frame.AddrReturn.Offset)
		{
			fprintf(file, "ERROR: Endless recursion detected.\n");
			break;
		}

		for (paramNum = 0; paramNum < 4; paramNum++)
		{
			DWORD64 address = frame.Params[paramNum];

			if (!address) // null
			{
				fprintf(file, "Param %d: 0x%08x (null)\n", paramNum, address);
			}
			else
			{
				char buffer[256];
				_R(modlist, address, 48, pretty);

				if (TryReadCString(address, 256, buffer))
				{
					fprintf(file, "Param %d: %s (char*) \"%s\"\n", paramNum, pretty, buffer);
				}
				else
				{
					fprintf(file, "Param %d: %s (void*)\n", paramNum, pretty);
				}
			}
		}

		_R(modlist, frame.AddrReturn.Offset, 48, pretty);
		fprintf(file, "RET: %s\n", pretty);
	}
}

bool TryReadCString(DWORD64 address, size_t size, char * out)
{
	int index = 0;
	auto * str = (unsigned char *)address;

	__try
	{
		do
		{
			auto c = str[index];
			//if (c == '\0' || !isprint(c)) break;
			if (!isprint(c)) break;
			out[index] = c;
			index++;
		} while (index < size);

		out[index] = 0; // Terminator
		if (index == 0) return false;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

void AddressToModOffset(ModList * modlist, DWORD64 address, size_t size, char * pretty) // 0x00000000 -> module+0x00
{
	modlist->Resolve(address, size, pretty);
}

/*
void SuspendForDebugging() // Bad idea
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return;
	}

	DWORD pid = GetCurrentProcessId();
	DWORD tid = GetCurrentThreadId();
	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);

	if (Thread32First(snapshot, &entry))
	{
		do
		{
			if (entry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID))
			{
				if (entry.th32ThreadID != tid && entry.th32OwnerProcessID == pid)
				{
					HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);

					if (thread)
					{
						SuspendThread(thread);
						CloseHandle(thread);
					}
				}
			}

			entry.dwSize = sizeof(entry);
		} while (Thread32Next(snapshot, &entry));
	}

	// Suspend self
	HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);

	if (thread)
	{
		SuspendThread(thread);
		CloseHandle(thread);
	}
}
*/

DWORD WINAPI WorkerThreadProc(LPVOID param)
{
	_MESSAGE("Worker thread started.");

	/*
	HANDLE wait = CreateEvent(NULL, TRUE, FALSE, "crashlog.praxis22");
	WaitForSingleObject(wait, INFINITE); // Requires outside signal, powershell works
	_MESSAGE("Event triggered.");
	DumpStacksForAllThreads();
	CloseHandle(wait);
	*/

	_MESSAGE("Worker thread stopped.");
	return 0;
}

void DumpStacksForAllThreads() // praxis22
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return;
	}

	DWORD tid = GetCurrentThreadId();
	DWORD pid = GetCurrentProcessId();
	THREADENTRY32 entry;
	//CONTEXT context;
	ModList modlist;

	FILE * file;
	fopen_s(&file, "all_stacks.log", "w");

	if (!file)
	{
		_ERROR("ERROR: Failed to create exception dump file all_stacks.log.");
		return;
	}

	entry.dwSize = sizeof(entry);

	if (Thread32First(snapshot, &entry))
	{
		do
		{
			if (entry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID))
			{
				if (entry.th32ThreadID != tid && entry.th32OwnerProcessID == pid)
				{
					HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);

					if (thread)
					{
						SuspendThread(thread); // Needed?

						CONTEXT context;
						context.ContextFlags = CONTEXT_ALL;
						GetThreadContext(thread, &context);
						fprintf(file, "Dumping stack for thread: %d.\n", entry.th32ThreadID);
						PrintStack(&modlist, file, GetCurrentProcess(), thread, &context);

						ResumeThread(thread);
						CloseHandle(thread);
					}
				}
			}

			entry.dwSize = sizeof(entry);
		} while (Thread32Next(snapshot, &entry));
	}

	fflush(file);
	fclose(file);
}

/*
void Serialization_Revert(SKSESerializationInterface * intfc)
{
	_MESSAGE("revert");
}

void Serialization_Save(SKSESerializationInterface * intfc)
{
	_MESSAGE("save");
}

void Serialization_Load(SKSESerializationInterface * intfc)
{
	_MESSAGE("load");
}
*/

extern "C"
{

bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
{
	info->infoVersion =	PluginInfo::kInfoVersion;
	info->name =		"Crash Log";
	info->version =		g_version;

	g_pluginHandle = skse->GetPluginHandle();

	if(skse->runtimeVersion != RUNTIME_VERSION_1_9_32_0)
	{
		_ERROR("unsupported runtime version %08X", skse->runtimeVersion);

		return false;
	}

	g_serialization = (SKSESerializationInterface *)skse->QueryInterface(kInterface_Serialization);
	if(!g_serialization)
	{
		_ERROR("couldn't get serialization interface");

		return false;
	}

	if(g_serialization->version < SKSESerializationInterface::kVersion)
	{
		_ERROR("serialization interface too old (%d expected %d)", g_serialization->version, SKSESerializationInterface::kVersion);

		return false;
	}

	return true;
}

bool SKSEPlugin_Load(const SKSEInterface * skse)
{
	_MESSAGE("Crash Log, version %d", g_version);
	CreateExceptionHandler();
	//CreateWorkerThread(); // praxis22

	g_serialization->SetUniqueID(g_pluginHandle, '00CL');

	// TODO: These aren't needed right?
	//g_serialization->SetRevertCallback(g_pluginHandle, Serialization_Revert);
	//g_serialization->SetSaveCallback(g_pluginHandle, Serialization_Save);
	//g_serialization->SetLoadCallback(g_pluginHandle, Serialization_Load);

	return true;
}

};
