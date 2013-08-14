// capcall.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include <Windows.h>
#include <assert.h>
#include <stdio.h>
#include "..\\common\\dllinsert.h"
#include "..\\common\\output_debug.h"


#ifdef _UNICODE
#include <malloc.h>
char** AnsiArgs(int argc,wchar_t* argv[])
{
    char** ppArgv=NULL;
    int i,ret;
    int len;

    ppArgv =(char**) calloc(sizeof(*ppArgv),(argc+1));
    if(ppArgv == NULL)
    {
        goto fail;
    }

    for(i=0; i<argc; i++)
    {
        if(argv[i])
        {
            len = wcslen(argv[i]);
            ppArgv[i] = (char*)calloc(sizeof(char),(len+1)*2);
            if(ppArgv[i] == NULL)
            {
                goto fail;
            }
            SetLastError(0);
            ret = WideCharToMultiByte(CP_ACP,0,argv[i],len,ppArgv[i],(len+1)*2,NULL,NULL);
            if(ret == 0 && GetLastError())
            {
                ret = GetLastError();
                goto fail;
            }
        }
    }

    /*all is ok*/

    return ppArgv;
fail:
    if(ppArgv)
    {
        for(i=0; i<argc; i++)
        {
            if(ppArgv[i])
            {
                free(ppArgv[i]);
            }
            ppArgv[i]=NULL;
        }
        free(ppArgv);
    }
    ppArgv =NULL;

    return NULL;
}
#endif

void Usage(int ec,const char* fmt,...)
{
    va_list ap;
    FILE* fp=stderr;
    if(ec == 0)
    {
        fp = stdout;
    }
    if(fmt)
    {
        va_start(ap,fmt);
        vfprintf(fp,fmt,ap);
        fprintf(fp,"\n");
    }

    fprintf(fp,"capcall [OPTIONS]\n");
    fprintf(fp,"\t-h|--help                   : to display help message\n");
    fprintf(fp,"\t-p|--program  exename       : to specify process execname\n");
    fprintf(fp,"\t-d|--dll   dllname          : to specify dll name\n");
    fprintf(fp,"\t-F|--full  fulldllname      : to specify full dll name\n");
    fprintf(fp,"\t-f|--func  func             : to specify function name\n");
    fprintf(fp,"\t-P|--param param            : to specify parameter for function call\n");
    fprintf(fp,"\t-w|--wait time              : to specify wait time default is 0\n");
    fprintf(fp,"\t--  params                  : to set for parameter call function\n");

    exit(ec);
}

static int st_RunParamIdx = -1;
static char* st_pExecName = NULL;
static char* st_pDllName = NULL;
static char* st_pFullDllName = NULL;
static char* st_pFuncName = NULL;
static char* st_pParam = NULL;
static int st_WaitTime = 0;

void ParseParam(int argc,char* argv[])
{
    int i;

    for(i=1; i<argc; i++)
    {
        if(strcmp(argv[i],"-h")==0 ||
                strcmp(argv[i],"--help") == 0)
        {
            Usage(0,NULL);
        }
        else if(strcmp(argv[i],"-p")==0 ||
                strcmp(argv[i],"--program") == 0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_pExecName = argv[i+1];
            i ++;
        }
        else if(strcmp(argv[i],"-d")==0 ||
                strcmp(argv[i],"--dll") == 0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_pDllName = argv[i+1];
            i ++;
        }
        else if(strcmp(argv[i],"-F")==0 ||
                strcmp(argv[i],"--full") == 0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_pFullDllName = argv[i+1];
            i ++;
        }
        else if(strcmp(argv[i],"-f")==0 ||
                strcmp(argv[i],"--func") == 0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_pFuncName = argv[i+1];
            i ++;
        }
        else if(strcmp(argv[i],"-P")==0 ||
                strcmp(argv[i],"--param") == 0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_pParam = argv[i+1];
            i ++;
        }
        else if(strcmp(argv[i],"-w")==0 ||
                strcmp(argv[i],"--wait") == 0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_WaitTime= atoi(argv[i+1]);
            i ++;
        }
        else if(strcmp(argv[i],"--")==0)
        {
            if((i+1)>=argc)
            {
                Usage(3,"%s need args",argv[i]);
            }
            st_RunParamIdx = i + 1;
            break;

        }
        else
        {
            st_RunParamIdx = i;
            break;
        }
    }

    if(st_pExecName  == NULL)
    {
        Usage(3,"please specify -p|--program");
    }

    if(st_pDllName == NULL && st_pFullDllName == NULL)
    {
        Usage(3,"please specify -d|--dll or -F|--full at least one");
    }

    if(st_pFuncName == NULL)
    {
        Usage(3,"please specify -f|--func");
    }

	if (st_pDllName == NULL)
	{
		st_pDllName = strrchr(st_pFullDllName,'\\');
		if (st_pDllName == NULL )
		{
			st_pDllName = st_pFullDllName;
		}
		else
		{
			st_pDllName += 1;
		}
		DEBUG_INFO("dllname %s\n",st_pDllName);
	}

    return;
}

