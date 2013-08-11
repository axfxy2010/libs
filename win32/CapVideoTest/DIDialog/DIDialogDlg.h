
// DIDialogDlg.h : header file
//

#pragma once


// CDIDialogDlg dialog
class CDIDialogDlg : public CDialogEx
{
// Construction
public:
	CDIDialogDlg(CWnd* pParent = NULL);	// standard constructor
	CString m_strExe;
	CString m_strDll;
	CString m_strBmp;

// Dialog Data
	enum { IDD = IDD_DIDIALOG_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
};
