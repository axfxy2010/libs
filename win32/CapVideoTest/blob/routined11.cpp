
#include "iris-int.h"
#include "d3d11.h"
#include "..\\detours\\detours.h"
#include "..\\common\\output_debug.h"
#include <assert.h>
#include <vector>
#include "..\\common\\uniansi.h"
#include <D3DX11tex.h >
#include "..\\common\\StackWalker.h"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dx11.lib")

#define COM_METHOD(TYPE, METHOD) TYPE STDMETHODCALLTYPE METHOD


extern "C" int RoutineDetourD11(void);
extern "C" void RotineClearD11(void);
extern "C" __declspec(dllexport) int CaptureFullScreenFileD11(const char* filetosave);

#define   POINTER_STATE_FREE  0
#define   POINTER_STATE_HOLD 1
#define   POINTER_STATE_GRAB  -1

typedef struct _RegisterD11Pointers
{
    ID3D11Device *m_pDevice;
    int m_DeviceState;
    int m_DeviceHoldCount;
    ID3D11DeviceContext *m_pDeviceContext;
    int m_DeviceContextState;
    int m_DeviceContextHoldCount;
    IDXGISwapChain *m_pSwapChain;
    int m_SwapChainState;
    int m_SwapChainHoldCount;
} RegisterD11Pointers_t;


static int st_D3D11EnvInit=0;
static CRITICAL_SECTION st_PointerLock;
static std::vector<RegisterD11Pointers_t*> st_D11PointersVec;


void __SnapShotDeivces(const char* file,const char* func,int lineno)
{
    unsigned int i;
    RegisterD11Pointers_t *pPointer=NULL;
    EnterCriticalSection(&st_PointerLock);
    DEBUG_INFO("[%s:%s:%d]\tPointers [%d]\n",file,func,lineno,st_D11PointersVec.size());
    for(i=0; i<st_D11PointersVec.size(); i++)
    {
        pPointer = st_D11PointersVec[i];
        DEBUG_INFO("[%d] device (0x%p:%d:%d) devicecontext (0x%p:%d:%d) swapchain (0x%p:%d:%d)\n",
                   i,pPointer->m_pDevice,pPointer->m_DeviceState,pPointer->m_DeviceHoldCount,
                   pPointer->m_pDeviceContext,pPointer->m_DeviceContextState,pPointer->m_DeviceContextHoldCount,
                   pPointer->m_pSwapChain,pPointer->m_SwapChainState,pPointer->m_SwapChainHoldCount);
        pPointer = NULL;
    }
    LeaveCriticalSection(&st_PointerLock);
    return;
}


static int UnRegisterAllD11Pointers(void)
{
    RegisterD11Pointers_t *pPointer=NULL;
    int cont,wait;
    int count =0;
    BOOL bret;
    unsigned int idx;

    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);

    do
    {
        cont = 0;
        wait = 0;
        assert(pPointer == NULL);
        DEBUG_INFO("\n");
        EnterCriticalSection(&st_PointerLock);
        DEBUG_INFO("st_D11PointersVec.size %ld\n",st_D11PointersVec.size());
        if(st_D11PointersVec.size() > 0)
        {
            idx = st_D11PointersVec.size();
            idx --;
            pPointer = st_D11PointersVec[idx];
            if(pPointer->m_DeviceState == POINTER_STATE_FREE &&
                    pPointer->m_DeviceContextState == POINTER_STATE_FREE &&
                    pPointer->m_SwapChainState == POINTER_STATE_FREE)
            {
                /*free this one*/
                count += 1;
                st_D11PointersVec.erase(st_D11PointersVec.begin()+idx);
                if(st_D11PointersVec.size() > 0)
                {
                    cont = 1;
                }
            }
            else
            {
                wait = 1;
                pPointer = NULL;
            }
        }
        LeaveCriticalSection(&st_PointerLock);
        DEBUG_INFO("pPointer 0x%p\n",pPointer);
        if(pPointer)
        {
            ULONG ul;
            if(pPointer->m_pDevice)
            {
                DEBUG_INFO("Device[0x%p]\n",pPointer->m_pDevice);
                ul = pPointer->m_pDevice->Release();
                DEBUG_INFO("Device[0x%p] realease count %ld\n",pPointer->m_pDevice,ul);
            }
            pPointer->m_pDevice = NULL;

            if(pPointer->m_pDeviceContext)
            {
                ul = pPointer->m_pDeviceContext->Release();
                DEBUG_INFO("DeviceContext[0x%p] release count(%ld)\n",pPointer->m_pDeviceContext,ul);
            }
            pPointer->m_pDeviceContext = NULL;

            if(pPointer->m_pSwapChain)
            {
                ul = pPointer->m_pSwapChain->Release();
                DEBUG_INFO("SwapChain[0x%p] release count (%ld)\n",pPointer->m_pSwapChain,ul);
            }
            pPointer->m_pSwapChain = NULL;

            delete pPointer;
        }
        pPointer = NULL;

        if(wait)
        {
            bret = SwitchToThread();
            if(!bret)
            {
                Sleep(10);
            }
        }
    }
    while(cont || wait);

    DEBUG_INFO("UnRegister count %d\n",count);

    return count;
}

static void CleanupD11Environment(void)
{
    if(st_D3D11EnvInit)
    {
        DEBUG_INFO("\n");
        UnRegisterAllD11Pointers();
        /*we do not destroy critical section because ,we can not restore the detours
        so we do not set st_D3D11EnvInit = 0*/
    }
}

static int InitializeD11Environment(void)
{
    if(st_D3D11EnvInit == 0)
    {
        InitializeCriticalSection(&st_PointerLock);
        assert(st_D11PointersVec.size() == 0);
        st_D3D11EnvInit = 1;
    }
    return 0;
}


int RegisterSwapChainWithDevice(IDXGISwapChain *pSwapChain,ID3D11Device* pDevice,ID3D11DeviceContext* pDeviceContext)
{
    RegisterD11Pointers_t *pPointer=NULL,*pCurPointer=NULL;
    int failed=0;
    unsigned int i;


    pPointer = new RegisterD11Pointers_t;
    pPointer->m_pDevice = NULL;
    pPointer->m_DeviceState = POINTER_STATE_FREE;
    pPointer->m_DeviceHoldCount = 0;
    pPointer->m_pDeviceContext = NULL;
    pPointer->m_DeviceContextState = POINTER_STATE_FREE;
    pPointer->m_DeviceContextHoldCount = 0;
    pPointer->m_pSwapChain = NULL;
    pPointer->m_SwapChainState = POINTER_STATE_FREE;
    pPointer->m_SwapChainHoldCount = 0;

    if(pSwapChain)
    {
        pSwapChain->AddRef();
    }
    if(pDevice)
    {
        pDevice->AddRef();
    }
    if(pDeviceContext)
    {
        pDeviceContext->AddRef();
    }

    pPointer->m_pDevice  = pDevice;
    pPointer->m_pDeviceContext = pDeviceContext;
    pPointer->m_pSwapChain= pSwapChain;

    failed = 0;

    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
    EnterCriticalSection(&st_PointerLock);
    for(i=0; i<st_D11PointersVec.size() ; i++)
    {
        pCurPointer = st_D11PointersVec[i];
        if((pCurPointer->m_pDevice && pCurPointer->m_pDevice == pPointer->m_pDevice) ||
                (pCurPointer->m_pDeviceContext && pCurPointer->m_pDeviceContext == pPointer->m_pDeviceContext) ||
                (pCurPointer->m_pSwapChain && pCurPointer->m_pSwapChain == pPointer->m_pSwapChain))
        {
            /*has register conflict*/
            failed = 1;
            break;
        }
    }

    if(failed == 0)
    {
        /*now insert the push*/
        st_D11PointersVec.push_back(pPointer);

        DEBUG_INFO("push  pointer [0x%p] [%d]\n",pPointer,st_D11PointersVec.size() - 1);
    }
    LeaveCriticalSection(&st_PointerLock);

    if(failed)
    {
        goto fail;
    }

    /*now insert ok*/
    return 1;
fail:
    if(pPointer)
    {
        if(pPointer->m_pDevice)
        {
            pPointer->m_pDevice->Release();
        }
        pPointer->m_pDevice = NULL;

        if(pPointer->m_pDeviceContext)
        {
            pPointer->m_pDeviceContext->Release();
        }
        pPointer->m_pDeviceContext = NULL;

        if(pPointer->m_pSwapChain)
        {
            pPointer->m_pSwapChain->Release();
        }
        pPointer->m_pSwapChain = NULL;

        delete pPointer;
    }
    pPointer = NULL;
    return 0;
}


static int RegisterFactory1CreateSwapDevice(ID3D11Device* pDevice,IDXGISwapChain *pSwapChain)
{
    int failed=1;
    unsigned int i;
    RegisterD11Pointers_t *pCurPointer=NULL;

    if(pSwapChain == NULL || pDevice  == NULL)
    {
        return 0;
    }

    pSwapChain->AddRef();
    failed = 1;
    DEBUG_INFO("RegisterCreate device 0x%p swapchain 0x%p\n",pDevice,pSwapChain);
    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
    EnterCriticalSection(&st_PointerLock);
    for(i=0; i<st_D11PointersVec.size(); i++)
    {
        pCurPointer = st_D11PointersVec[i];
        if(pCurPointer->m_pDevice && pCurPointer->m_pDevice == pDevice &&
                pCurPointer->m_pSwapChain == NULL)
        {
            assert(pCurPointer->m_SwapChainState == POINTER_STATE_FREE);
            assert(pCurPointer->m_SwapChainHoldCount == 0);
            pCurPointer->m_pSwapChain = pSwapChain;
            failed = 0;
            break;
        }
    }
    LeaveCriticalSection(&st_PointerLock);

    if(failed)
    {
        goto fail;
    }
    return 1;
fail:
    if(pSwapChain)
    {
        pSwapChain->Release();
    }
    return 0;
}

#define  REGISTER_POINTERS_RELEASE_ASSERT(pPointer) \
do\
{\
    assert((pPointer)->m_pDevice == NULL);\
    assert((pPointer)->m_DeviceState == POINTER_STATE_FREE);\
    assert((pPointer)->m_pDeviceContext == NULL);\
    assert((pPointer)->m_DeviceContextState == POINTER_STATE_FREE);\
    assert((pPointer)->m_pSwapChain == NULL);\
    assert((pPointer)->m_SwapChainState == POINTER_STATE_FREE);\
}while(0)

