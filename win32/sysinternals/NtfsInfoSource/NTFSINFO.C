//====================================================================
//
// Ntsinfo.c
//
// Copyright (C) 1997 Mark Russinovich
// http://www.ntinternals.com
//
// Shows NTFS volume information.
//
//====================================================================
#include <windows.h>
#include <stdio.h>
#include <conio.h>

//
// File System Control command for getting NTFS information
//
#define FSCTL_GET_VOLUME_INFORMATION	0x90064


//
// return code type
//
typedef UINT NTSTATUS;

//
// Error codes returned by NtFsControlFile (see NTSTATUS.H)
//
#define STATUS_SUCCESS			         ((NTSTATUS)0x00000000L)
#define STATUS_BUFFER_OVERFLOW           ((NTSTATUS)0x80000005L)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000DL)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_ALREADY_COMMITTED         ((NTSTATUS)0xC0000021L)
#define STATUS_INVALID_DEVICE_REQUEST    ((NTSTATUS)0xC0000010L)

//
// Metadata file names
//
char MetaFileNames[][32] = {
	"$mft",
	"$mftmirr",
	"$logfile",
	"$volume",
	"$attrdef",
	"$bitmap",
	"$boot",
	"$badclus",
	"$quota",
	"$badclust",
	"$upcase",
	""
};


//
// Io Status block (see NTDDK.H)
//
typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;


//
// Apc Routine (see NTDDK.H)
//
typedef VOID (*PIO_APC_ROUTINE) (
				PVOID ApcContext,
				PIO_STATUS_BLOCK IoStatusBlock,
				ULONG Reserved
			);


//
// The undocumented NtFsControlFile
//
// This function is used to send File System Control (FSCTL)
// commands into file system drivers. Its definition is 
// in ntdll.dll (ntdll.lib), a file shipped with the NTDDK.
//
NTSTATUS (__stdcall *NtFsControlFile)( 
					HANDLE FileHandle,
					HANDLE Event,					// optional
					PIO_APC_ROUTINE ApcRoutine,		// optional
					PVOID ApcContext,				// optional
					PIO_STATUS_BLOCK IoStatusBlock,	
					ULONG FsControlCode,
					PVOID InputBuffer,				// optional
					ULONG InputBufferLength,
					PVOID OutputBuffer,				// optional
					ULONG OutputBufferLength
			); 


//
// NTFS volume information
//
typedef struct {
	LARGE_INTEGER    	SerialNumber;
	LARGE_INTEGER    	NumberOfSectors;
	LARGE_INTEGER    	TotalClusters;
	LARGE_INTEGER    	FreeClusters;
	LARGE_INTEGER    	Reserved;
	ULONG    			BytesPerSector;
	ULONG    			BytesPerCluster;
	ULONG    			BytesPerMFTRecord;
	ULONG    			ClustersPerMFTRecord;
	LARGE_INTEGER    	MFTLength;
	LARGE_INTEGER    	MFTStart;
	LARGE_INTEGER    	MFTMirrorStart;
	LARGE_INTEGER    	MFTZoneStart;
	LARGE_INTEGER    	MFTZoneEnd;
} NTFS_VOLUME_DATA_BUFFER, *PNTFS_VOLUME_DATA_BUFFER;

//--------------------------------------------------------------------
//                      F U N C T I O N S
//--------------------------------------------------------------------

//--------------------------------------------------------------------
//
// PrintNtError
//
// Translates an NTDLL error code into its text equivalent. This
// only deals with ones commonly returned by defragmenting FS Control
// commands.
//--------------------------------------------------------------------
void PrintNtError( NTSTATUS Status )
{
	switch( Status ) {
	case STATUS_SUCCESS:
		printf("STATUS_SUCCESS\n\n");
		break;
	case STATUS_INVALID_PARAMETER:
		printf("STATUS_INVALID_PARAMETER\n\n");
		break;
	case STATUS_BUFFER_TOO_SMALL:
		printf("STATUS_BUFFER_TOO_SMALL\n\n");
		break;
	case STATUS_ALREADY_COMMITTED:
		printf("STATUS_ALREADY_COMMITTED\n\n");
		break;
	case STATUS_INVALID_DEVICE_REQUEST:
		printf("STATUS_INVALID_DEVICE_REQUEST\n\n");
		break;
	default:
		printf("0x%08x\n\n", Status );
		break;
	}		  
}


