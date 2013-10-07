// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"
#include "resource2.h"

#include "MainDlg.h"
#include "install.h"
#include <bzswin/security.h>
#include <bzscmn/file.h>
#include <bzswin/wow64.h>
#include <bzswin/registry.h>

using namespace BazisLib;
using namespace BootEditor;

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	// center the dialog on the screen
	CenterWindow();

	// set icons
	HICON hIcon = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
		IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
		IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
	SetIcon(hIconSmall, FALSE);

	bHandled = FALSE;

	DlgResize_Init();

	ManagedPointer<AIBootConfigurationEntry> pEntry;
	bool bAlreadyInstalled = false;

	ManagedPointer<AIBootConfigurationEditor> pEditor = CreateConfigurationEditor();
	if (!pEditor)
	{
		::MessageBox(HWND_DESKTOP,
			_T("Cannot access boot configuration data"),
			NULL,
			MB_ICONERROR);
		EndDialog(0);
		return 0;
	}

	ActionStatus st = FindBestOSEntry(&pEntry, &bAlreadyInstalled, pEditor);
	if (!st.Successful())	
	{
		::MessageBox(HWND_DESKTOP,
				   String::sFormat(_T("Cannot access boot configuration data: %s"), st.GetMostInformativeText().c_str()).c_str(),
				   NULL,
				   MB_ICONERROR);
		EndDialog(0);
		return 0;
	}

	m_ExistingEntryName = pEntry->GetDescription();
	SetDlgItemText(IDC_REUSEENTRY, String::sFormat(_T("Use existing entry (%s)"), m_ExistingEntryName.c_str()).c_str());
	SendDlgItemMessage(IDC_SETDEFAULT, BM_SETCHECK, BST_CHECKED);

	SendDlgItemMessage(bAlreadyInstalled ? IDC_REUSEENTRY : IDC_NEWENTRY, BM_SETCHECK, BST_CHECKED);
	::EnableWindow(GetDlgItem(IDC_ENTRYNAME), !bAlreadyInstalled);

	if (m_ExistingEntryName.ifind(_T("[VirtualKD]")) != -1)
	{
		SendDlgItemMessage(IDC_ADDSUFFIX, BM_SETCHECK, BST_UNCHECKED);
		::EnableWindow(GetDlgItem(IDC_ADDSUFFIX), FALSE);
		SetDlgItemText(IDC_ENTRYNAME, m_ExistingEntryName.c_str());
	}
	else
	{
		SendDlgItemMessage(IDC_ADDSUFFIX, BM_SETCHECK, BST_CHECKED);
		SetDlgItemText(IDC_ENTRYNAME, (m_ExistingEntryName + _T(" [VirtualKD]")).c_str());
	}

	SetDlgItemInt(IDC_TIMEOUT, pEditor->GetTimeout());

#ifdef SUPPORT_VISUALDDK
	SendDlgItemMessage(IDC_INSTALLVDDK, BM_SETCHECK, BST_CHECKED);

	TCHAR tszWinDir[MAX_PATH] = {0,};
	GetWindowsDirectory(tszWinDir, _countof(tszWinDir));

	TCHAR *p = _tcschr(tszWinDir, '\\');
	if (p)
		p[1] = 0;

	SetDlgItemText(IDC_VDDKLOCATION, tszWinDir);

	SetWindowText(_T("Install VirtualKD and VisualDDK Monitor on Virtual Machine"));

#else
	::EnableWindow(GetDlgItem(IDC_INSTALLVDDK), FALSE);
	::EnableWindow(GetDlgItem(IDC_VDDKLOCATION), FALSE);
#endif

	if (_tcsstr(GetCommandLine(), _T("/AUTO")))
		PostMessage(WM_COMMAND, IDOK);

	return TRUE;
}

static ActionStatus SaveResourceToFile(const FilePath &filePath, LPCTSTR lpResType, DWORD dwID)
{
	HMODULE hModule = GetModuleHandle(NULL);
	HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(dwID), lpResType);
	if (!hRes)
		return MAKE_STATUS(ActionStatus::FailFromLastError());

	DWORD len = SizeofResource(hModule, hRes);
	if (!len)
		return MAKE_STATUS(ActionStatus::FailFromLastError());

	HGLOBAL hResObj = LoadResource(hModule, hRes);
	if (!hResObj)
		return MAKE_STATUS(ActionStatus::FailFromLastError());

	PVOID p = LockResource(hResObj);
	if (!p)
		return MAKE_STATUS(ActionStatus::FailFromLastError());

	ActionStatus st;
	File file(filePath, FastFileFlags::CreateOrTruncateRW, &st);
	if (!st.Successful())
		return st;
	file.Write(p, len, &st);
	if (!st.Successful())
		return st;
	return MAKE_STATUS(Success);
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	bool createNewEntry = false, setDefault = false;
	String entryName, monitorLocation;
	unsigned timeout = -1;

	if (SendDlgItemMessage(IDC_SETDEFAULT, BM_GETCHECK) == BST_CHECKED)
		setDefault = true, timeout = GetDlgItemInt(IDC_TIMEOUT);

	if (SendDlgItemMessage(IDC_NEWENTRY, BM_GETCHECK) == BST_CHECKED)
	{
		size_t len = ::GetWindowTextLength(GetDlgItem(IDC_ENTRYNAME));
		entryName.SetLength(GetDlgItemText(IDC_ENTRYNAME, entryName.PreAllocate(len, false), len + 1));
		createNewEntry = true;
	}
	else
	{
		entryName = m_ExistingEntryName;
		if (SendDlgItemMessage(IDC_ADDSUFFIX, BM_GETCHECK) == BST_CHECKED)
			entryName += _T(" [VirtualKD]");
	}