static ULONG UnRegisterDevice(ID3D11Device* pDevice)
{
    int wait;
    int releaseidx=-1;
    unsigned int i;
    RegisterD11Pointers_t *pPointer=NULL,*pCurPointer=NULL;
    BOOL bret;
    ULONG ul;

    DEBUG_INFO("Unregister Device 0x%p\n",pDevice);
    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
    do
    {
        wait = 0;
        releaseidx = -1;
        EnterCriticalSection(&st_PointerLock);
        for(i=0; i<st_D11PointersVec.size() ; i++)
        {
            pCurPointer = st_D11PointersVec[i];
#if 0
            DEBUG_INFO("[%d]device 0x%p(%d) devicecontext 0x%p(%d) swapchain 0x%p(%d)\n",i,
                       pCurPointer->m_pDevice,pCurPointer->m_DeviceState,
                       pCurPointer->m_pDeviceContext ,pCurPointer->m_DeviceContextState,
                       pCurPointer->m_pSwapChain,pCurPointer->m_SwapChainState);
#endif
            if(pCurPointer->m_pDevice == pDevice)
            {
                assert(pCurPointer->m_DeviceState == POINTER_STATE_HOLD);
                if(pCurPointer->m_DeviceHoldCount > 1)
                {
                    /*because only we have gain the pointer owner ,so we should give this for the to avoid the grab state*/

                    wait = 1;
                    break;
                }
                else
                {
                    /*this is because when we enter this function ,we have hold the device count*/
                    assert(pCurPointer->m_DeviceHoldCount == 1);
                    releaseidx = i;
                    /*now we should set the state free ,and give it null pointer this 1 is from the DeviceHold function*/
                    pCurPointer->m_pDevice = NULL;
                    pCurPointer->m_DeviceState = POINTER_STATE_FREE;
                    pCurPointer->m_DeviceHoldCount = 0;
                    if(pCurPointer->m_pSwapChain == NULL && pCurPointer->m_pDeviceContext == NULL)
                    {
                        /*we release this register context*/
                        pPointer = pCurPointer;
                    }
                    break;
                }
            }
        }

        if(pPointer)
        {
            assert(releaseidx > 0);
            st_D11PointersVec.erase(st_D11PointersVec.begin() + releaseidx);
        }
        LeaveCriticalSection(&st_PointerLock);

        if(wait)
        {
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);

    if(pPointer)
    {
        DEBUG_INFO("Delete pointer [0x%p] [%d]\n",pPointer,releaseidx);
        REGISTER_POINTERS_RELEASE_ASSERT(pPointer);
        delete pPointer;
    }
    pPointer = NULL;

    ul = 1;
    if(releaseidx >= 0)
    {
        DEBUG_INFO("release\n");
        ul = pDevice->Release();
    }

    DEBUG_INFO("unregister 0x%p ul(%ld)\n",pDevice,ul);
    return ul;
}


static ULONG UnRegisterSwapChain(IDXGISwapChain *pSwapChain)
{
    int wait;
    int releaseidx=-1;
    unsigned int i;
    RegisterD11Pointers_t *pPointer=NULL,*pCurPointer=NULL;
    BOOL bret;
    ULONG ul;
    DEBUG_INFO("Unregister swapchain 0x%p\n",pSwapChain);
    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
    do
    {
        wait = 0;
        releaseidx = -1;
        EnterCriticalSection(&st_PointerLock);
        for(i=0; i<st_D11PointersVec.size() ; i++)
        {
            pCurPointer = st_D11PointersVec[i];
#if 0
            DEBUG_INFO("[%d]device 0x%p(%d) devicecontext 0x%p(%d) swapchain 0x%p(%d)\n",i,
                       pCurPointer->m_pDevice,pCurPointer->m_DeviceState,
                       pCurPointer->m_pDeviceContext ,pCurPointer->m_DeviceContextState,
                       pCurPointer->m_pSwapChain,pCurPointer->m_SwapChainState);
#endif
            if(pCurPointer->m_pSwapChain == pSwapChain)
            {
                assert(pCurPointer->m_SwapChainState == POINTER_STATE_HOLD);
                if(pCurPointer->m_SwapChainHoldCount > 1)
                {
                    wait = 1;
                    break;
                }
                else
                {
                    /*we have hold count for swap chain*/
                    assert(pCurPointer->m_SwapChainHoldCount == 1);
                    releaseidx = i;
                    /*set it for free*/
                    pCurPointer->m_pSwapChain = NULL;
                    pCurPointer->m_SwapChainState = POINTER_STATE_FREE;
                    pCurPointer->m_SwapChainHoldCount = 0;
                    if(pCurPointer->m_pDevice == NULL && pCurPointer->m_pDeviceContext == NULL)
                    {
                        /*we release this register context*/
                        pPointer = pCurPointer;
                    }
                    break;
                }
            }
        }

        if(pPointer)
        {
            assert(releaseidx > 0);
            assert(wait == 0);
            st_D11PointersVec.erase(st_D11PointersVec.begin() + releaseidx);
        }
        LeaveCriticalSection(&st_PointerLock);

        if(wait)
        {
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);

    if(pPointer)
    {
        DEBUG_INFO("Delete pointer [0x%p] [%d]\n",pPointer,releaseidx);
        REGISTER_POINTERS_RELEASE_ASSERT(pPointer);
        delete pPointer;
    }
    pPointer = NULL;

    ul = 1;
    if(releaseidx >=0)
    {
        DEBUG_INFO("release\n");
        ul = pSwapChain->Release();
    }

    DEBUG_INFO("unregister swapchain 0x%p ul (%ld)\n",pSwapChain,ul);
    return ul;
}

static ULONG UnRegisterDeviceContext(ID3D11DeviceContext* pDeviceContext)
{
    int wait;
    int releaseidx=-1;
    unsigned int i;
    RegisterD11Pointers_t *pPointer=NULL,*pCurPointer=NULL;
    BOOL bret;
    ULONG ul;

    DEBUG_INFO("Unregister DeviceContext 0x%p\n",pDeviceContext);
    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
    do
    {
        wait = 0;
        releaseidx = -1;
        EnterCriticalSection(&st_PointerLock);
        for(i=0; i<st_D11PointersVec.size() ; i++)
        {
            pCurPointer = st_D11PointersVec[i];
#if  0
            DEBUG_INFO("[%d]device 0x%p(%d) devicecontext 0x%p(%d) swapchain 0x%p(%d)\n",i,
                       pCurPointer->m_pDevice,pCurPointer->m_DeviceState,
                       pCurPointer->m_pDeviceContext ,pCurPointer->m_DeviceContextState,
                       pCurPointer->m_pSwapChain,pCurPointer->m_SwapChainState);
#endif
            if(pCurPointer->m_pDeviceContext == pDeviceContext)
            {
                assert(pCurPointer->m_DeviceContextState == POINTER_STATE_HOLD);
                if(pCurPointer->m_DeviceContextHoldCount > 1)
                {
                    wait = 1;
                    break;
                }
                else
                {
                    /*we have hold the count for enter this function*/
                    assert(pCurPointer->m_DeviceContextHoldCount == 1);
                    releaseidx = i;
                    pCurPointer->m_pDeviceContext = NULL;
                    pCurPointer->m_DeviceContextState = POINTER_STATE_FREE;
                    if(pCurPointer->m_pDevice == NULL && pCurPointer->m_pSwapChain == NULL)
                    {
                        /*we release this register context*/
                        pPointer = pCurPointer;
                    }
                    break;
                }
            }
        }

        if(pPointer)
        {
            assert(releaseidx > 0);
            st_D11PointersVec.erase(st_D11PointersVec.begin() + releaseidx);
        }
        LeaveCriticalSection(&st_PointerLock);

        if(wait)
        {
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);
    DEBUG_INFO("releaseidx %d\n",releaseidx);

    if(pPointer)
    {
        DEBUG_INFO("Delete pointer [0x%p] [%d]\n",pPointer,releaseidx);
        REGISTER_POINTERS_RELEASE_ASSERT(pPointer);
        delete pPointer;
    }
    pPointer = NULL;

    ul = 1;
    if(releaseidx >= 0)
    {
        DEBUG_INFO("release\n");
        ul = pDeviceContext->Release();
    }

    DEBUG_INFO("unregister devicecontext 0x%p ul (%ld)\n",pDeviceContext,ul);
    return ul;
}

int GrabD11Context(unsigned int idx,IDXGISwapChain **ppSwapChain,ID3D11Device **ppDevice,ID3D11DeviceContext **ppDeviceContext,int timeoutms)
{
    int grabed = -1;
    int wait;
    RegisterD11Pointers_t* pCurPointer=NULL;
    BOOL bret;
    ULONG startticks,curticks;
    int printout=0;

    startticks = GetTickCount();

    do
    {
        wait = 0;
        grabed = -1;
        EnterCriticalSection(&st_PointerLock);
        if(st_D11PointersVec.size() > idx)
        {
            grabed = 0;
            pCurPointer = st_D11PointersVec[idx];
            if((pCurPointer->m_pDevice &&  pCurPointer->m_pDeviceContext &&
                    pCurPointer->m_pSwapChain))
            {
                if(pCurPointer->m_DeviceContextState != POINTER_STATE_FREE ||
                        pCurPointer->m_DeviceState != POINTER_STATE_FREE ||
                        pCurPointer->m_SwapChainState != POINTER_STATE_FREE)
                {
                    wait = 1;
                }
                else
                {
                    assert(pCurPointer->m_DeviceHoldCount == 0);
                    assert(pCurPointer->m_DeviceContextHoldCount == 0);
                    assert(pCurPointer->m_SwapChainHoldCount == 0);
                    *ppDevice = pCurPointer->m_pDevice;
                    *ppDeviceContext = pCurPointer->m_pDeviceContext;
                    *ppSwapChain = pCurPointer->m_pSwapChain;
                    pCurPointer->m_DeviceContextState = POINTER_STATE_GRAB;
                    pCurPointer->m_DeviceState = POINTER_STATE_GRAB;
                    pCurPointer->m_SwapChainState = POINTER_STATE_GRAB;
                    grabed = 1;
                }
            }
        }
        LeaveCriticalSection(&st_PointerLock);
        if(wait)
        {

            curticks = GetTickCount();
            if((curticks-startticks) >(unsigned int) timeoutms || (curticks < startticks && (int)curticks >= timeoutms))
            {
                wait = 0;
                assert(grabed == 0);
                DEBUG_INFO("Grap for [%d] timeout (%ld - %ld)\n",timeoutms);
                break;
            }

            if((curticks - startticks) > 1000 &&  printout == 0)
            {
                DEBUG_INFO("Grab More Time %ld %ld\n",curticks,startticks);
                __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
                printout = 1;
            }
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);

    if(printout && grabed > 0)
    {
        curticks = GetTickCount();
        DEBUG_INFO("Grab pointers (%ld - %ld) Device(0x%p) DeviceContext(0x%p) SwapChain(0x%p)\n",curticks,startticks,
                   *ppDevice ,*ppDeviceContext,*ppSwapChain);
    }

    return grabed;
}

static int ReleaseD11Context(IDXGISwapChain *pSwapChain,ID3D11Device *pDevice,ID3D11DeviceContext *pDeviceContext)
{
    int released=0;
    int releaseidx = -1;
    unsigned int i;
    RegisterD11Pointers_t *pCurPointer=NULL;

    EnterCriticalSection(&st_PointerLock);
    for(i=0; i<st_D11PointersVec.size(); i++)
    {
        pCurPointer = st_D11PointersVec[i];
        if(pCurPointer->m_pSwapChain == pSwapChain && pCurPointer->m_pDevice == pDevice &&
                pCurPointer->m_pDeviceContext == pDeviceContext)
        {
            if(pCurPointer->m_SwapChainState != POINTER_STATE_GRAB ||
                    pCurPointer->m_DeviceContextState != POINTER_STATE_GRAB ||
                    pCurPointer->m_DeviceState != POINTER_STATE_GRAB)
            {
                DEBUG_INFO("[device:0x%p(%d) context:0x%p(%d) swapchain:0x%p(%d)]\n",
                           pCurPointer->m_pDevice,pCurPointer->m_DeviceState,
                           pCurPointer->m_pDeviceContext,pCurPointer->m_DeviceContextState,
                           pCurPointer->m_pSwapChain,pCurPointer->m_SwapChainState);
            }

            assert(pCurPointer->m_DeviceHoldCount == 0);
            assert(pCurPointer->m_DeviceContextHoldCount == 0);
            assert(pCurPointer->m_SwapChainHoldCount == 0);
            pCurPointer->m_DeviceState = POINTER_STATE_FREE;
            pCurPointer->m_DeviceContextState = POINTER_STATE_FREE;
            pCurPointer->m_SwapChainState = POINTER_STATE_FREE;
            releaseidx = i;
            break;
        }
    }
    LeaveCriticalSection(&st_PointerLock);

    if(releaseidx >= 0)
    {
        released = 1;
    }
    else
    {
        DEBUG_INFO("Not Release device(0x%p) context(0x%p) swapchain(0x%p)\n",pDevice,pDeviceContext,pSwapChain);
    }
    return released ;
}

static int HoldDevice(const char* file,const char*func,int lineno,ID3D11Device *pDevice)
{
    int hold=0;
    int wait=0;
    RegisterD11Pointers_t *pCurPointer=NULL;
    unsigned int i;
    BOOL bret;
    ULONG startticks,curticks;
    int printout = 0;
    int multihold = 0;

    startticks = GetTickCount();


    do
    {
        wait = 0;
        EnterCriticalSection(&st_PointerLock);
        for(i=0; i<st_D11PointersVec.size(); i++)
        {
            pCurPointer = st_D11PointersVec[i];
            if(pCurPointer->m_pDevice == pDevice)
            {
                if(pCurPointer->m_DeviceState == POINTER_STATE_GRAB)
                {
                    wait = 1;
                }
                else
                {
                    hold = 1;
                    pCurPointer->m_DeviceHoldCount ++;
                    pCurPointer->m_DeviceState = POINTER_STATE_HOLD;
                    multihold = pCurPointer->m_DeviceHoldCount;
                }
                break;
            }
        }
        LeaveCriticalSection(&st_PointerLock);
        if(wait)
        {
            curticks = GetTickCount();
            if((curticks - startticks) > 1000 && printout == 0)
            {
                printout = 1;
                __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
                DEBUG_INFO("Try Get Device[0x%p] (%ld - %ld)\n",pDevice,curticks,startticks);
            }
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);

    if(hold == 0)
    {
        DEBUG_INFO("[%s:%s:%d] not hold device 0x%p\n",
                   file,func,lineno,pDevice);
    }

    if(printout)
    {
        curticks = GetTickCount();
        DEBUG_INFO("%sDevice[0x%p] (%ld - %ld)\n",hold ? "Hold": "Not Hold",pDevice,curticks,startticks);
    }
    if(multihold > 1)
    {
        DEBUG_INFO("Device[0x%p] holdcount(%d)\n",pDevice,multihold);
    }

    return hold;
}

static int UnholdDevice(const char* file,const char*func,int lineno,ID3D11Device *pDevice)
{
    int unhold=0;
    RegisterD11Pointers_t *pCurPointer=NULL;
    unsigned int i;

    EnterCriticalSection(&st_PointerLock);
    for(i=0; i<st_D11PointersVec.size(); i++)
    {
        pCurPointer = st_D11PointersVec[i];
        if(pCurPointer->m_pDevice == pDevice)
        {
            assert(pCurPointer->m_DeviceState == POINTER_STATE_HOLD);
            {
                unhold = 1;
                pCurPointer->m_DeviceHoldCount --;
                if(pCurPointer->m_DeviceHoldCount == 0)
                {
                    pCurPointer->m_DeviceState = POINTER_STATE_FREE;
                }
            }
            break;
        }
    }
    LeaveCriticalSection(&st_PointerLock);

    if(unhold == 0)
    {
        DEBUG_INFO("[%s:%s:%d] not unhold device 0x%p\n",
                   file,func,lineno,pDevice);
    }

    return unhold;
}


static int HoldDeviceContext(const char* file,const char*func,int lineno,ID3D11DeviceContext *pDeviceContext)
{
    int hold=0;
    int wait=0;
    RegisterD11Pointers_t *pCurPointer=NULL;
    unsigned int i;
    BOOL bret;
    ULONG curticks,startticks;
    int printout = 0;
    int multihold=0;

    startticks = GetTickCount();

    do
    {
        wait = 0;
        EnterCriticalSection(&st_PointerLock);
        for(i=0; i<st_D11PointersVec.size(); i++)
        {
            pCurPointer = st_D11PointersVec[i];
            if(pCurPointer->m_pDeviceContext == pDeviceContext)
            {
                if(pCurPointer->m_DeviceContextState == POINTER_STATE_GRAB)
                {
                    wait = 1;
                }
                else
                {
                    hold = 1;
                    pCurPointer->m_DeviceContextHoldCount ++;
                    pCurPointer->m_DeviceContextState = POINTER_STATE_HOLD;
                    multihold = pCurPointer->m_DeviceContextHoldCount;
                }
                break;
            }
        }
        LeaveCriticalSection(&st_PointerLock);
        if(wait)
        {
            curticks = GetTickCount();
            if((curticks - startticks) > 1000 && printout == 0)
            {
                printout = 1;
                __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
                DEBUG_INFO("Try Hold DeviceContext [0x%p] (%ld - %ld)\n",
                           pDeviceContext,curticks,startticks);
            }
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);

    if(hold == 0)
    {
        DEBUG_INFO("[%s:%s:%d] not hold devicecontext 0x%p\n",
                   file,func,lineno,pDeviceContext);
    }

    if(printout)
    {
        curticks = GetTickCount();
        DEBUG_INFO("%s[0x%p] (%ld - %ld)\n",hold ? "Hold" : "Not Hold",
                   pDeviceContext,curticks,startticks);
    }

    if(multihold > 1)
    {
        DEBUG_INFO("DeviceContext[0x%p] holdcount(%d)\n",pDeviceContext,multihold);
    }

    return hold;
}

static int UnholdDeviceContext(const char* file,const char*func,int lineno,ID3D11DeviceContext *pDeviceContext)
{
    int unhold=0;
    RegisterD11Pointers_t *pCurPointer=NULL;
    unsigned int i;

    EnterCriticalSection(&st_PointerLock);
    for(i=0; i<st_D11PointersVec.size(); i++)
    {
        pCurPointer = st_D11PointersVec[i];
        if(pCurPointer->m_pDeviceContext == pDeviceContext)
        {
            assert(pCurPointer->m_DeviceContextState == POINTER_STATE_HOLD);
            {
                unhold = 1;
                pCurPointer->m_DeviceContextHoldCount --;
                if(pCurPointer->m_DeviceContextHoldCount == 0)
                {
                    pCurPointer->m_DeviceContextState = POINTER_STATE_FREE;
                }
            }
            break;
        }
    }
    LeaveCriticalSection(&st_PointerLock);

    if(unhold == 0)
    {
        DEBUG_INFO("[%s:%s:%d] not unhold devicecontext 0x%p\n",
                   file,func,lineno,pDeviceContext);
    }

    return unhold;
}


static int HoldSwapChain(const char* file,const char*func,int lineno,IDXGISwapChain *pSwapChain)
{
    int hold=0;
    int wait=0;
    RegisterD11Pointers_t *pCurPointer=NULL;
    unsigned int i;
    BOOL bret;
    ULONG startticks,curticks;
    int printout=0;
    int multihold=0;

    startticks = GetTickCount();


    do
    {
        wait = 0;
        EnterCriticalSection(&st_PointerLock);
        for(i=0; i<st_D11PointersVec.size(); i++)
        {
            pCurPointer = st_D11PointersVec[i];
            if(pCurPointer->m_pSwapChain == pSwapChain)
            {
                if(pCurPointer->m_SwapChainState == POINTER_STATE_GRAB)
                {
                    wait = 1;
                }
                else
                {
                    hold = 1;
                    pCurPointer->m_SwapChainHoldCount ++;
                    pCurPointer->m_SwapChainState = POINTER_STATE_HOLD;
                    multihold = pCurPointer->m_SwapChainHoldCount;
                }
                break;
            }
        }
        LeaveCriticalSection(&st_PointerLock);
        if(wait)
        {
            curticks = GetTickCount();
            if((curticks - startticks) > 1000 && printout == 0)
            {
                printout = 1;
                __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
                DEBUG_INFO("Try Hold SwapChain[0x%p] (%ld - %ld)\n",
                           pSwapChain,curticks,startticks);
            }
            bret = SwitchToThread();
            if(!bret)
            {
                /*sleep for a while*/
                Sleep(10);
            }
        }
    }
    while(wait);

    if(hold == 0)
    {
        DEBUG_INFO("[%s:%s:%d] not hold swapchain 0x%p\n",
                   file,func,lineno,pSwapChain);
    }

    if(printout)
    {
        curticks = GetTickCount();
        DEBUG_INFO("%s[0x%p] (%ld-%ld)\n",
                   hold ? "Hold" : "Not Hold",pSwapChain,curticks,startticks);
    }

    if(multihold > 1)
    {
        DEBUG_INFO("SwapChain[0x%p] holdcount(%d)\n",pSwapChain,multihold);
    }

    return hold;
}

static int UnholdSwapChain(const char* file,const char*func,int lineno,IDXGISwapChain *pSwapChain)
{
    int unhold=0;
    RegisterD11Pointers_t *pCurPointer=NULL;
    unsigned int i;

    EnterCriticalSection(&st_PointerLock);
    for(i=0; i<st_D11PointersVec.size(); i++)
    {
        pCurPointer = st_D11PointersVec[i];
        if(pCurPointer->m_pSwapChain== pSwapChain)
        {
            assert(pCurPointer->m_SwapChainState == POINTER_STATE_HOLD);
            {
                unhold = 1;
                pCurPointer->m_SwapChainHoldCount --;
                if(pCurPointer->m_SwapChainHoldCount == 0)
                {
                    pCurPointer->m_SwapChainState = POINTER_STATE_FREE;
                }
            }
            break;
        }
    }
    LeaveCriticalSection(&st_PointerLock);

    if(unhold == 0)
    {
        DEBUG_INFO("[%s:%s:%d] not unhold swapchain 0x%p\n",
                   file,func,lineno,pSwapChain);
    }
    return unhold;
}



#define  SWAP_CHAIN_IN()  do{HoldSwapChain(__FILE__,__FUNCTION__,__LINE__,m_ptr);} while(0)

#define  SWAP_CHAIN_OUT()  do{UnholdSwapChain(__FILE__,__FUNCTION__,__LINE__,m_ptr);} while(0)


class CDXGISwapChainHook : public IDXGISwapChain
{
private:
    IDXGISwapChain* m_ptr;
public:
    CDXGISwapChainHook(IDXGISwapChain* pPtr) : m_ptr(pPtr) {};

public:
    COM_METHOD(HRESULT, QueryInterface)(THIS_ REFIID riid, void** ppvObj)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->QueryInterface(riid,ppvObj);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(ULONG, AddRef)(THIS)
    {
        ULONG uret;
        SWAP_CHAIN_IN();
        uret = m_ptr->AddRef();
        SWAP_CHAIN_OUT();
        return uret;
    }

    COM_METHOD(ULONG, Release)(THIS)
    {
        ULONG uret;
        SWAP_CHAIN_IN();
        uret = m_ptr->Release();
        if(uret == 1)
        {
            uret = UnRegisterSwapChain(m_ptr);
        }
        SWAP_CHAIN_OUT();
        return uret;
    }

    COM_METHOD(HRESULT , SetPrivateData)(THIS_  REFGUID Name,UINT DataSize,const void *pData)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->SetPrivateData(Name,DataSize,pData);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,SetPrivateDataInterface)(THIS_ REFGUID Name,const IUnknown *pUnknown)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->SetPrivateDataInterface(Name,pUnknown);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,GetPrivateData)(THIS_  REFGUID Name,UINT *pDataSize,void *pData)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetPrivateData(Name,pDataSize,pData);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,GetParent)(THIS_ REFIID riid,void **ppParent)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetParent(riid,ppParent);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,GetDevice)(THIS_ REFIID riid,void **ppDevice)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetDevice(riid,ppDevice);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,Present)(THIS_ UINT SyncInterval,UINT Flags)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->Present(SyncInterval,Flags);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,GetBuffer)(THIS_ UINT Buffer,REFIID riid,void **ppSurface)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetBuffer(Buffer,riid,ppSurface);
        SWAP_CHAIN_OUT();
        return hr;
    }


    COM_METHOD(HRESULT,SetFullscreenState)(THIS_ BOOL Fullscreen,IDXGIOutput *pTarget)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->SetFullscreenState(Fullscreen,pTarget);
        SWAP_CHAIN_OUT();
        return hr;
    }


    COM_METHOD(HRESULT,GetFullscreenState)(THIS_ BOOL *pFullscreen,IDXGIOutput **ppTarget)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetFullscreenState(pFullscreen,ppTarget);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,GetDesc)(THIS_ DXGI_SWAP_CHAIN_DESC *pDesc)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetDesc(pDesc);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,ResizeBuffers)(THIS_ UINT BufferCount,UINT Width,UINT Height,DXGI_FORMAT NewFormat,UINT SwapChainFlags)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->ResizeBuffers(BufferCount,Width,Height,NewFormat,SwapChainFlags);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,ResizeTarget)(THIS_ const DXGI_MODE_DESC *pNewTargetParameters)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->ResizeTarget(pNewTargetParameters);
        SWAP_CHAIN_OUT();
        return hr;
    }


    COM_METHOD(HRESULT,GetContainingOutput)(THIS_ IDXGIOutput **ppOutput)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetContainingOutput(ppOutput);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,GetFrameStatistics)(THIS_ DXGI_FRAME_STATISTICS *pStats)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetFrameStatistics(pStats);
        SWAP_CHAIN_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,GetLastPresentCount)(THIS_ UINT *pLastPresentCount)
    {
        HRESULT hr;
        SWAP_CHAIN_IN();
        hr = m_ptr->GetLastPresentCount(pLastPresentCount);
        SWAP_CHAIN_OUT();
        return hr;
    }

};




