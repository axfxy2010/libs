// dllcall.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "..\\dllex\\dllex.h"

#pragma comment(lib,"dllex.lib")
int _tmain(int argc, _TCHAR* argv[])
{
	fprintf(stdout,"call proc (%d) PrintFunc 0x%p RepeatFunc 0x%p\n",GetCurrentProcessId(),&PrintFunc,&RepeatFunc);
	PrintFunc("main function");
	RepeatFunc("main function");
	while(1)
	{
		Sleep(1000);
	}
	return 0;
}