//--------------------------------------------------------------------
//
// PrintWin32Error
// 
// Translates a Win32 error into a text equivalent
//
//--------------------------------------------------------------------
void PrintWin32Error( DWORD ErrorCode )
{
	LPVOID lpMsgBuf;
 
	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
					NULL, ErrorCode, 
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPTSTR) &lpMsgBuf, 0, NULL );
	printf("%s\n", lpMsgBuf );
	LocalFree( lpMsgBuf );
}


//--------------------------------------------------------------------
//
// DumpMetaData
//
// Does directory lookups of NTFS metadata files.
//
//--------------------------------------------------------------------
void DumpMetaFiles( int DriveId )
{
	char	fileName[1024];
	WIN32_FIND_DATA findData;
	HANDLE	findHandle;
	int		i;

	//
	// Loop through array of files
	//
	for( i = 0; ; i++ ) {

		if( !(*MetaFileNames[i]) )  break;

		sprintf( fileName, "%c:\\%s", DriveId + 'A', MetaFileNames[i] );
		findHandle = FindFirstFile( fileName, &findData );
		if( findHandle != INVALID_HANDLE_VALUE ) {

			printf("%-11s	%d bytes\n", findData.cFileName, findData.nFileSizeLow );
			FindClose( findHandle );
		}
	}
}

//--------------------------------------------------------------------
//
// GetNTFSInfo
//
// Open the volume and query its data.
//
//--------------------------------------------------------------------
BOOLEAN GetNTFSInfo( int DriveId, PNTFS_VOLUME_DATA_BUFFER VolumeInfo ) 
{
	static char			volumeName[] = "\\\\.\\A:";
	HANDLE				volumeHandle;
	IO_STATUS_BLOCK		ioStatus;
	NTSTATUS			status;

	//
	// open the volume
	//
	volumeName[4] = DriveId + 'A'; 
	volumeHandle = CreateFile( volumeName, GENERIC_READ, 
					FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
					0, 0 );
	if( volumeHandle == INVALID_HANDLE_VALUE )	{

		printf("\nError opening volume: ");
		PrintWin32Error( GetLastError() );
		return FALSE;
	}

	//
	// Query the volume information
	//
	status = NtFsControlFile( volumeHandle, NULL, NULL, 0, &ioStatus,
						FSCTL_GET_VOLUME_INFORMATION,
						NULL, 0,
						VolumeInfo, sizeof( NTFS_VOLUME_DATA_BUFFER ) );

	if( status != STATUS_SUCCESS ) {
		
		printf("\nError obtaining NTFS information: ");
		PrintNtError( status );
		CloseHandle( volumeHandle );
		return FALSE;
	}

	//
	// Close the volume
	//
	CloseHandle( volumeHandle );

	return TRUE;
}

//--------------------------------------------------------------------
//
// LocateNDLLCalls
//
// Loads function entry points in NTDLL
//
//--------------------------------------------------------------------
BOOLEAN LocateNTDLLCalls()
{

 	if( !(NtFsControlFile = (void *) GetProcAddress( GetModuleHandle("ntdll.dll"),
			"NtFsControlFile" )) ) {

		return FALSE;
	}
}