#define  DEVICE_CONTEXT_IN() do{HoldDeviceContext(__FILE__,__FUNCTION__,__LINE__,m_ptr);} while(0)

#define  DEVICE_CONTEXT_OUT() do{UnholdDeviceContext(__FILE__,__FUNCTION__,__LINE__,m_ptr);} while(0)



class CD3D11DeviceContextHook : public ID3D11DeviceContext
{
private:
    ID3D11DeviceContext *m_ptr;
public:
    CD3D11DeviceContextHook(ID3D11DeviceContext *pPtr):m_ptr(pPtr) {};

public:
    COM_METHOD(HRESULT, QueryInterface)(THIS_ REFIID riid, void** ppvObj)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr =  m_ptr->QueryInterface(riid, ppvObj);
        DEVICE_CONTEXT_OUT();
        return hr;
    }
    COM_METHOD(ULONG, AddRef)(THIS)
    {
        ULONG ret;
        DEVICE_CONTEXT_IN();
        ret=  m_ptr->AddRef();
        DEVICE_CONTEXT_OUT();
        return ret;
    }
    COM_METHOD(ULONG, Release)(THIS)
    {
        ULONG uret,realret;
        int ret;

        DEVICE_CONTEXT_IN();
        uret = m_ptr->Release();
        realret = uret;
        DEBUG_INFO("DeviceContext 0x%p release %ld\n",m_ptr,realret);
        /*it means that is the just one ,we should return for the job*/
        if(uret == 1)
        {
            ret = UnRegisterDeviceContext(m_ptr);
            /*if 1 it means not release one*/
            realret = ret;
        }

        DEVICE_CONTEXT_OUT();
        return realret;
    }

    COM_METHOD(void,GetDevice)(THIS_ ID3D11Device **ppDevice)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GetDevice(ppDevice);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(HRESULT,GetPrivateData)(THIS_ REFGUID guid,UINT *pDataSize,void *pData)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr =  m_ptr->GetPrivateData(guid,pDataSize,pData);
        DEVICE_CONTEXT_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,SetPrivateData)(THIS_ REFGUID guid,UINT DataSize,const void *pData)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr =  m_ptr->SetPrivateData(guid,DataSize,pData);
        DEVICE_CONTEXT_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,SetPrivateDataInterface)(THIS_ REFGUID guid,const IUnknown *pData)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr =  m_ptr->SetPrivateDataInterface(guid,pData);
        DEVICE_CONTEXT_OUT();
        return hr;
    }

    COM_METHOD(void,VSSetConstantBuffers)(THIS_ UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSSetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,PSSetShaderResources)(THIS_ UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView *const *ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSSetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,PSSetShader)(THIS_ ID3D11PixelShader *pPixelShader,ID3D11ClassInstance *const *ppClassInstances,UINT NumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSSetShader(pPixelShader,ppClassInstances,NumClassInstances);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,PSSetSamplers)(THIS_ UINT StartSlot,UINT NumSamplers,ID3D11SamplerState *const *ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSSetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,VSSetShader)(THIS_ ID3D11VertexShader *pVertexShader,ID3D11ClassInstance *const *ppClassInstances,	UINT NumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSSetShader(pVertexShader,ppClassInstances,NumClassInstances);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,DrawIndexed)(THIS_ UINT IndexCount,UINT StartIndexLocation,INT BaseVertexLocation)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DrawIndexed(IndexCount,StartIndexLocation,BaseVertexLocation);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,Draw)(THIS_ UINT VertexCount,UINT StartVertexLocation)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->Draw(VertexCount,StartVertexLocation);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(HRESULT,Map)(THIS_ ID3D11Resource *pResource,UINT Subresource,D3D11_MAP MapType,UINT MapFlags,D3D11_MAPPED_SUBRESOURCE *pMappedResource)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr =  m_ptr->Map(pResource,Subresource,MapType,MapFlags,pMappedResource);
        DEVICE_CONTEXT_OUT();
        return hr;
    }

    COM_METHOD(void ,Unmap)(THIS_ ID3D11Resource *pResource,UINT Subresource)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->Unmap(pResource,Subresource);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,PSSetConstantBuffers)(THIS_ UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSSetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,IASetInputLayout)(THIS_ ID3D11InputLayout *pInputLayout)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IASetInputLayout(pInputLayout);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,IASetVertexBuffers)(THIS_ UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppVertexBuffers,const UINT *pStrides,const UINT *pOffsets)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IASetVertexBuffers(StartSlot,NumBuffers,ppVertexBuffers,pStrides,pOffsets);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,IASetIndexBuffer)(THIS_ ID3D11Buffer *pIndexBuffer,DXGI_FORMAT Format,UINT Offset)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IASetIndexBuffer(pIndexBuffer,Format,Offset);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,DrawIndexedInstanced)(THIS_ UINT IndexCountPerInstance,UINT InstanceCount,UINT StartIndexLocation,INT BaseVertexLocation,UINT StartInstanceLocation)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DrawIndexedInstanced(IndexCountPerInstance,InstanceCount,StartIndexLocation,BaseVertexLocation,StartInstanceLocation);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,DrawInstanced)(THIS_ UINT VertexCountPerInstance,UINT InstanceCount,UINT StartVertexLocation,UINT StartInstanceLocation)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DrawInstanced(VertexCountPerInstance,InstanceCount,StartVertexLocation,StartInstanceLocation);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,GSSetConstantBuffers)(THIS_ UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSSetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,GSSetShader)(THIS_ ID3D11GeometryShader *pShader,ID3D11ClassInstance *const *ppClassInstances,	UINT NumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSSetShader(pShader,ppClassInstances,NumClassInstances);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,IASetPrimitiveTopology)(THIS_ D3D11_PRIMITIVE_TOPOLOGY Topology)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IASetPrimitiveTopology(Topology);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,VSSetShaderResources)(THIS_ UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView *const *ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSSetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,VSSetSamplers)(THIS_ UINT StartSlot,UINT NumSamplers,ID3D11SamplerState *const *ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSSetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,Begin)(THIS_ ID3D11Asynchronous *pAsync)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->Begin(pAsync);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,End)(THIS_ ID3D11Asynchronous *pAsync)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->End(pAsync);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(HRESULT,GetData)(THIS_ ID3D11Asynchronous *pAsync,void *pData,UINT DataSize,UINT GetDataFlags)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr =  m_ptr->GetData(pAsync,pData,DataSize,GetDataFlags);
        DEVICE_CONTEXT_OUT();
        return hr;
    }

    COM_METHOD(void,SetPredication)(THIS_ ID3D11Predicate *pPredicate,BOOL PredicateValue)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->SetPredication(pPredicate,PredicateValue);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,GSSetShaderResources)(THIS_ UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView *const *ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSSetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,GSSetSamplers)(THIS_ UINT StartSlot,UINT NumSamplers,ID3D11SamplerState *const *ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSSetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,OMSetRenderTargets)(THIS_ UINT NumViews,ID3D11RenderTargetView *const *ppRenderTargetViews,ID3D11DepthStencilView *pDepthStencilView)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMSetRenderTargets(NumViews,ppRenderTargetViews,pDepthStencilView);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,OMSetRenderTargetsAndUnorderedAccessViews)(THIS_ UINT NumRTVs,ID3D11RenderTargetView *const *ppRenderTargetViews,ID3D11DepthStencilView *pDepthStencilView,
            UINT UAVStartSlot,UINT NumUAVs,ID3D11UnorderedAccessView *const *ppUnorderedAccessViews,const UINT *pUAVInitialCounts)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs,ppRenderTargetViews,pDepthStencilView,UAVStartSlot,NumUAVs,ppUnorderedAccessViews,pUAVInitialCounts);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,OMSetBlendState)(THIS_ ID3D11BlendState *pBlendState,const FLOAT BlendFactor[ 4 ],UINT SampleMask)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMSetBlendState(pBlendState,BlendFactor,SampleMask);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,OMSetDepthStencilState)(THIS_ ID3D11DepthStencilState *pDepthStencilState,UINT StencilRef)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMSetDepthStencilState(pDepthStencilState,StencilRef);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,SOSetTargets)(THIS_ UINT NumBuffers,ID3D11Buffer *const *ppSOTargets,const UINT *pOffsets)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->SOSetTargets(NumBuffers,ppSOTargets,pOffsets);
        DEVICE_CONTEXT_OUT();
        return;
    }

    COM_METHOD(void,DrawAuto)(THIS)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DrawAuto();
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DrawIndexedInstancedIndirect)(THIS_ ID3D11Buffer *pBufferForArgs,UINT AlignedByteOffsetForArgs)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DrawIndexedInstancedIndirect(pBufferForArgs,AlignedByteOffsetForArgs);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void ,DrawInstancedIndirect)(THIS_ ID3D11Buffer *pBufferForArgs,UINT AlignedByteOffsetForArgs)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DrawInstancedIndirect(pBufferForArgs,AlignedByteOffsetForArgs);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,Dispatch)(THIS_  UINT ThreadGroupCountX,UINT ThreadGroupCountY,UINT ThreadGroupCountZ)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->Dispatch(ThreadGroupCountX,ThreadGroupCountY,ThreadGroupCountZ);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DispatchIndirect)(THIS_  ID3D11Buffer *pBufferForArgs,UINT AlignedByteOffsetForArgs)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DispatchIndirect(pBufferForArgs,AlignedByteOffsetForArgs);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,RSSetState)(THIS_  ID3D11RasterizerState *pRasterizerState)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->RSSetState(pRasterizerState);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,RSSetViewports)(THIS_  UINT NumViewports,const D3D11_VIEWPORT *pViewports)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->RSSetViewports(NumViewports,pViewports);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,RSSetScissorRects)(THIS_ UINT NumRects,const D3D11_RECT *pRects)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->RSSetScissorRects(NumRects,pRects);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CopySubresourceRegion)(THIS_ ID3D11Resource *pDstResource,UINT DstSubresource,
                                           UINT DstX,UINT DstY,UINT DstZ,ID3D11Resource *pSrcResource,UINT SrcSubresource,const D3D11_BOX *pSrcBox)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CopySubresourceRegion(pDstResource,DstSubresource,DstX,DstY,DstZ,pSrcResource,SrcSubresource,pSrcBox);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CopyResource)(THIS_ ID3D11Resource *pDstResource,ID3D11Resource *pSrcResource)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CopyResource(pDstResource,pSrcResource);
        DEVICE_CONTEXT_OUT();
        return ;
    }


    COM_METHOD(void,UpdateSubresource)(THIS_ ID3D11Resource *pDstResource,UINT DstSubresource,
                                       const D3D11_BOX *pDstBox,const void *pSrcData,UINT SrcRowPitch,UINT SrcDepthPitch)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->UpdateSubresource(pDstResource,DstSubresource,pDstBox,pSrcData,SrcRowPitch,SrcDepthPitch);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CopyStructureCount)(THIS_ ID3D11Buffer *pDstBuffer,UINT DstAlignedByteOffset,ID3D11UnorderedAccessView *pSrcView)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CopyStructureCount(pDstBuffer,DstAlignedByteOffset,pSrcView);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,ClearRenderTargetView)(THIS_ ID3D11RenderTargetView *pRenderTargetView,const FLOAT ColorRGBA[ 4 ])
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ClearRenderTargetView(pRenderTargetView,ColorRGBA);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,ClearUnorderedAccessViewUint)(THIS_  ID3D11UnorderedAccessView *pUnorderedAccessView,const UINT Values[ 4 ])
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ClearUnorderedAccessViewUint(pUnorderedAccessView,Values);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,ClearUnorderedAccessViewFloat)(THIS_  ID3D11UnorderedAccessView *pUnorderedAccessView,const FLOAT Values[ 4 ])
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ClearUnorderedAccessViewFloat(pUnorderedAccessView,Values);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,ClearDepthStencilView)(THIS_  ID3D11DepthStencilView *pDepthStencilView,UINT ClearFlags,
                                           FLOAT Depth,UINT8 Stencil)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ClearDepthStencilView(pDepthStencilView,ClearFlags,Depth,Stencil);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,GenerateMips)(THIS_  ID3D11ShaderResourceView *pShaderResourceView)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GenerateMips(pShaderResourceView);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,SetResourceMinLOD)(THIS_  ID3D11Resource *pResource,    FLOAT MinLOD)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->SetResourceMinLOD(pResource,MinLOD);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(FLOAT,GetResourceMinLOD)(THIS_ ID3D11Resource *pResource)
    {
        float f;
        DEVICE_CONTEXT_IN();
        f = m_ptr->GetResourceMinLOD(pResource);
        DEVICE_CONTEXT_OUT();
        return f;
    }

    COM_METHOD(void,ResolveSubresource)(THIS_ ID3D11Resource *pDstResource,UINT DstSubresource,
                                        ID3D11Resource *pSrcResource,UINT SrcSubresource,DXGI_FORMAT Format)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ResolveSubresource(pDstResource,DstSubresource,pSrcResource,SrcSubresource,Format);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,ExecuteCommandList)(THIS_  ID3D11CommandList *pCommandList,    BOOL RestoreContextState)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ExecuteCommandList(pCommandList, RestoreContextState);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSSetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,
                                          ID3D11ShaderResourceView *const *ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSSetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSSetShader)(THIS_  ID3D11HullShader *pHullShader,ID3D11ClassInstance *const *ppClassInstances,    UINT NumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSSetShader(pHullShader,ppClassInstances,NumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSSetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState *const *ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSSetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSSetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSSetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSSetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView *const *ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSSetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSSetShader)(THIS_  ID3D11DomainShader *pDomainShader,ID3D11ClassInstance *const *ppClassInstances,UINT NumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSSetShader(pDomainShader,ppClassInstances,NumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSSetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState *const *ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSSetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSSetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSSetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSSetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView *const *ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSSetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSSetUnorderedAccessViews)(THIS_  UINT StartSlot,UINT NumUAVs,
            ID3D11UnorderedAccessView *const *ppUnorderedAccessViews,const UINT *pUAVInitialCounts)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSSetUnorderedAccessViews(StartSlot,NumUAVs,ppUnorderedAccessViews,pUAVInitialCounts);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSSetShader)(THIS_  ID3D11ComputeShader *pComputeShader,ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSSetShader(pComputeShader,ppClassInstances,NumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSSetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState *const *ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSSetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSSetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer *const *ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSSetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,VSGetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSGetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,PSGetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView **ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSGetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,PSGetShader)(THIS_  ID3D11PixelShader **ppPixelShader,ID3D11ClassInstance **ppClassInstances,UINT *pNumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSGetShader(ppPixelShader,ppClassInstances,pNumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }
    COM_METHOD(void,PSGetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState **ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSGetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,VSGetShader)(THIS_  ID3D11VertexShader **ppVertexShader,ID3D11ClassInstance **ppClassInstances,UINT *pNumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSGetShader(ppVertexShader,ppClassInstances,pNumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,PSGetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->PSGetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,IAGetInputLayout)(THIS_  ID3D11InputLayout **ppInputLayout)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IAGetInputLayout(ppInputLayout);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,IAGetVertexBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppVertexBuffers,
                                        UINT *pStrides,UINT *pOffsets)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IAGetVertexBuffers(StartSlot,NumBuffers,ppVertexBuffers,pStrides,pOffsets);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,IAGetIndexBuffer)(THIS_  ID3D11Buffer **pIndexBuffer,DXGI_FORMAT *Format,UINT *Offset)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IAGetIndexBuffer(pIndexBuffer,Format,Offset);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,GSGetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSGetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,GSGetShader)(THIS_  ID3D11GeometryShader **ppGeometryShader,ID3D11ClassInstance **ppClassInstances,UINT *pNumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSGetShader(ppGeometryShader,ppClassInstances,pNumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,IAGetPrimitiveTopology)(THIS_  D3D11_PRIMITIVE_TOPOLOGY *pTopology)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->IAGetPrimitiveTopology(pTopology);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,VSGetShaderResources)(THIS_ UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView **ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSGetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,VSGetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState **ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->VSGetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,GetPredication)(THIS_  ID3D11Predicate **ppPredicate,BOOL *pPredicateValue)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GetPredication(ppPredicate,pPredicateValue);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,GSGetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView **ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSGetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,GSGetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState **ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->GSGetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,OMGetRenderTargets)(THIS_  UINT NumViews,ID3D11RenderTargetView **ppRenderTargetViews,ID3D11DepthStencilView **ppDepthStencilView)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMGetRenderTargets(NumViews,ppRenderTargetViews,ppDepthStencilView);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,OMGetRenderTargetsAndUnorderedAccessViews)(THIS_  UINT NumRTVs,ID3D11RenderTargetView **ppRenderTargetViews,
            ID3D11DepthStencilView **ppDepthStencilView,UINT UAVStartSlot,UINT NumUAVs,ID3D11UnorderedAccessView **ppUnorderedAccessViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs,ppRenderTargetViews,ppDepthStencilView,UAVStartSlot,NumUAVs,ppUnorderedAccessViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,OMGetBlendState)(THIS_  ID3D11BlendState **ppBlendState,FLOAT BlendFactor[ 4 ],UINT *pSampleMask)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMGetBlendState(ppBlendState,BlendFactor,pSampleMask);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,OMGetDepthStencilState)(THIS_  ID3D11DepthStencilState **ppDepthStencilState,UINT *pStencilRef)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->OMGetDepthStencilState(ppDepthStencilState,pStencilRef);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,SOGetTargets)(THIS_  UINT NumBuffers,ID3D11Buffer **ppSOTargets)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->SOGetTargets(NumBuffers,ppSOTargets);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,RSGetState)(THIS_  ID3D11RasterizerState **ppRasterizerState)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->RSGetState(ppRasterizerState);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,RSGetViewports)(THIS_  UINT *pNumViewports,D3D11_VIEWPORT *pViewports)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->RSGetViewports(pNumViewports,pViewports);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,RSGetScissorRects)(THIS_ UINT *pNumRects,D3D11_RECT *pRects)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->RSGetScissorRects(pNumRects,pRects);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSGetShaderResources)(THIS_ UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView **ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSGetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSGetShader)(THIS_  ID3D11HullShader **ppHullShader,ID3D11ClassInstance **ppClassInstances,UINT *pNumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSGetShader(ppHullShader,ppClassInstances,pNumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSGetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState **ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSGetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,HSGetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->HSGetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSGetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView **ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSGetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSGetShader)(THIS_  ID3D11DomainShader **ppDomainShader,ID3D11ClassInstance **ppClassInstances,UINT *pNumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSGetShader(ppDomainShader,ppClassInstances,pNumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSGetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState **ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSGetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,DSGetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->DSGetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSGetShaderResources)(THIS_  UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView **ppShaderResourceViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSGetShaderResources(StartSlot,NumViews,ppShaderResourceViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSGetUnorderedAccessViews)(THIS_  UINT StartSlot,UINT NumUAVs,ID3D11UnorderedAccessView **ppUnorderedAccessViews)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSGetUnorderedAccessViews(StartSlot,NumUAVs,ppUnorderedAccessViews);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSGetShader)(THIS_  ID3D11ComputeShader **ppComputeShader,ID3D11ClassInstance **ppClassInstances,UINT *pNumClassInstances)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSGetShader(ppComputeShader,ppClassInstances,pNumClassInstances);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSGetSamplers)(THIS_  UINT StartSlot,UINT NumSamplers,ID3D11SamplerState **ppSamplers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSGetSamplers(StartSlot,NumSamplers,ppSamplers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,CSGetConstantBuffers)(THIS_  UINT StartSlot,UINT NumBuffers,ID3D11Buffer **ppConstantBuffers)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->CSGetConstantBuffers(StartSlot,NumBuffers,ppConstantBuffers);
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,ClearState)(THIS)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->ClearState();
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(void,Flush)(THIS)
    {
        DEVICE_CONTEXT_IN();
        m_ptr->Flush();
        DEVICE_CONTEXT_OUT();
        return ;
    }

    COM_METHOD(D3D11_DEVICE_CONTEXT_TYPE,GetType)(THIS)
    {
        D3D11_DEVICE_CONTEXT_TYPE type;
        DEVICE_CONTEXT_IN();
        type = m_ptr->GetType();
        DEVICE_CONTEXT_OUT();
        return type;
    }

    COM_METHOD(UINT,GetContextFlags)(THIS)
    {
        UINT ui;
        DEVICE_CONTEXT_IN();
        ui = m_ptr->GetContextFlags();
        DEVICE_CONTEXT_OUT();
        return ui;
    }

    COM_METHOD(HRESULT,FinishCommandList)(THIS_ 	BOOL RestoreDeferredContextState,ID3D11CommandList **ppCommandList)
    {
        HRESULT hr;
        DEVICE_CONTEXT_IN();
        hr = m_ptr->FinishCommandList(RestoreDeferredContextState,ppCommandList);
        DEVICE_CONTEXT_OUT();
        return hr;
    }
};




