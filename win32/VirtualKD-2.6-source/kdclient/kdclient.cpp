/*! \file kdclient.cpp
    \author Ivan Shcherbakov (Bazis)
    $Id: kdclient.cpp,v 1.18 2009-09-16 12:40:14 Administrator Exp $
    \brief Contains the VMWare hooking/unhooking entries
*/

#include "stdafx.h"
#include <tchar.h>
#include "../kdvm/kdrpc.h"
#include <bzscmn/bzscmn.h>
#include "tablemgr.h"
#include "../rpcdispatch/rpcdisp.h"
#include "../rpcdispatch/kdcomdisp.h"
#include "../rpcdispatch/reporter.h"

StatusReporter *g_pReporter = NULL;
static RPCTableManager *g_pManager = NULL;
static volatile LONG s_CallCount = 0;
static bool s_bUnload = false;

static HANDLE s_hThread = INVALID_HANDLE_VALUE;

extern HMODULE g_hThisDll;

//! Provides convenient way to mark whether a code block is executing by using an global volatile counter
class InterlockedIncrementer
{
private:
	(volatile LONG) *m_pPtr;

public:
	inline InterlockedIncrementer(volatile LONG *pPtr)
		: m_pPtr(pPtr)
	{
		InterlockedIncrement(m_pPtr);
	}

	~InterlockedIncrementer()
	{
		InterlockedDecrement(m_pPtr);
	}
};

#ifdef KDVMWARE_USE_PROXY

int MaxDelay = 0;
static char ReplyBuffer[262144];

static bool KDRPCProxyHandler(void *pContext, int ChannelNumber, char *pCommandBody, unsigned CommandBodyLength, char **ppReply, unsigned *pReplyLen)
{
	InterlockedIncrementer inc(&s_CallCount);
	DWORD done = 0;
	DWORD tick = GetTickCount();
	for (;;)
	{
		BOOL b = CallNamedPipe(_T("\\\\.\\pipe\\kdvmware_proxypipe"), pCommandBody, CommandBodyLength, ReplyBuffer, sizeof(ReplyBuffer), &done, INFINITE);
		if (b)
		{
			int delay = (int)(GetTickCount() - tick);
			if (delay > MaxDelay)
				MaxDelay = delay;
			break;
		}
		int er = GetLastError();
		if ((GetTickCount() - tick) > 1000)
		{
			*pReplyLen = 0;
			return false;
		}
	}
	*pReplyLen = done;
	*ppReply = ReplyBuffer;
	return true;
}

#else

static KdRpcDispatcher *s_pClient = NULL;

static bool KDRPCDirectHandler(void *pContext, int ChannelNumber, char *pCommandBody, unsigned CommandBodyLength, char **ppReply, unsigned *pReplyLen)
{
	InterlockedIncrementer inc(&s_CallCount);
	if (!s_pClient)
		return false;
	*pReplyLen = s_pClient->OnRequest(pCommandBody, CommandBodyLength, ppReply);
	return true;
}

#endif

#include "cmdline.h"
unsigned SessionNameFromVMCmdLineW(const RemoteProcessInfo &info, wchar_t *pName, size_t MaxNameLength);

DWORD CALLBACK  PatcherThreadMain(LPVOID lpParam)
{
	HINSTANCE hThisDll = (HINSTANCE)lpParam;
#ifndef KDVMWARE_USE_PROXY
	TCHAR tsz[MAX_PATH] = {0,};
	
	String pipeName = _T("\\\\.\\pipe\\kd_");
	String pipeEnding;
	if (SessionNameFromVMCmdLineW(GetLocalProcessInfo(), tsz, sizeof(tsz)/sizeof(tsz[0])) && tsz[0])
	{
		pipeEnding = tsz;
	}
	else
	{
		GetCurrentDirectory(__countof(tsz), tsz);
		FilePath fp(tsz);
		pipeEnding = fp.GetFileName();
	}

	size_t pos = -1;
	for (;;)
	{
		pos = pipeEnding.find(' ', pos + 1);
		if (pos == String::npos)
			break;
		pipeEnding[pos] = '_';
	}
	pipeName += pipeEnding;
	wcsncpy(g_pReporter->GetStatusPointer()->PipeName, pipeName.c_str(), __countof(g_pReporter->GetStatusPointer()->PipeName));
	s_pClient = new KdRpcDispatcher(new KdComDispatcher(pipeName.c_str()));
#endif

	g_pManager = new RPCTableManager(hThisDll);
	bool forceRepatch = false;
	while (!s_bUnload)
	{
#ifdef KDVMWARE_USE_PROXY
		if (!g_pManager->InstallHandler(g_szRPCCommandHeader, sizeof(g_szRPCCommandHeader)-2, KDRPCProxyHandler, NULL, forceRepatch))
#else
		if (!g_pManager->InstallHandler(g_szRPCCommandHeader, sizeof(g_szRPCCommandHeader)-2, KDRPCDirectHandler, NULL, forceRepatch))
#endif
		{
			g_pReporter->GetStatusPointer()->PatchErrorPlus1 = ERROR_UNIDENTIFIED_ERROR + 1;
			return 0;
		}
		g_pReporter->GetStatusPointer()->PatchErrorPlus1 = ERROR_SUCCESS + 1;
		if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
			g_pReporter->LogLineIfEnabled(_T("VMWare reset monitor activated...\r\n"));
		while (!s_bUnload)
		{
			Sleep(1000);
			if (g_pManager->IsEntryModified())
			{
				g_pReporter->GetStatusPointer()->PatchErrorPlus1 = 0;
				if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
					g_pReporter->LogLineIfEnabled(_T("Virtual machine reset. RPC handler table reinitialized.\r\nPatching table once again...\r\n"));
				Sleep(200);
				g_pReporter->GetStatusPointer()->OSDetected = false;
				forceRepatch = true;
				break;
			}
		}
	}
	if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
		g_pReporter->LogLineIfEnabled(_T("VMWare reset monitor thread: exitting\r\n"));
	return 0;
}

