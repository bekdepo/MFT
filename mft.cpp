// mft.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "mft.h"
#include "SimpleOpt.h"
#include <WinIoCtl.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// The one and only application object
#define U8		unsigned char
#define S8		char
#define U16		unsigned short
#define U32		unsigned int
#define U64		unsigned __int64

void HexDump(U8 *addr, U32 len)
{
	U8		*s=addr, *endPtr=(U8*)((U32)addr+len);
	U32		i, remainder=len%16;
	
	printf("\n");
	printf("Offset      Hex Value                                        Ascii value\n");
	printf("-----------------------------------------------------------------------------\n");
	
	// print out 16 byte blocks.
	while (s+16<=endPtr){
		
		// offset 출력.
		printf("0x%08lx  ", (long)(s-addr));
		
		// 16 bytes 단위로 내용 출력.
		for (i=0; i<16; i++){
			printf("%02x ", s[i]);
		}
		printf(" ");
		
		for (i=0; i<16; i++){
			if (s[i]>=32 && s[i]<=125)printf("%c", s[i]);
			else printf(".");
		}
		s += 16;
		printf("\n");
	}
	
	// Print out remainder.
	if (remainder){
		
		// offset 출력.
		printf("0x%08lx  ", (long)(s-addr));
		
		// 16 bytes 단위로 출력하고 남은 것 출력.
		for (i=0; i<remainder; i++){
			printf("%02x ", s[i]);
		}
		for (i=0; i<(16-remainder); i++){
			printf("   ");
		}
		
		printf(" ");
		for (i=0; i<remainder; i++){
			if (s[i]>=32 && s[i]<=125) printf("%c", s[i]);
			else	printf(".");
		}
		for (i=0; i<(16-remainder); i++){
			printf(" ");
		}
		printf("\n");
	}
	return;
}


CString g_strGetPath;
CString g_strOutPath;
CString g_strEnuPath;
BOOL g_bRecursive = FALSE;
BOOL g_bVerbose = FALSE;


VOID ShowUsage()
{
	printf("\nmft.exe [-g FILE|DIR] [-o FILE|DIR] [-r]\n");
	printf("   -g, --get=FILE|DIR       You wanna get a file or folder\n");
	printf("   -o, --output=FILE        Output file path\n");
	printf("   -e, --enumerate=DIR      Enumerate target folder\n");
	printf("   -r, --recursively        Recursively enumerate\n");	
	printf("   -v, --verbose            Verbose mode\n");	
	printf("   -h, --help               Show usage\n");	
	printf("\n");
}

enum 
{
	OPT_GET,	
	OPT_OUTPATH,
	OPT_ENUMERATE,
	OPT_RECURSIVELY,
	OPT_VERBOSE, 
	OPT_HELP 
};

CSimpleOpt::SOption g_rgOptions[] =
{
	{ OPT_GET,		    _T('g'),	_T("get"),		    SO_REQ_SEP },    
    { OPT_OUTPATH,		_T('o'),	_T("output"),		SO_REQ_SEP },
	{ OPT_ENUMERATE,	_T('e'),	_T("enumerate"),	SO_REQ_SEP },
	{ OPT_RECURSIVELY,	_T('r'),	_T("recursively"),	SO_NONE },	
	{ OPT_VERBOSE,		_T('v'),	_T("verbose"),		SO_NONE },
    { OPT_HELP,			_T('h'),	_T("help"),			SO_NONE },
    SO_END_OF_OPTIONS
};