#define  D11_DEVICE_IN()  do{HoldDevice(__FILE__,__FUNCTION__,__LINE__,m_ptr);}while(0)
#define  D11_DEVICE_OUT()  do{UnholdDevice(__FILE__,__FUNCTION__,__LINE__,m_ptr);}while(0)


// {340B065A-2EBA-44BA-AD8D-92A6BA0819D3}
static const GUID IID_DeviceHookGuid=
{ 0x340b065a, 0x2eba, 0x44ba, { 0xad, 0x8d, 0x92, 0xa6, 0xba, 0x8, 0x19, 0xd3 } };


class CD3D11DeviceHook : public ID3D11Device
{
private:
    ID3D11Device *m_ptr;
public:
    CD3D11DeviceHook(ID3D11Device* pPtr) : m_ptr(pPtr) {};
public:
    COM_METHOD(HRESULT,QueryInterface)(THIS_  REFIID riid,void **ppvObject)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        if(riid == IID_DeviceHookGuid)
        {
            m_ptr->AddRef();
            *ppvObject = (void*)this;
            hr = S_OK;
        }
        else
        {
            hr = m_ptr->QueryInterface(riid,ppvObject);
        }
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(ULONG,AddRef)(THIS)
    {
        ULONG ul;
        D11_DEVICE_IN();
        ul = m_ptr->AddRef();
        D11_DEVICE_OUT();
        return ul;
    }