#ifdef SUPPORT_VISUALDDK
	if (SendDlgItemMessage(IDC_INSTALLVDDK, BM_GETCHECK) == BST_CHECKED)
	{
		size_t len = ::GetWindowTextLength(GetDlgItem(IDC_VDDKLOCATION));
		monitorLocation.SetLength(GetDlgItemText(IDC_VDDKLOCATION, monitorLocation.PreAllocate(len, false), len + 1));
	}
#endif

	{
		FilePath fp = FilePath::GetSpecialDirectoryLocation(dirSystem);
		fp.AppendPath(_T("kdbazis.dll"));

#ifdef _WIN64
		bool is64Bit = true;
#else
		bool is64Bit = BazisLib::WOW64APIProvider::sIsWow64Process();
#endif

		ActionStatus st;

		{
			WOW64FSRedirHolder holder;
			st = SaveResourceToFile(fp, _T("KDVMDLL"), is64Bit ? IDR_KDVM64 : IDR_KDVM32);
		}
		if (!st.Successful())
		{
			::MessageBox(HWND_DESKTOP,
				String::sFormat(_T("Cannot create KDBAZIS.DLL: %s"), st.GetMostInformativeText().c_str()).c_str(),
				NULL,
				MB_ICONERROR);
			return 0;
		}

		if (!monitorLocation.empty())
		{
			HWND hMonitor = FindWindow(NULL, _T("DDKLaunchMonitor"));
			if (hMonitor)
			{
				::SendMessage(hMonitor, WM_CLOSE, 0, 0);
				Sleep(1000);
			}

			fp = monitorLocation;
			fp.AppendPath(_T("DDKLaunchMonitor.exe"));

			WOW64FSRedirHolder holder;
			st = SaveResourceToFile(fp, _T("VDDKMON"), is64Bit ? IDR_VDDKMON64 : IDR_VDDKMON32);

			RegistryKey key(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"));
			key[_T("DDKLaunchMonitor")] = fp.c_str();

			if (!st.Successful())
			{
				::MessageBox(HWND_DESKTOP,
					String::sFormat(_T("Cannot create DDKLaunchMonitor.exe: %s"), st.GetMostInformativeText().c_str()).c_str(),
					NULL,
					MB_ICONERROR);
				return 0;
			}
		}
	}

	ActionStatus st = CreateVirtualKDBootEntry(createNewEntry, setDefault, entryName.c_str(), timeout);
	if (!st.Successful())
	{
		::MessageBox(HWND_DESKTOP,
			String::sFormat(_T("Cannot create VirtualKD boot entry: %s"), st.GetMostInformativeText().c_str()).c_str(),
			NULL,
			MB_ICONERROR);
		return 0;
	}
	else
	{
		bool doReboot = false;

		if (_tcsstr(GetCommandLine(), _T("/AUTO")))
		{
			if (_tcsstr(GetCommandLine(), _T("/REBOOT")))
				doReboot = true;
		}
		else
		{
			if (MessageBox(_T("VirtualKD was successfully installed. Please do not forget to run VMMON.EXE on the host machine, or use VisualDDK for debugging.\r\nDo you want to restart your computer now?"),
				_T("Question"),
				MB_ICONQUESTION | MB_YESNO) == IDYES)
				doReboot = true;
		}
		
		if (doReboot)
		{
			BazisLib::Win32::Security::PrivilegeHolder holder(SE_SHUTDOWN_NAME);
			ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_SOFTWARE | SHTDN_REASON_MINOR_INSTALLATION);
		}
	}

	EndDialog(1);
	return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	EndDialog(0);
	return 0;
}

LRESULT CMainDlg::OnBnClickedNewentry(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	::EnableWindow(GetDlgItem(IDC_ENTRYNAME), TRUE);
	return 0;
}

LRESULT CMainDlg::OnBnClickedReuseentry(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	::EnableWindow(GetDlgItem(IDC_ENTRYNAME), FALSE);
	return 0;
}

LRESULT CMainDlg::OnBnClickedSetdefault(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	::EnableWindow(GetDlgItem(IDC_TIMEOUT),
					SendDlgItemMessage(IDC_SETDEFAULT, BM_GETCHECK) == BST_CHECKED);
	return 0;
}