int AnsiMain(int argc,char* argv[])
{
    char* pCommandLine=NULL,*pCurCommand;
    int commandsize=0,commandlen=0;
    int i;
    int ret,leftlen;
    unsigned int processid=0;
    PVOID pFnAddr=NULL,pRetVal=NULL;
    ParseParam(argc,argv);

    if(st_RunParamIdx >= 0)
    {
        /*now we should make the command line*/
        commandlen = 0;
		commandlen += strlen(st_pExecName);
		commandlen += 2;
        for(i=st_RunParamIdx; i<argc; i++)
        {
            commandlen += strlen(argv[i]);
            commandlen += 5;
        }

        pCommandLine = new char[commandlen];
        assert(pCommandLine);
        pCurCommand = pCommandLine;
        leftlen = commandlen;
		ret = _snprintf_s(pCurCommand,leftlen,leftlen,"%s ",st_pExecName);
		pCurCommand += ret;
		leftlen -= ret;
        for(i=st_RunParamIdx; i<argc; i++)
        {
            ret = _snprintf_s(pCurCommand,leftlen,leftlen,"%s ",argv[i]);
            pCurCommand += ret;
            leftlen -= ret;
        }
    }

	if (pCommandLine)
	{
    	ret = LoadInsert(NULL,pCommandLine,st_pFullDllName,st_pDllName);
	}
	else
	{
		ret = LoadInsert(st_pExecName,NULL,st_pFullDllName,st_pDllName);
	}
    if(ret < 0)
    {
		DEBUG_INFO("\n");
        goto out;
    }

    processid = ret;
    if(st_WaitTime > 0)
    {
        Sleep(st_WaitTime * 1000);
    }

    ret = __GetRemoteProcAddress(processid,st_pDllName,st_pFuncName,&pFnAddr);
    if(ret < 0)
    {
		DEBUG_INFO("\n");
        goto out;
    }

	ret = __CallRemoteFunc(processid,pFnAddr,st_pParam,st_WaitTime,&pRetVal);
	if (ret < 0)
	{
		DEBUG_INFO("\n");
		goto out;
	}
	ret = 0;

out:
    if(pCommandLine)
    {
        delete [] pCommandLine;
    }
    pCommandLine = NULL;

    return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
    int ret;
#ifdef _UNICODE
    char** ppArgv=NULL;
    int i;
    ppArgv = AnsiArgs(argc,argv);
    if(ppArgv == NULL)
    {
        ret = GetLastError() ? GetLastError() : 1;
        ret = -ret;
		DEBUG_INFO("\n");
        goto out;
    }
    ret = AnsiMain(argc,ppArgv);
#else
    ret = AnsiMain(argc,argv);
#endif
#ifdef _UNICODE
out:
    if(ppArgv)
    {
        for(i=0; i<argc; i++)
        {
            if(ppArgv[i])
            {
                free(ppArgv[i]);
            }
            ppArgv[i] = NULL;
        }
        free(ppArgv);
    }
    ppArgv=NULL;
#endif
    return ret;
}