    COM_METHOD(ULONG,Release)(THIS)
    {
        ULONG ul;
        D11_DEVICE_IN();
        ul = m_ptr->Release();
        if(ul == 1)
        {
            ul = UnRegisterDevice(m_ptr);
        }
        D11_DEVICE_OUT();
        return ul;
    }

    COM_METHOD(HRESULT,CreateBuffer)(THIS_  const D3D11_BUFFER_DESC *pDesc,const D3D11_SUBRESOURCE_DATA *pInitialData,ID3D11Buffer **ppBuffer)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateBuffer(pDesc,pInitialData,ppBuffer);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateTexture1D)(THIS_  const D3D11_TEXTURE1D_DESC *pDesc,const D3D11_SUBRESOURCE_DATA *pInitialData,ID3D11Texture1D **ppTexture1D)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateTexture1D(pDesc,pInitialData,ppTexture1D);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateTexture2D)(THIS_  const D3D11_TEXTURE2D_DESC *pDesc,const D3D11_SUBRESOURCE_DATA *pInitialData,ID3D11Texture2D **ppTexture2D)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateTexture2D(pDesc,pInitialData,ppTexture2D);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateTexture3D)(THIS_  const D3D11_TEXTURE3D_DESC *pDesc,const D3D11_SUBRESOURCE_DATA *pInitialData,ID3D11Texture3D **ppTexture3D)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateTexture3D(pDesc,pInitialData,ppTexture3D);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateShaderResourceView)(THIS_  ID3D11Resource *pResource,const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,ID3D11ShaderResourceView **ppSRView)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateShaderResourceView(pResource,pDesc,ppSRView);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateUnorderedAccessView)(THIS_  ID3D11Resource *pResource,const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc,ID3D11UnorderedAccessView **ppUAView)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateUnorderedAccessView(pResource,pDesc,ppUAView);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateRenderTargetView)(THIS_  ID3D11Resource *pResource,const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,ID3D11RenderTargetView **ppRTView)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateRenderTargetView(pResource,pDesc,ppRTView);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateDepthStencilView)(THIS_  ID3D11Resource *pResource,const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,ID3D11DepthStencilView **ppDepthStencilView)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateDepthStencilView(pResource,pDesc,ppDepthStencilView);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateInputLayout)(THIS_  const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,UINT NumElements,
                                          const void *pShaderBytecodeWithInputSignature,SIZE_T BytecodeLength,ID3D11InputLayout **ppInputLayout)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateInputLayout(pInputElementDescs,NumElements,pShaderBytecodeWithInputSignature,BytecodeLength,ppInputLayout);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateVertexShader)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage *pClassLinkage,ID3D11VertexShader **ppVertexShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateVertexShader(pShaderBytecode,BytecodeLength,pClassLinkage,ppVertexShader);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,CreateGeometryShader)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage *pClassLinkage,
            ID3D11GeometryShader **ppGeometryShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateGeometryShader(pShaderBytecode,BytecodeLength,pClassLinkage,ppGeometryShader);
        D11_DEVICE_OUT();
        return hr;
    }


    COM_METHOD(HRESULT,CreateGeometryShaderWithStreamOutput)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,const D3D11_SO_DECLARATION_ENTRY *pSODeclaration,
            UINT NumEntries,const UINT *pBufferStrides,UINT NumStrides,UINT RasterizedStream,ID3D11ClassLinkage *pClassLinkage,ID3D11GeometryShader **ppGeometryShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateGeometryShaderWithStreamOutput(pShaderBytecode,BytecodeLength,pSODeclaration,NumEntries,pBufferStrides,NumStrides,RasterizedStream,pClassLinkage,ppGeometryShader);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT ,CreatePixelShader)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage *pClassLinkage,ID3D11PixelShader **ppPixelShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreatePixelShader(pShaderBytecode,BytecodeLength,pClassLinkage,ppPixelShader);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateHullShader)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage *pClassLinkage,ID3D11HullShader **ppHullShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateHullShader(pShaderBytecode,BytecodeLength,pClassLinkage,ppHullShader);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateDomainShader)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage *pClassLinkage,ID3D11DomainShader **ppDomainShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateDomainShader(pShaderBytecode,BytecodeLength,pClassLinkage,ppDomainShader);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateComputeShader)(THIS_  const void *pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage *pClassLinkage,ID3D11ComputeShader **ppComputeShader)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateComputeShader(pShaderBytecode,BytecodeLength,pClassLinkage,ppComputeShader);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateClassLinkage)(THIS_  ID3D11ClassLinkage **ppLinkage)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateClassLinkage(ppLinkage);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateBlendState)(THIS_  const D3D11_BLEND_DESC *pBlendStateDesc,ID3D11BlendState **ppBlendState)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateBlendState(pBlendStateDesc,ppBlendState);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateDepthStencilState)(THIS_ const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc,ID3D11DepthStencilState **ppDepthStencilState)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateDepthStencilState(pDepthStencilDesc,ppDepthStencilState);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateRasterizerState)(THIS_  const D3D11_RASTERIZER_DESC *pRasterizerDesc,ID3D11RasterizerState **ppRasterizerState)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateRasterizerState(pRasterizerDesc,ppRasterizerState);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateSamplerState)(THIS_  const D3D11_SAMPLER_DESC *pSamplerDesc,ID3D11SamplerState **ppSamplerState)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateSamplerState(pSamplerDesc,ppSamplerState);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateQuery)(THIS_  const D3D11_QUERY_DESC *pQueryDesc,ID3D11Query **ppQuery)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateQuery(pQueryDesc,ppQuery);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreatePredicate)(THIS_  const D3D11_QUERY_DESC *pPredicateDesc,ID3D11Predicate **ppPredicate)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreatePredicate(pPredicateDesc,ppPredicate);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateCounter)(THIS_  const D3D11_COUNTER_DESC *pCounterDesc,ID3D11Counter **ppCounter)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateCounter(pCounterDesc,ppCounter);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateDeferredContext)(THIS_  	UINT ContextFlags,ID3D11DeviceContext **ppDeferredContext)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CreateDeferredContext(ContextFlags,ppDeferredContext);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,OpenSharedResource)(THIS_  HANDLE hResource,REFIID ReturnedInterface,void **ppResource)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->OpenSharedResource(hResource,ReturnedInterface,ppResource);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CheckFormatSupport)(THIS_ DXGI_FORMAT Format,UINT *pFormatSupport)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CheckFormatSupport(Format,pFormatSupport);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CheckMultisampleQualityLevels)(THIS_  DXGI_FORMAT Format,UINT SampleCount,UINT *pNumQualityLevels)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CheckMultisampleQualityLevels(Format,SampleCount,pNumQualityLevels);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(void,CheckCounterInfo)(THIS_ D3D11_COUNTER_INFO *pCounterInfo)
    {
        D11_DEVICE_IN();
        m_ptr->CheckCounterInfo(pCounterInfo);
        D11_DEVICE_OUT();
        return;
    }

    COM_METHOD(HRESULT,CheckCounter)(THIS_ const D3D11_COUNTER_DESC *pDesc,D3D11_COUNTER_TYPE *pType,UINT *pActiveCounters,LPSTR szName,
                                     UINT *pNameLength,LPSTR szUnits,UINT *pUnitsLength,LPSTR szDescription,UINT *pDescriptionLength)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CheckCounter(pDesc,pType,pActiveCounters,szName,pNameLength,szUnits,pUnitsLength,szDescription,pDescriptionLength);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CheckFeatureSupport)(THIS_  D3D11_FEATURE Feature,void *pFeatureSupportData,UINT FeatureSupportDataSize)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->CheckFeatureSupport(Feature,pFeatureSupportData,FeatureSupportDataSize);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,GetPrivateData)(THIS_  REFGUID guid,UINT *pDataSize,void *pData)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->GetPrivateData(guid,pDataSize,pData);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,SetPrivateData)(THIS_  REFGUID guid,UINT DataSize,const void *pData)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->SetPrivateData(guid,DataSize,pData);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,SetPrivateDataInterface)(THIS_  REFGUID guid,const IUnknown *pData)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->SetPrivateDataInterface(guid,pData);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(D3D_FEATURE_LEVEL,GetFeatureLevel)(THIS)
    {
        D3D_FEATURE_LEVEL ulevel;
        D11_DEVICE_IN();
        ulevel = m_ptr->GetFeatureLevel();
        D11_DEVICE_OUT();
        return ulevel;
    }

    COM_METHOD(UINT,GetCreationFlags)(THIS)
    {
        UINT ui;
        D11_DEVICE_IN();
        ui = m_ptr->GetCreationFlags();
        D11_DEVICE_OUT();
        return ui;
    }

    COM_METHOD(HRESULT,GetDeviceRemovedReason)(THIS)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->GetDeviceRemovedReason();
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(void,GetImmediateContext)(THIS_  ID3D11DeviceContext **ppImmediateContext)
    {
        D11_DEVICE_IN();
        m_ptr->GetImmediateContext(ppImmediateContext);
        D11_DEVICE_OUT();
        return ;
    }

    COM_METHOD(HRESULT,SetExceptionMode)(THIS_      UINT RaiseFlags)
    {
        HRESULT hr;
        D11_DEVICE_IN();
        hr = m_ptr->SetExceptionMode(RaiseFlags);
        D11_DEVICE_OUT();
        return hr;
    }

    COM_METHOD(UINT,GetExceptionMode)(THIS)
    {
        UINT ui;
        D11_DEVICE_IN();
        ui = m_ptr->GetExceptionMode();
        D11_DEVICE_OUT();
        return ui;
    }