INT Parse(LPCTSTR lpszPath)
{	
	TCHAR DriveLetter;
	TCHAR fullDriveLetter[4];
	DriveLetter = lpszPath[0];
	wsprintf(fullDriveLetter, _T("%c:\\"), DriveLetter);
	
	//
	// Get the cluster size
	//
	DWORD sectorsPerCluster;
	DWORD bytesPerSector;
	DWORD numberOfFreeClusters;
	DWORD totalNumberOfClusters;
	DWORD clusterSizeInBytes;
	if ( !GetDiskFreeSpace( fullDriveLetter, 
							&sectorsPerCluster, 
							&bytesPerSector, 
							&numberOfFreeClusters, 
							&totalNumberOfClusters))	
	{
		return -1;	
	}
	clusterSizeInBytes = sectorsPerCluster * bytesPerSector;

	//
	// Get the volume file system
	//
	TCHAR volumeName[128];
	DWORD volumeSerialNumber;
	DWORD maximumFileNameLength;
	DWORD fileSystemFlags;
	TCHAR fileSystemName[32];
	
	if ( !GetVolumeInformation( fullDriveLetter,
								volumeName,
								128,
								&volumeSerialNumber,
								&maximumFileNameLength,
								&fileSystemFlags,
								fileSystemName,
								32))
	{
		return -1;
	}

	// Get a handle to the volume
	TCHAR VolumeName[7];
	wsprintf(VolumeName, _T("\\\\.\\%c:"), DriveLetter);
	
	HANDLE hVolume = CreateFile(VolumeName,
								GENERIC_READ,
								FILE_SHARE_READ|FILE_SHARE_WRITE,
								NULL,
								OPEN_EXISTING,
								FILE_ATTRIBUTE_NORMAL,
								NULL);
	
	if (hVolume == INVALID_HANDLE_VALUE)
	{		
		return -1;
	}

	//
	// Get the offset of the volume
	//
	int volumeDiskExtentsBufferSize = 1024;
	PVOLUME_DISK_EXTENTS pVolumeDiskExtents = (PVOLUME_DISK_EXTENTS) malloc(volumeDiskExtentsBufferSize);
	
	DWORD bytesReturned;
	
	if ( !DeviceIoControl ( hVolume,
							IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
							NULL,
							0,
							pVolumeDiskExtents,
							volumeDiskExtentsBufferSize,
							&bytesReturned,
							NULL))
	{			
		return -1;
	}

	CloseHandle(hVolume);


	
	if (_tcscmp(fileSystemName, _T("NTFS")) == 0)
	{
		// printf("NTFS\n");

		//
		// Open the disk for RAW access
		//
		TCHAR DiskName[19];
		wsprintf(DiskName, _T("\\\\.\\PHYSICALDRIVE%d"), pVolumeDiskExtents->Extents[0].DiskNumber);
		
		HANDLE hDisk = CreateFile( DiskName,
								   GENERIC_READ,
								   FILE_SHARE_READ,
								   NULL,
								   OPEN_EXISTING,
								   FILE_ATTRIBUTE_NORMAL,
								   NULL);
		
		if (hDisk == INVALID_HANDLE_VALUE)
		{			
			goto CLEANUP;
		}
		
		//
		// Advance to the volume offset
		//
		if ( SetFilePointer( hDisk,
							 pVolumeDiskExtents->Extents[0].StartingOffset.LowPart,
							 &(pVolumeDiskExtents->Extents[0].StartingOffset.HighPart),
							 FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			CloseHandle(hDisk);
			goto CLEANUP;
		}
		
		//
		// Read the boot sector and calculate the offset of the Second cluster
		//		
		char *bootSectorBuffer = (char*) malloc(bytesPerSector);
		DWORD bytesRead;
		BOOL bRead = ReadFile(hDisk, bootSectorBuffer, bytesPerSector, &bytesRead, NULL) != 0;
		
		if (bRead && (bytesRead == bytesPerSector))
		{
			HexDump((BYTE*)bootSectorBuffer, bytesPerSector);			
		}				
		
		CloseHandle(hDisk);
		free(bootSectorBuffer);

		// Got the boot sector
	}

CLEANUP:
	free(pVolumeDiskExtents);	
	return 1;
}


int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	UNUSED_ALWAYS(envp);

	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
		return EXIT_FAILURE;

	CSimpleOpt args(argc, argv, g_rgOptions, TRUE);
    while (args.Next()) 
	{
        if (args.LastError() != SO_SUCCESS) 
		{
            TCHAR* pszError = _T("Unknown error");
            switch (args.LastError()) 
			{
            case SO_OPT_INVALID:  pszError =     _T("Unrecognized option"); break;
            case SO_OPT_MULTIPLE: pszError =     _T("Option matched multiple strings"); break;
            case SO_ARG_INVALID:  pszError =     _T("Option does not accept argument"); break;
            case SO_ARG_INVALID_TYPE: pszError = _T("Invalid argument format"); break;
            case SO_ARG_MISSING:  pszError =     _T("Required argument is missing"); break;
            }
            printf(_T("%s: '%s' (use --help to get command line help)\n"), 
				pszError, args.OptionText());
            return EXIT_FAILURE;
        }

		switch (args.OptionId())
		{
		case OPT_GET:
			g_strGetPath = args.OptionArg();
			break;

		case OPT_OUTPATH:
			g_strOutPath = args.OptionArg();
			break;

		case OPT_ENUMERATE:
			g_strEnuPath = args.OptionArg();
			break;

		case OPT_RECURSIVELY:
			g_bRecursive = TRUE;
			break;

		case OPT_VERBOSE: 
			g_bVerbose = TRUE; 
			break;
			
		default: 
			ShowUsage(); 
			return EXIT_SUCCESS;
		}
    }

	if (!g_strGetPath.IsEmpty())
		Parse(g_strGetPath);

	return EXIT_SUCCESS;
}