//--------------------------------------------------------------------
//
// main
//
// Just open the volume and dump its information.
//
//--------------------------------------------------------------------
int main( int argc, char *argv[])
{
	int							drive;
	NTFS_VOLUME_DATA_BUFFER		volumeInfo;

	//
	// Get the drive to open off the command line
	//
	if( argc != 2) {
		printf("Usage: %s <drive letter>\n", argv[0] );
		exit(1);
	}

	printf("\nNTFS Information Dump\n");
	printf("Copyright (C) 1997 Mark Russinovich\n");
	printf("http://www.ntinternals.com\n");

	if( !LocateNTDLLCalls() ) {

		printf("Not running on supported version of Windows NT.\n");
		exit(1);
	}

	if( argv[1][0] >= 'a' && argv[1][0] <= 'z' ) {
		drive = argv[1][0] - 'a';
	} else if( argv[1][0] >= 'A' && argv[1][0] <= 'Z' ) {
		drive = argv[1][0] - 'A';
	} else if( argv[1][0] == '/' ) {
		printf("Usage: %s <drive letter>\n", argv[0] );
		exit(1);
	} else {
		printf("illegal drive: %c\n", argv[1][0] );
		exit(1);
	}

	//
	// Get ntfs volume data
	//
	if( GetNTFSInfo( drive, &volumeInfo )) {

		printf("\nVolume Size\n");
		printf("-----------\n");
		printf("Volume size            : %I64d MB\n", (volumeInfo.NumberOfSectors.QuadPart *
			volumeInfo.BytesPerSector) / (1024 * 1024) );
		printf("Total sectors          : %I64d\n", volumeInfo.NumberOfSectors.QuadPart );
		printf("Total clusters         : %I64d\n", volumeInfo.TotalClusters.QuadPart );
		printf("Free clusters          : %I64d\n", volumeInfo.FreeClusters.QuadPart );
		printf("Free space             : %I64d MB (%I64d%% of drive)\n", (volumeInfo.FreeClusters.QuadPart * 
			 volumeInfo.BytesPerCluster) / (1024 * 1024),
			 (volumeInfo.FreeClusters.QuadPart * 100)/
			  volumeInfo.TotalClusters.QuadPart);
		//printf("Total reserved         : %I64d\n", volumeInfo.TotalReserved.QuadPart );

		printf("\nAllocation Size\n");
		printf("----------------\n");
		printf("Bytes per sector       : %d\n", volumeInfo.BytesPerSector );
		printf("Bytes per cluster      : %d\n", volumeInfo.BytesPerCluster );
		printf("Bytes per MFT record   : %d\n", volumeInfo.BytesPerMFTRecord );
		printf("Clusters per MFT record: %d\n", volumeInfo.ClustersPerMFTRecord );

		printf("\nMFT Information\n");
		printf("---------------\n");
		printf("MFT size               : %I64d MB (%I64d%% of drive)\n", volumeInfo.MFTLength.QuadPart 
			/ (1024*1024),
			(volumeInfo.MFTLength.QuadPart * 100 / volumeInfo.BytesPerCluster) /
			volumeInfo.TotalClusters.QuadPart );
		printf("MFT start cluster      : %I64d\n", volumeInfo.MFTStart.QuadPart );
		printf("MFT zone clusters      : %I64d - %I64d\n",
			volumeInfo.MFTZoneStart.QuadPart, volumeInfo.MFTZoneEnd.QuadPart );
		printf("MFT zone size          : %I64d MB (%I64d%% of drive)\n",
			(volumeInfo.MFTZoneEnd.QuadPart - volumeInfo.MFTZoneStart.QuadPart) * 
			volumeInfo.BytesPerCluster / (1024*1024),
			(volumeInfo.MFTZoneEnd.QuadPart - volumeInfo.MFTZoneStart.QuadPart) * 100/
			volumeInfo.TotalClusters.QuadPart );
		printf("MFT mirror start       : %I64d\n", volumeInfo.MFTMirrorStart.QuadPart	);

		printf("\nMeta-Data files\n");
		printf("---------------\n");
		DumpMetaFiles( drive );
		return(0);

	} else  {

		return(1);
	}
}