public:
    /*these functions are for the good function*/
    ID3D11Device* GetPointer()
    {
        return m_ptr;
    };
};

#define  DXGI_FACTORY1_IN()  do{DEBUG_INFO("\n");} while(0)
#define  DXGI_FACTORY1_OUT() do{DEBUG_INFO("\n");}while(0)



class CDXGIFactory1Hook : public IDXGIFactory1
{
private:
    IDXGIFactory1 *m_ptr;
public:
    CDXGIFactory1Hook(IDXGIFactory1* pPtr) : m_ptr(pPtr) {};
public:

    COM_METHOD(HRESULT,QueryInterface)(THIS_ REFIID riid,void **ppvObject)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->QueryInterface(riid,ppvObject);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(ULONG,AddRef)(THIS)
    {
        ULONG ul;
        DXGI_FACTORY1_IN();
        ul = m_ptr->AddRef();
        DXGI_FACTORY1_OUT();
        return ul;
    }

    COM_METHOD(ULONG,Release)(THIS)
    {
        ULONG ul;
        DXGI_FACTORY1_IN();
        ul = m_ptr->Release();
        DXGI_FACTORY1_OUT();
        return ul;
    }

    COM_METHOD(HRESULT,SetPrivateData)(THIS_  REFGUID Name,UINT DataSize,const void *pData)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->SetPrivateData(Name,DataSize,pData);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,SetPrivateDataInterface)(THIS_  REFGUID Name,const IUnknown *pUnknown)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->SetPrivateDataInterface(Name,pUnknown);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,GetPrivateData)(THIS_  REFGUID Name,UINT *pDataSize,void *pData)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->GetPrivateData(Name,pDataSize,pData);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,GetParent)(THIS_  REFIID riid,void **ppParent)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->GetParent(riid,ppParent);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,EnumAdapters)(THIS_  UINT Adapter,IDXGIAdapter **ppAdapter)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->EnumAdapters(Adapter,ppAdapter);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,MakeWindowAssociation)(THIS_    HWND WindowHandle,    UINT Flags)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->MakeWindowAssociation(WindowHandle,Flags);
        DXGI_FACTORY1_OUT();
        return hr;
    }


    COM_METHOD(HRESULT,GetWindowAssociation)(THIS_  HWND *pWindowHandle)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->GetWindowAssociation(pWindowHandle);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateSwapChain)(THIS_  IUnknown *pDevice,DXGI_SWAP_CHAIN_DESC *pDesc,IDXGISwapChain **ppSwapChain)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->CreateSwapChain(pDevice,pDesc,ppSwapChain);
        if(SUCCEEDED(hr) && ppSwapChain && *ppSwapChain)
        {
            HRESULT rhr;
            CD3D11DeviceHook *pDevHook=NULL;
            CDXGISwapChainHook *pSwapChainHook=NULL;
            IDXGISwapChain *pGetSwapChain=*ppSwapChain;
            DEBUG_INFO("From Device 0x%p create swapchain 0x%p\n",pDevice,*ppSwapChain);
            rhr = pDevice->QueryInterface(IID_DeviceHookGuid,(void**)&pDevHook);
            if(SUCCEEDED(rhr))
            {
                int registered =0;
                ULONG ul;
                /*yes this is the hook function ,so we release it as quickly as we can*/
                ul = pDevHook->Release();
                assert(ul > 0);
                registered = RegisterFactory1CreateSwapDevice(pDevHook->GetPointer(),pGetSwapChain);
                if(registered)
                {
                    pSwapChainHook = new CDXGISwapChainHook(pGetSwapChain);
                    /*we replace it for our hook object*/
                    *ppSwapChain = (IDXGISwapChain*)pSwapChainHook;
                }
                else
                {
                    /*now success ,so we should make it not right one*/
                    DEBUG_INFO("Not register Device [0x%p] SwapChain 0x%p\n",pDevHook->GetPointer(),pGetSwapChain);
                    ul = pGetSwapChain->Release();
                    if(ul != 0)
                    {
                        DEBUG_INFO("SwapChain Release not 0\n",pGetSwapChain);
                    }
                    *ppSwapChain = NULL;
                    /*make things failed*/
                    hr = E_FAIL;
                }
            }
            else
            {
                DEBUG_INFO("Device(0x%p) not HookDevice object\n",pDevice);
            }
        }
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,CreateSoftwareAdapter)(THIS_  HMODULE Module,IDXGIAdapter **ppAdapter)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->CreateSoftwareAdapter(Module,ppAdapter);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(HRESULT,EnumAdapters1)(THIS_  UINT Adapter,IDXGIAdapter1 **ppAdapter)
    {
        HRESULT hr;
        DXGI_FACTORY1_IN();
        hr = m_ptr->EnumAdapters1(Adapter,ppAdapter);
        DXGI_FACTORY1_OUT();
        return hr;
    }

    COM_METHOD(BOOL,IsCurrent)(THIS)
    {
        BOOL bret;
        DXGI_FACTORY1_IN();
        bret  = m_ptr->IsCurrent();
        DXGI_FACTORY1_OUT();
        return bret;
    }
};