bool IsVMWareExecutable(LPCTSTR lpFullExePath)
{
	return _tcsstr(lpFullExePath, _T("vmware-vmx.exe")) || _tcsstr(lpFullExePath, _T("vmware-vmx-debug.exe")) || _tcsstr(lpFullExePath, _T("vmware-vmx-stats.exe"));
}

bool HookVmware(HINSTANCE hThisDll)
{
	TCHAR tsz[MAX_PATH];
	GetModuleFileName(GetModuleHandle(0), tsz, __countof(tsz));
	if (!IsVMWareExecutable(tsz))
		return true;
	DisableThreadLibraryCalls((HMODULE)hThisDll);
	s_CallCount = 0;
	g_pReporter = new StatusReporter();
	ASSERT(g_pReporter);
	s_bUnload = false;
	DWORD dwID;
	s_hThread = CreateThread(NULL, 0, PatcherThreadMain, (LPVOID)hThisDll, 0, &dwID);
	if (s_hThread == INVALID_HANDLE_VALUE)
		return false;
	return true;
}

void CleanupOnForcibleDLLUnload()
{
	if (!g_pReporter)
		return;
#ifndef KDVMWARE_USE_PROXY
	delete s_pClient;
	s_pClient = NULL;
#endif
}


extern "C" void CALLBACK VMWareUnhookThread(LPVOID)
{
	if (!g_pReporter)
		return;
	if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
		g_pReporter->LogLineIfEnabled(_T("Waiting for reset monitor thread to terminate...\r\n"));
	s_bUnload = true;
	WaitForSingleObject(s_hThread, INFINITE);
	CloseHandle(s_hThread);
	if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
		g_pReporter->LogLineIfEnabled(_T("Unpatching RPC handler table...\r\n"));

	if (g_pManager)
		g_pManager->RestoreOriginalHandler();

	if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
		g_pReporter->LogLineIfEnabled(_T("Waiting for all previously started RPC calls to complete...\r\n"));

	for (int i = 0; i < 10; i++)
	{
		if (s_CallCount)
			i = 0;
		Sleep(100);
	}

	if (s_EnableLogging && (g_pReporter->GetStatusPointer()->DebugLevel >= DebugLevelTracePatching))
		g_pReporter->LogLineIfEnabled(_T("Unloading...\r\n"));

	delete g_pManager;
	g_pManager = NULL;
#ifndef KDVMWARE_USE_PROXY
	delete s_pClient;
	s_pClient = NULL;
#endif
	delete g_pReporter;
	g_pReporter = NULL;
	FreeLibraryAndExitThread(g_hThisDll, 0);
}

//-------------------------------------------------------------------------------------------------------------------

extern "C"
{
	__declspec(dllexport) unsigned VBoxHandler(char *pCommandBody, unsigned CommandBodyLength, char **ppReply);
	__declspec(dllexport) void VBoxInit();
	__declspec(dllexport) void VBoxShutdown();
};

unsigned VBoxHandler(char *pCommandBody, unsigned CommandBodyLength, char **ppReply)
{
#ifdef KDVMWARE_USE_PROXY
#error Not implemented
#else
	InterlockedIncrementer inc(&s_CallCount);
	if (!s_pClient)
		return 0;
	return s_pClient->OnRequest(pCommandBody, CommandBodyLength, ppReply);
#endif
}

#include "../VBoxDD/VBoxCmdLine.h"

void VBoxInit()
{
	g_pReporter = new StatusReporter();
#ifndef KDVMWARE_USE_PROXY
	wchar_t wszPipeName[MAX_PATH] = {0,};
	if (!VBoxCmdLineToPipeNameW(wszPipeName, __countof(wszPipeName)))
		return;
	ASSERT(g_pReporter);
	wcsncpy(g_pReporter->GetStatusPointer()->PipeName, wszPipeName, __countof(g_pReporter->GetStatusPointer()->PipeName));
#else
	wcsncpy(g_pReporter->GetStatusPointer()->PipeName, L"\\\\.\\pipe\\kdvmware_proxypipe", __countof(g_pReporter->GetStatusPointer()->PipeName));
#endif
	s_pClient = new KdRpcDispatcher(new KdComDispatcher(wszPipeName));
	g_pReporter->GetStatusPointer()->PatchErrorPlus1 = 1;
}

void VBoxShutdown()
{
	CleanupOnForcibleDLLUnload();
}