/***********************************************************************
* this is the detours handle functions ,and it will handle for the handle ,we should make the
* dummy class and interface for it
***********************************************************************/
static HRESULT(WINAPI* D3D11CreateDeviceAndSwapChainNext)(IDXGIAdapter *pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        const D3D_FEATURE_LEVEL *pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVersion,
        const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
        IDXGISwapChain **ppSwapChain,
        ID3D11Device **ppDevice,
        D3D_FEATURE_LEVEL *pFeatureLevel,
        ID3D11DeviceContext **ppImmediateContext)
    = D3D11CreateDeviceAndSwapChain;

static HRESULT(WINAPI *CreateDXGIFactory1Next)(REFIID riid, void **ppFactory)=CreateDXGIFactory1;

HRESULT  WINAPI D3D11CreateDeviceAndSwapChainCallBack(
    IDXGIAdapter *pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    IDXGISwapChain **ppSwapChain,
    ID3D11Device **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    ID3D11DeviceContext **ppImmediateContext
)
{
    HRESULT hr;
    IDXGISwapChain *pGetSwapChain=NULL;
    ID3D11Device *pGetDevice=NULL;
    ID3D11DeviceContext *pGetDevCon=NULL;


    hr = D3D11CreateDeviceAndSwapChainNext(pAdapter,DriverType,Software,Flags,pFeatureLevels,
                                           FeatureLevels,SDKVersion,pSwapChainDesc,ppSwapChain,ppDevice,pFeatureLevel,ppImmediateContext);
    if(SUCCEEDED(hr))
    {
        if(ppDevice)
        {
            pGetDevice= *ppDevice;
        }
        if(ppSwapChain)
        {
            pGetSwapChain = *ppSwapChain ;
        }
        if(ppImmediateContext)
        {
            pGetDevCon = *ppImmediateContext;
        }
        DEBUG_INFO("pDevice 0x%p SwapChain 0x%p DeviceContext 0x%p\n",
                   pGetDevice,pGetSwapChain,pGetDevCon);

        if(pGetDevice  || pGetDevCon || pGetSwapChain)
        {
            int registered;
            ULONG ul;
            CD3D11DeviceHook *pDevHook=NULL;
            CD3D11DeviceContextHook *pDevConHook=NULL;
            CDXGISwapChainHook *pSwapChainHook=NULL;
            /*now at least one in the success ,so we should register for the job*/
            registered  = RegisterSwapChainWithDevice(pGetSwapChain,pGetDevice,pGetDevCon);
            if(registered)
            {
                /*register success ,so we shoud allocate for the job*/
                if(pGetDevice)
                {
                    pDevHook = new CD3D11DeviceHook(pGetDevice);
                }
                if(pGetDevCon)
                {
                    pDevConHook = new CD3D11DeviceContextHook(pGetDevCon);
                }
                if(pGetSwapChain)
                {
                    pSwapChainHook = new CDXGISwapChainHook(pGetSwapChain);
                }

                /*to replace the pointer and this will call the things we control*/
                if(ppDevice)
                {
                    *ppDevice=(ID3D11Device*)pDevHook;
                }
                if(ppSwapChain)
                {
                    *ppSwapChain = (IDXGISwapChain*)pSwapChainHook;
                }
                if(ppImmediateContext)
                {
                    *ppImmediateContext= (ID3D11DeviceContext*)pDevConHook;
                }

            }
            else
            {
                /*it is failed ,so we should not return ok*/
                if(pGetSwapChain)
                {
                    ul = pGetSwapChain->Release();
                    if(ul!= 0)
                    {
                        ERROR_INFO("SwapChain(0x%p) release not 0\n",pGetSwapChain);
                    }
                }
                pGetSwapChain = NULL;
                if(pGetDevCon)
                {
                    ul = pGetDevCon->Release();
                    if(ul != 0)
                    {
                        ERROR_INFO("DeviceContext(0x%p) release not 0\n",pGetDevCon);
                    }
                }
                pGetDevCon = NULL;
                if(pGetDevice)
                {
                    ul = pGetDevice->Release();
                    if(ul != 0)
                    {
                        ERROR_INFO("Device(0x%p) release not 0\n",pGetDevice);
                    }
                }
                pGetDevice = NULL;
                if(ppDevice)
                {
                    *ppDevice=pGetDevice;
                }
                if(ppSwapChain)
                {
                    *ppSwapChain = pGetSwapChain;
                }
                if(ppImmediateContext)
                {
                    *ppImmediateContext=pGetDevCon;
                }
                /*set failed*/
                hr = E_FAIL;

            }

        }
    }

    return hr;
}

LONG WINAPI DetourApplicationCrashHandler(EXCEPTION_POINTERS *pException)
{
    StackWalker sw;
    sw.ShowCallstack(GetCurrentThread(), pException->ContextRecord,NULL,NULL);
    return EXCEPTION_EXECUTE_HANDLER;
}


static HRESULT WINAPI CreateDXGIFactory1Callback(REFIID riid, void **ppFactory)
{
    HRESULT hr;
    LPTOP_LEVEL_EXCEPTION_FILTER pOrigExpFilter=NULL;
    pOrigExpFilter = SetUnhandledExceptionFilter(DetourApplicationCrashHandler);
    DEBUG_INFO("origin exception filter 0x%p\n",pOrigExpFilter);

    hr = CreateDXGIFactory1Next(riid,ppFactory);
    if(SUCCEEDED(hr) && riid ==  __uuidof(IDXGIFactory1) && ppFactory && *ppFactory)
    {
        IDXGIFactory1* pFactory1=NULL;
        CDXGIFactory1Hook *pFactoryHook1=NULL;
        pFactory1 = (IDXGIFactory1*) *ppFactory;
        pFactoryHook1 = new CDXGIFactory1Hook(pFactory1);
        *ppFactory = (void**)pFactoryHook1;
        DEBUG_INFO("Get IDXGIFactory1 pFactory1 0x%p\n",pFactory1);
    }
    return hr;
}


static int InitializeD11Hook(void)
{
    assert(D3D11CreateDeviceAndSwapChainNext);
    assert(CreateDXGIFactory1Next);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach((PVOID*)&D3D11CreateDeviceAndSwapChainNext, D3D11CreateDeviceAndSwapChainCallBack);
    DetourAttach((PVOID*)&CreateDXGIFactory1Next,CreateDXGIFactory1Callback);
    DetourTransactionCommit();


    return 0;
}



int RoutineDetourD11(void)
{
    int ret;

    ret = InitializeD11Environment();
    if(ret < 0)
    {
        return ret;
    }
    ret = InitializeD11Hook();
    if(ret < 0)
    {
        return ret;
    }

    return 0;

}


void RotineClearD11(void)
{
    DEBUG_INFO("\n");
    CleanupD11Environment();
    return;
}




int __CaptureFullScreenFileD11(ID3D11Device *pDevice,ID3D11DeviceContext* pContext,IDXGISwapChain* pSwapChain,const char* filetosave)
{
    D3D11_TEXTURE2D_DESC StagingDesc;
    ID3D11Texture2D *pBackBuffer = NULL;
    ID3D11Texture2D *pBackBufferStaging = NULL;
    HRESULT hr;
#ifdef _UNICODE
    wchar_t *pFileToSaveW=NULL;
    int filetosavesize=0;
#endif
    int ret=1;
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if(FAILED(hr))
    {
        ret = GetLastError() > 0 ? GetLastError() : 1;
        DEBUG_INFO("can not get buffer 0x%08x (%d)\n",hr,ret);
        goto out;
    }

    /**/
    pBackBuffer->GetDesc(&StagingDesc);

    StagingDesc.Usage = D3D11_USAGE_STAGING;
    StagingDesc.BindFlags = 0;
    StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = pDevice->CreateTexture2D(&StagingDesc, NULL, &pBackBufferStaging);
    if(FAILED(hr))
    {
        ret = GetLastError() > 0 ? GetLastError() : 1;
        DEBUG_INFO("can not create texture2d 0x%08x (%d)\n",hr,ret);
        goto out;
    }

    pContext->CopyResource(pBackBufferStaging,pBackBuffer);

    SetLastError(0);
#ifdef _UNICODE
    ret = AnsiToUnicode((char*)filetosave,&pFileToSaveW,&filetosavesize);
    if(ret < 0)
    {
        ret = GetLastError() ? GetLastError() : 1;
        goto out;
    }
    hr = D3DX11SaveTextureToFile(pContext,pBackBufferStaging,D3DX11_IFF_BMP,pFileToSaveW);
#else
    hr = D3DX11SaveTextureToFile(pContext,pBackBufferStaging,D3DX11_IFF_BMP,filetosave);
#endif
    if(FAILED(hr))
    {
        ret = GetLastError() > 0 ? GetLastError() : 1;
        DEBUG_INFO("can not save texture file 0x%08x (%d)\n",hr,ret);
        goto out;
    }

    ret = 0;

out:
#ifdef _UNICODE
    AnsiToUnicode(NULL,&pFileToSaveW,&filetosavesize);
#endif
    assert(ret >= 0);
    if(pBackBuffer)
    {
        pBackBuffer->Release();
    }
    pBackBuffer = NULL;
    if(pBackBufferStaging)
    {
        pBackBufferStaging->Release();
    }
    pBackBufferStaging = NULL;
    SetLastError(ret);
    return -ret;
}


int CaptureFullScreenFileD11(const char* filetosave)
{
    int idx=0;
    int cont=0;
    int ret;
    IDXGISwapChain *pSwapChain=NULL;
    ID3D11Device *pDevice=NULL;
    ID3D11DeviceContext *pDeviceContext=NULL;

    __SnapShotDeivces(__FILE__,__FUNCTION__,__LINE__);
    do
    {
        cont = 0;
        assert(pSwapChain==NULL);
        assert(pDevice == NULL);
        assert(pDeviceContext == NULL);
        ret = GrabD11Context(idx,&pSwapChain,&pDevice,&pDeviceContext,1000);
        if(ret < 0)
        {
            ERROR_INFO("can not get context %d\n",idx);
            goto fail;
        }
        else if(ret == 0)
        {
            idx ++;
            cont =1;
            continue;
        }

        /*now we get it*/
        ret = __CaptureFullScreenFileD11(pDevice,pDeviceContext,pSwapChain,filetosave);
        if(ret >=  0)
        {
            break;
        }

        ReleaseD11Context(pSwapChain,pDevice,pDeviceContext);
        pSwapChain = NULL;
        pDevice = NULL;
        pDeviceContext = NULL;
        idx ++;

    }
    while(cont);
    if(pDevice)
    {
        ReleaseD11Context(pSwapChain,pDevice,pDeviceContext);
    }
    pSwapChain = NULL;
    pDevice = NULL;
    pDeviceContext = NULL;

    return 0;
fail:
    if(pDevice)
    {
        ReleaseD11Context(pSwapChain,pDevice,pDeviceContext);
    }
    return ret;
}

