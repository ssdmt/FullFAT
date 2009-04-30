/*****************************************************************************
 *  FullFAT - High Performance, Thread-Safe Embedded FAT File-System         *
 *  Copyright (C) 2009  James Walmsley (james@worm.me.uk)                    *
 *                                                                           *
 *  This program is free software: you can redistribute it and/or modify     *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  This program is distributed in the hope that it will be useful,          *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.    *
 *                                                                           *
 *  IMPORTANT NOTICE:                                                        *
 *  =================                                                        *
 *  Alternative Licensing is available directly from the Copyright holder,   *
 *  (James Walmsley). For more information consult LICENSING.TXT to obtain   *
 *  a Commercial license.                                                    *
 *                                                                           *
 *  See RESTRICTIONS.TXT for extra restrictions on the use of FullFAT.       *
 *                                                                           *
 *  Removing the above notice is illegal and will invalidate this license.   *
 *****************************************************************************
 *  See http://worm.me.uk/fullfat for more information.                      *
 *  Or  http://fullfat.googlecode.com/ for latest releases and the wiki.     *
 *****************************************************************************/

/**
 *	@file		ff_file.c
 *	@author		James Walmsley
 *	@ingroup	FILEIO
 *
 *	@defgroup	FILEIO FILE I/O Access
 *	@brief		Provides an interface to allow File I/O on a mounted IOMAN.
 *
 *	Provides file-system interfaces for the FAT file-system.
 **/

#include "ff_file.h"

/**
 *	@public
 *	@brief	Opens a File for Access
 *
 *	@param	pIoman		FF_IOMAN object that was created by FF_CreateIOMAN().
 *	@param	path		Path to the File or object.
 *	@param	Mode		Access Mode required.
 *	@param	pError		Pointer to a signed byte for error checking. Can be NULL if not required.
 *	@param	pError		To be checked when a NULL pointer is returned.
 *
 *	@return	NULL pointer on Error, in which case pError should be checked for more information.
 *	@return	pError can be:
 **/
FF_FILE *FF_Open(FF_IOMAN *pIoman, FF_T_INT8 *path, FF_T_UINT8 Mode, FF_T_SINT8 *pError) {
	FF_FILE		*pFile;
	FF_FILE		*pFileChain;
	FF_DIRENT	Object, OriginalEntry;
	FF_T_UINT32 DirCluster, FileCluster;
	FF_T_UINT32	nBytesPerCluster;
	FF_T_INT8	filename[FF_MAX_FILENAME];

	FF_T_UINT16	i;

	if(pError) {
		*pError = 0;
	}
	
	if(!pIoman) {
		if(pError) {
			*pError = FF_ERR_NULL_POINTER;
		}
		return (FF_FILE *)NULL;
	}
	pFile = malloc(sizeof(FF_FILE));
	if(!pFile) {
		if(pError) {
			*pError = FF_ERR_NOT_ENOUGH_MEMORY;
		}
		return (FF_FILE *)NULL;
	}

	i = (FF_T_UINT16) strlen(path);

	while(i != 0) {
		if(path[i] == '\\' || path[i] == '/') {
			break;
		}
		i--;
	}

	strncpy(filename, (path + i + 1), FF_MAX_FILENAME);

	if(i == 0) {
		i = 1;
	}
	
	DirCluster = FF_FindDir(pIoman, path, i);
	

	if(DirCluster) {
		FileCluster = FF_FindEntryInDir(pIoman, DirCluster, filename, 0x00, &Object);
		if(!FileCluster) {	// If 0 was returned, it might be because the file has no allocated cluster
			FF_tolower(Object.FileName, FF_MAX_FILENAME);
			FF_tolower(filename, FF_MAX_FILENAME);
			if(Object.Filesize == 0 && strcmp(filename, Object.FileName) == 0) {
				// The file really was found!
				FileCluster = 1;
			} else {
				if(Mode == FF_MODE_WRITE) {
#ifdef FF_LFN_SUPPORT
					strncpy(filename, (path + i), FF_MAX_FILENAME); // Copy back with Case!
#endif
					if(filename[0] == '\\' || filename[0] == '/' ) {
						i = 1;
					} else {
						i = 0;
					}
					FileCluster = FF_CreateFile(pIoman, DirCluster, filename + i, &Object);
					Object.CurrentItem += 1;
				}
			}
		}
		
		if(FileCluster) {
			if(Object.Attrib == FF_FAT_ATTR_DIR) {
				if(Mode != FF_MODE_DIR) {
					// Not the object, File Not Found!
					free(pFile);
					if(pError) {
						*pError = FF_ERR_FILE_OBJECT_IS_A_DIR;
					}
					return (FF_FILE *) NULL;
				}
			}
			
			//---------- Ensure Read-Only files don't get opened for Writing.
			if(Mode == FF_MODE_WRITE) {
				if((Object.Attrib & FF_FAT_ATTR_READONLY)) {
					free(pFile);
					if(pError) {
						*pError = FF_ERR_FILE_IS_READ_ONLY;
					}
					return (FF_FILE *) NULL;
				}
			}
			pFile->pIoman				= pIoman;
			pFile->FilePointer			= 0;
			pFile->ObjectCluster		= Object.ObjectCluster;
			pFile->Filesize				= Object.Filesize;
			pFile->CurrentCluster		= 0;
			pFile->AddrCurrentCluster	= pFile->ObjectCluster;
			pFile->Mode					= Mode;
			pFile->Next					= NULL;
			pFile->DirCluster			= DirCluster;
			pFile->DirEntry				= Object.CurrentItem - 1;
			nBytesPerCluster			= pFile->pIoman->pPartition->SectorsPerCluster / pIoman->BlkSize;
			pFile->iChainLength			= FF_GetChainLength(pIoman, pFile->ObjectCluster);
			pFile->iEndOfChain			= FF_TraverseFAT(pFile->pIoman, pFile->ObjectCluster, pFile->iChainLength);
			pFile->FileDeleted			= FF_FALSE;

			/*
				Add pFile onto the end of our linked list of FF_FILE objects.
			*/
			FF_PendSemaphore(pIoman->pSemaphore);
			{
				if(!pIoman->FirstFile) {
					pIoman->FirstFile = pFile;
				} else {
					pFileChain = (FF_FILE *) pIoman->FirstFile;
					do {
						if(pFileChain->ObjectCluster == pFile->ObjectCluster) {
							// File is already open! DON'T ALLOW IT!
							FF_ReleaseSemaphore(pIoman->pSemaphore);
							free(pFile);
							if(pError) {
								*pError = FF_ERR_FILE_ALREADY_OPEN;
							}
							return (FF_FILE *) NULL;
						}
						if(!pFileChain->Next) {
							pFileChain->Next = pFile;
							break;
						}
						pFileChain = (FF_FILE *) pFileChain->Next;
					}while(pFileChain != NULL);
				}
			}
			FF_ReleaseSemaphore(pIoman->pSemaphore);
			
			if(pFile->Mode == FF_MODE_WRITE) {	// FF doesn't like files with no allocated clusters! Give it 1.
				if(pFile->Filesize == 0 && pFile->ObjectCluster == 0) {	// No Allocated clusters.
					// Create a Cluster chain!
					pFile->AddrCurrentCluster = FF_CreateClusterChain(pFile->pIoman);
					FF_GetEntry(pIoman, pFile->DirEntry, pFile->DirCluster, &OriginalEntry);
					OriginalEntry.ObjectCluster = pFile->AddrCurrentCluster;
					FF_PutEntry(pIoman, pFile->DirEntry, pFile->DirCluster, &OriginalEntry);
					pFile->ObjectCluster = pFile->AddrCurrentCluster;
					pFile->iChainLength = 1;
					pFile->CurrentCluster = 0;
					pFile->iEndOfChain = pFile->AddrCurrentCluster;
				}
			}

			return pFile;
		}else {
			free(pFile);
			if(pError) {
				*pError = FF_ERR_FILE_NOT_FOUND;
			}
			return (FF_FILE *) NULL;
		} 
	}
	if(pError) {
		*pError = FF_ERR_FILE_INVALID_PATH;
	}

	free(pFile);

	return (FF_FILE *)NULL;
}

static FF_T_BOOL FF_isDirEmpty(FF_IOMAN *pIoman, FF_T_INT8 *Path) {
	
	FF_DIRENT MyDir;
	FF_T_SINT8	RetVal = 0;
	FF_T_UINT8	i = 0;
	
	RetVal = FF_FindFirst(pIoman, &MyDir, Path);
	while(RetVal == 0) {
		i++;
		RetVal = FF_FindNext(pIoman, &MyDir);
		if(i > 2) {
			return FF_FALSE;
		}
	}

	return FF_TRUE;
}

FF_T_SINT8 FF_RmDir(FF_IOMAN *pIoman, FF_T_INT8 *path) {
	FF_FILE *pFile;
	FF_T_SINT8 Error = 0;
	FF_T_UINT8 EntryBuffer[32];
	FF_T_SINT8 RetVal = 0;

	if(!pIoman) {
		return FF_ERR_NULL_POINTER;
	}

	pFile = FF_Open(pIoman, path, FF_MODE_DIR, &Error);

	if(!pFile) {
		return Error;	// File in use or File not found!
	}

	pFile->FileDeleted = FF_TRUE;
	
	FF_lockDIR(pIoman);
	{
		if(FF_isDirEmpty(pIoman, path)) {
			FF_lockFAT(pIoman);
			{
				FF_UnlinkClusterChain(pIoman, pFile->ObjectCluster, 0);	// 0 to delete the entire chain!
			}
			FF_unlockFAT(pIoman);
			
			// Edit the Directory Entry! (So it appears as deleted);
			FF_FetchEntry(pIoman, pFile->DirCluster, pFile->DirEntry, EntryBuffer);
			EntryBuffer[0] = 0xE5;
			FF_PushEntry(pIoman, pFile->DirCluster, pFile->DirEntry, EntryBuffer);
			
			FF_IncreaseFreeClusters(pIoman, pFile->iChainLength);

			FF_FlushCache(pIoman);
		} else {
			RetVal = FF_ERR_DIR_NOT_EMPTY;
		}
	}
	FF_unlockDIR(pIoman);
	
	FF_Close(pFile); // Free the file pointer resources
	// File is now lost!
	return RetVal;
}

FF_T_SINT8 FF_RmFile(FF_IOMAN *pIoman, FF_T_INT8 *path) {
	FF_FILE *pFile;
	FF_T_SINT8 Error = 0;
	FF_T_UINT8 EntryBuffer[32];

	pFile = FF_Open(pIoman, path, FF_MODE_READ, &Error);

	if(!pFile) {
		return Error;	// File in use or File not found!
	}

	pFile->FileDeleted = FF_TRUE;

	FF_lockFAT(pIoman);	// Lock the FAT so its thread-safe.
	{
		FF_UnlinkClusterChain(pIoman, pFile->ObjectCluster, 0);	// 0 to delete the entire chain!
	}
	FF_unlockFAT(pIoman);

	FF_IncreaseFreeClusters(pIoman, pFile->iChainLength);

	// Edit the Directory Entry! (So it appears as deleted);
	FF_lockDIR(pIoman);
	{
		FF_FetchEntry(pIoman, pFile->DirCluster, pFile->DirEntry, EntryBuffer);
		EntryBuffer[0] = 0xE5;
		FF_PushEntry(pIoman, pFile->DirCluster, pFile->DirEntry, EntryBuffer);
	}
	FF_unlockDIR(pIoman);

	FF_FlushCache(pIoman);
	
	FF_Close(pFile); // Free the file pointer resources
	return 0;
}


/**
 *	@public
 *	@brief	Get's the next Entry based on the data recorded in the FF_DIRENT object.
 *
 *	@param	pFile		FF_FILE object that was created by FF_Open().
 *
 *	@return FF_TRUE if End of File was reached. FF_FALSE if not.
 *	@return FF_FALSE if a null pointer was provided.
 *
 **/
FF_T_BOOL FF_isEOF(FF_FILE *pFile) {
	if(!pFile) {
		return FF_FALSE;
	}
	if(pFile->FilePointer >= pFile->Filesize) {
		return FF_TRUE;
	} else {
		return FF_FALSE;
	}
}

static FF_T_UINT32 FF_GetSequentialClusters(FF_IOMAN *pIoman, FF_T_UINT32 StartCluster, FF_T_UINT32 Limit) {
	FF_T_UINT32 CurrentCluster;
	FF_T_UINT32 NextCluster = StartCluster;
	FF_T_UINT32 i = 0;

	do {
		CurrentCluster = NextCluster;
		NextCluster = FF_getFatEntry(pIoman, CurrentCluster);
		if(NextCluster == (CurrentCluster + 1)) {
			i++;
		} else {
			break;
		}

		if(Limit) {
			if(i == Limit) {
				break;
			}
		}
	}while(NextCluster == (CurrentCluster + 1));

	return i;
}

static FF_T_SINT32 FF_ReadClusters(FF_FILE *pFile, FF_T_UINT32 Count, FF_T_UINT8 *buffer) {
	FF_T_UINT32 Sectors;
	FF_T_UINT32 SequentialClusters = 0;
	FF_T_UINT32 nItemLBA;
	FF_T_SINT32 RetVal;	

	while(Count != 0) {
		if((Count - 1) > 0) {
			SequentialClusters = FF_GetSequentialClusters(pFile->pIoman, pFile->AddrCurrentCluster, (Count - 1));
		}
		Sectors = (SequentialClusters + 1) * pFile->pIoman->pPartition->SectorsPerCluster;
		nItemLBA = FF_Cluster2LBA(pFile->pIoman, pFile->AddrCurrentCluster);
		nItemLBA = FF_getRealLBA(pFile->pIoman, nItemLBA);

		do {
			if(pFile->pIoman->pBlkDevice->fnReadBlocks) {
				RetVal = pFile->pIoman->pBlkDevice->fnReadBlocks(buffer, nItemLBA, Sectors, pFile->pIoman->pBlkDevice->pParam);
				if(RetVal == FF_ERR_DRIVER_BUSY) {
					FF_Yield();
					FF_Sleep(FF_DRIVER_BUSY_SLEEP);
				}
			} else {
				RetVal = FF_ERR_DEVICE_DRIVER_FAILED;
			}
			
		}while(RetVal == FF_ERR_DRIVER_BUSY);	
		
		Count -= (SequentialClusters + 1);
		pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->AddrCurrentCluster, (SequentialClusters + 1));
		pFile->CurrentCluster += (SequentialClusters + 1);
		buffer += Sectors * pFile->pIoman->BlkSize;
		SequentialClusters = 0;
	}

	return 0;
}


static FF_T_SINT32 FF_ExtendFile(FF_FILE *pFile, FF_T_UINT32 Size) {
	FF_IOMAN	*pIoman = pFile->pIoman;
	FF_T_UINT32 nBytesPerCluster = pIoman->pPartition->BlkSize * pIoman->pPartition->SectorsPerCluster;
	FF_T_UINT32 nTotalClustersNeeded = Size / nBytesPerCluster;
	FF_T_UINT32 nClusterToExtend; 
	FF_T_UINT32 CurrentCluster, NextCluster;
	FF_T_UINT32	i;

	if(Size % nBytesPerCluster) {
		nTotalClustersNeeded += 1;
	}

	nClusterToExtend = (nTotalClustersNeeded - pFile->iChainLength);

	if(nTotalClustersNeeded > pFile->iChainLength) {

		NextCluster = pFile->AddrCurrentCluster;
		FF_lockFAT(pIoman);
		{
			for(i = 0; i <= nClusterToExtend; i++) {
				CurrentCluster = FF_FindEndOfChain(pIoman, NextCluster);
				NextCluster = FF_FindFreeCluster(pIoman);
				FF_putFatEntry(pIoman, CurrentCluster, NextCluster);
				FF_putFatEntry(pIoman, NextCluster, 0xFFFFFFFF);
			}
			
			pFile->iEndOfChain = FF_FindEndOfChain(pIoman, NextCluster);
		}
		FF_unlockFAT(pIoman);
		
		pFile->iChainLength += i;
		FF_DecreaseFreeClusters(pIoman, i);	// Keep Tab of Numbers for fast FreeSize()
	}

	return 0;
}

static FF_T_SINT32 FF_WriteClusters(FF_FILE *pFile, FF_T_UINT32 Count, FF_T_UINT8 *buffer) {
	FF_T_UINT32 Sectors;
	FF_T_UINT32 SequentialClusters = 0;
	FF_T_UINT32 nItemLBA;
	FF_T_SINT32 RetVal;	

	while(Count != 0) {
		if((Count - 1) > 0) {
			SequentialClusters = FF_GetSequentialClusters(pFile->pIoman, pFile->AddrCurrentCluster, (Count - 1));
		}
		Sectors = (SequentialClusters + 1) * pFile->pIoman->pPartition->SectorsPerCluster;
		nItemLBA = FF_Cluster2LBA(pFile->pIoman, pFile->AddrCurrentCluster);
		nItemLBA = FF_getRealLBA(pFile->pIoman, nItemLBA);

		do {
			if(pFile->pIoman->pBlkDevice->fnWriteBlocks) {
				RetVal = pFile->pIoman->pBlkDevice->fnWriteBlocks(buffer, nItemLBA, Sectors, pFile->pIoman->pBlkDevice->pParam);
				if(RetVal == FF_ERR_DRIVER_BUSY) {
					FF_Yield();
					FF_Sleep(FF_DRIVER_BUSY_SLEEP);
				}
			} else {
				RetVal = FF_ERR_DEVICE_DRIVER_FAILED;
			}
			
		}while(RetVal == FF_ERR_DRIVER_BUSY);
		
		Count -= (SequentialClusters + 1);
		pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->AddrCurrentCluster, (SequentialClusters + 1));
		pFile->CurrentCluster += (SequentialClusters + 1);
		buffer += Sectors * pFile->pIoman->BlkSize;
		SequentialClusters = 0;
	}

	return 0;
}

/**
 *	@public
 *	@brief	Equivalent to fread()
 *
 *	@param	pFile		FF_FILE object that was created by FF_Open().
 *	@param	ElementSize	The size of an element to read.
 *	@param	Count		The number of elements to read.
 *	@param	buffer		A pointer to a buffer of adequate size to be filled with the requested data.
 *
 *	@return Number of bytes read.
 *
 **/
FF_T_SINT32 FF_Read(FF_FILE *pFile, FF_T_UINT32 ElementSize, FF_T_UINT32 Count, FF_T_UINT8 *buffer) {
	FF_T_UINT32 nBytes = ElementSize * Count;
	FF_T_UINT32	nBytesRead = 0;
	FF_T_UINT32 nBytesToRead;
	FF_IOMAN	*pIoman;
	FF_BUFFER	*pBuffer;
	FF_T_UINT32 nRelBlockPos;
	FF_T_UINT32	nItemLBA;
	FF_T_SINT32	RetVal = 0;
	FF_T_UINT16	sSectors;
	FF_T_UINT32 nRelClusterPos;
	FF_T_UINT32 nBytesPerCluster;
	FF_T_UINT32	nClusterDiff;

	if(!pFile) {
		return FF_ERR_NULL_POINTER;
	}

	pIoman = pFile->pIoman;

	if(pFile->FilePointer == pFile->Filesize) {
		return 0;
	}

	if((pFile->FilePointer + nBytes) > pFile->Filesize) {
		nBytes = pFile->Filesize - pFile->FilePointer;
	}
	
	nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
	if(nClusterDiff) {
		if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
			pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
			pFile->CurrentCluster += nClusterDiff;
		}
	}

	nRelBlockPos = FF_getMinorBlockEntry(pIoman, pFile->FilePointer, 1); // Get the position within a block.
	
	nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
	nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);

	if((nRelBlockPos + nBytes) < pIoman->BlkSize) {	// Bytes to read are within a block and less than a block size.
		pBuffer = FF_GetBuffer(pIoman, nItemLBA, FF_MODE_READ);
		{
			memcpy(buffer, (pBuffer->pBuffer + nRelBlockPos), nBytes);
		}
		FF_ReleaseBuffer(pIoman, pBuffer);

		pFile->FilePointer += nBytes;
		
		return nBytes;		// Return the number of bytes read.

	} else {

		//---------- Read (memcpy) to a Sector Boundary
		if(nRelBlockPos != 0) {	// Not on a sector boundary, at this point the LBA is known.
			nBytesToRead = pIoman->BlkSize - nRelBlockPos;
			pBuffer = FF_GetBuffer(pIoman, nItemLBA, FF_MODE_READ);
			{
				// Here we copy to the sector boudary.
				memcpy(buffer, (pBuffer->pBuffer + nRelBlockPos), nBytesToRead);
			}
			FF_ReleaseBuffer(pIoman, pBuffer);

			nBytes				-= nBytesToRead;
			nBytesRead			+= nBytesToRead;
			pFile->FilePointer	+= nBytesToRead;
			buffer				+= nBytesToRead;
			
		}

		//---------- Read to a Cluster Boundary
		
		nRelClusterPos = FF_getClusterPosition(pIoman, pFile->FilePointer, 1);
		nBytesPerCluster = (pIoman->pPartition->SectorsPerCluster * pIoman->BlkSize);
		if(nRelClusterPos != 0 && nBytes >= nBytesPerCluster) { // Need to get to cluster boundary
			
			nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
			if(nClusterDiff) {
				if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
					pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
					pFile->CurrentCluster += nClusterDiff;
				}
			}
		
			nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
			nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);

			sSectors = (FF_T_UINT16) (pIoman->pPartition->SectorsPerCluster - (nRelClusterPos / pIoman->BlkSize));
			
			do {
				if(pIoman->pBlkDevice->fnReadBlocks) {
					RetVal = pFile->pIoman->pBlkDevice->fnReadBlocks(buffer, nItemLBA, sSectors, pIoman->pBlkDevice->pParam);
				}
				if(RetVal == FF_ERR_DRIVER_BUSY) {
					FF_Yield();
					FF_Sleep(FF_DRIVER_BUSY_SLEEP);
				}
			}while(RetVal == FF_ERR_DRIVER_BUSY);
			
			nBytesToRead		 = sSectors * pIoman->BlkSize;
			nBytes				-= nBytesToRead;
			buffer				+= nBytesToRead;
			nBytesRead			+= nBytesToRead;
			pFile->FilePointer	+= nBytesToRead;

		}

		//---------- Read Clusters
		if(nBytes >= nBytesPerCluster) {
			FF_ReadClusters(pFile, (nBytes / nBytesPerCluster), buffer);
			nBytesToRead = (nBytesPerCluster *  (nBytes / nBytesPerCluster));

			pFile->FilePointer	+= nBytesToRead;

			nBytes			-= nBytesToRead;
			buffer			+= nBytesToRead;
			nBytesRead		+= nBytesToRead;
		}

		//---------- Read Remaining Blocks
		if(nBytes >= pIoman->BlkSize) {
			sSectors = (FF_T_UINT16) (nBytes / pIoman->BlkSize);
			
			nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
			if(nClusterDiff) {
				if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
					pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
					pFile->CurrentCluster += nClusterDiff;
				}
			}
			
			nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
			nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);
			
			do {
				if(pIoman->pBlkDevice->fnReadBlocks) {
					RetVal = pFile->pIoman->pBlkDevice->fnReadBlocks(buffer, nItemLBA, sSectors, pIoman->pBlkDevice->pParam);
				}
				if(RetVal == FF_ERR_DRIVER_BUSY) {
					FF_Yield();
					FF_Sleep(FF_DRIVER_BUSY_SLEEP);
				}
			}while(RetVal == FF_ERR_DRIVER_BUSY);
			
			nBytesToRead = sSectors * pIoman->BlkSize;
			pFile->FilePointer	+= nBytesToRead;
			nBytes				-= nBytesToRead;
			buffer				+= nBytesToRead;
			nBytesRead			+= nBytesToRead;
		}

		//---------- Read (memcpy) Remaining Bytes
		if(nBytes > 0) {
			
			nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
			if(nClusterDiff) {
				if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
					pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
					pFile->CurrentCluster += nClusterDiff;
				}
			}
			
			nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
			nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);
			pBuffer = FF_GetBuffer(pIoman, nItemLBA, FF_MODE_READ);
			{
				memcpy(buffer, pBuffer->pBuffer, nBytes);
			}
			FF_ReleaseBuffer(pIoman, pBuffer);

			nBytesToRead = nBytes;
			pFile->FilePointer	+= nBytesToRead;
			nBytes				-= nBytesToRead;
			buffer				+= nBytesToRead;
			nBytesRead			+= nBytesToRead;

		}
	}

	return nBytesRead;

}




/**
 *	@public
 *	@brief	Equivalent to fgetc()
 *
 *	@param	pFile		FF_FILE object that was created by FF_Open().
 *
 *	@return The character that was read (cast as a 32-bit interger). -1 on EOF.
 *	@return -2 If a null file pointer was provided.
 *	@return -3 Device access failed.
 *
 **/
FF_T_INT32 FF_GetC(FF_FILE *pFile) {
	FF_T_UINT32		fileLBA;
	FF_BUFFER		*pBuffer;
	FF_T_UINT8		retChar;
	FF_T_UINT32		relMinorBlockPos;
	FF_T_UINT32     clusterNum;
	FF_T_UINT32		nClusterDiff;
	
	
	if(!pFile) {
		return FF_ERR_NULL_POINTER;
	}
	
	if(pFile->FilePointer >= pFile->Filesize) {
		return -1; // EOF!	
	}

	relMinorBlockPos	= FF_getMinorBlockEntry(pFile->pIoman, pFile->FilePointer, 1);
	clusterNum			= FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1);

	nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
	if(nClusterDiff) {
		if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
			pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->AddrCurrentCluster, nClusterDiff);
			pFile->CurrentCluster += nClusterDiff;
		}
	}
	

	fileLBA = FF_Cluster2LBA(pFile->pIoman, pFile->AddrCurrentCluster)	+ FF_getMajorBlockNumber(pFile->pIoman, pFile->FilePointer, (FF_T_UINT16) 1);
	fileLBA = FF_getRealLBA (pFile->pIoman, fileLBA)		+ FF_getMinorBlockNumber(pFile->pIoman, pFile->FilePointer, (FF_T_UINT16) 1);
	
	pBuffer = FF_GetBuffer(pFile->pIoman, fileLBA, FF_MODE_READ);
	{
		if(!pBuffer) {
			return -3;
		}
		retChar = pBuffer->pBuffer[relMinorBlockPos];
	}
	FF_ReleaseBuffer(pFile->pIoman, pBuffer);

	pFile->FilePointer += 1;

	return (FF_T_INT32) retChar;
}

FF_T_UINT32 FF_Tell(FF_FILE *pFile) {
	return pFile->FilePointer;
}



FF_T_SINT32 FF_Write(FF_FILE *pFile, FF_T_UINT32 ElementSize, FF_T_UINT32 Count, FF_T_UINT8 *buffer) {
	FF_T_UINT32 nBytes = ElementSize * Count;
	FF_T_UINT32	nBytesWritten = 0;
	FF_T_UINT32 nBytesToWrite;
	FF_IOMAN	*pIoman;
	FF_BUFFER	*pBuffer;
	FF_T_UINT32 nRelBlockPos;
	FF_T_UINT32	nItemLBA;
	FF_T_SINT32	RetVal = 0;
	FF_T_UINT16	sSectors;
	FF_T_UINT32 nRelClusterPos;
	FF_T_UINT32 nBytesPerCluster, nClusterDiff, nClusters;

	if(!pFile) {
		return FF_ERR_NULL_POINTER;
	}

	pIoman = pFile->pIoman;

	nBytesPerCluster = (pIoman->pPartition->SectorsPerCluster * pIoman->BlkSize);

	// Extend File for atleast nBytes!
	// Handle file-space allocation
	FF_ExtendFile(pFile, pFile->FilePointer + nBytes);

	nRelBlockPos = FF_getMinorBlockEntry(pIoman, pFile->FilePointer, 1); // Get the position within a block.
	
	nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
	if(nClusterDiff) {
		if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
			pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
			pFile->CurrentCluster += nClusterDiff;
		}
	}
	
	nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
	nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);

	if((nRelBlockPos + nBytes) < pIoman->BlkSize) {	// Bytes to read are within a block and less than a block size.
		pBuffer = FF_GetBuffer(pIoman, nItemLBA, FF_MODE_WRITE);
		{
			memcpy((pBuffer->pBuffer + nRelBlockPos), buffer, nBytes);
		}
		FF_ReleaseBuffer(pIoman, pBuffer);

		pFile->FilePointer += nBytes;
		
		
		
		return nBytes;		// Return the number of bytes read.

	} else {

		//---------- Write (memcpy) to a Sector Boundary
		if(nRelBlockPos != 0) {	// Not on a sector boundary, at this point the LBA is known.
			nBytesToWrite = pIoman->BlkSize - nRelBlockPos;
			pBuffer = FF_GetBuffer(pIoman, nItemLBA, FF_MODE_WRITE);
			{
				// Here we copy to the sector boudary.
				memcpy((pBuffer->pBuffer + nRelBlockPos), buffer, nBytesToWrite);
			}
			FF_ReleaseBuffer(pIoman, pBuffer);

			nBytes				-= nBytesToWrite;
			nBytesWritten		+= nBytesToWrite;
			pFile->FilePointer	+= nBytesToWrite;
			buffer				+= nBytesToWrite;
		}

		//---------- Write to a Cluster Boundary
		
		nRelClusterPos = FF_getClusterPosition(pIoman, pFile->FilePointer, 1);
		if(nRelClusterPos != 0 && nBytes >= nBytesPerCluster) { // Need to get to cluster boundary
			
			nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
			if(nClusterDiff) {
				if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
					pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
					pFile->CurrentCluster += nClusterDiff;
				}
			}
		
			nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
			nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);

			sSectors = (FF_T_UINT16) (pIoman->pPartition->SectorsPerCluster - (nRelClusterPos / pIoman->BlkSize));
			
			do {
				if(pIoman->pBlkDevice->fnWriteBlocks) {
					RetVal = pFile->pIoman->pBlkDevice->fnWriteBlocks(buffer, nItemLBA, sSectors, pIoman->pBlkDevice->pParam);
				}
				if(RetVal == FF_ERR_DRIVER_BUSY) {
					FF_Yield();
					FF_Sleep(FF_DRIVER_BUSY_SLEEP);
				}
			}while(RetVal == FF_ERR_DRIVER_BUSY);
			
			nBytesToWrite		 = sSectors * pIoman->BlkSize;
			nBytes				-= nBytesToWrite;
			buffer				+= nBytesToWrite;
			nBytesWritten		+= nBytesToWrite;
			pFile->FilePointer	+= nBytesToWrite;

		}

		//---------- Write Clusters
		if(nBytes >= nBytesPerCluster) {

			nClusters = (nBytes / nBytesPerCluster);
			
			FF_WriteClusters(pFile, nClusters, buffer);
			
			nBytesToWrite = (nBytesPerCluster *  nClusters);
			
			pFile->FilePointer	+= nBytesToWrite;

			nBytes				-= nBytesToWrite;
			buffer				+= nBytesToWrite;
			nBytesWritten		+= nBytesToWrite;
		}

		//---------- Write Remaining Blocks
		if(nBytes >= pIoman->BlkSize) {
			sSectors = (FF_T_UINT16) (nBytes / pIoman->BlkSize);
			
			nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
			if(nClusterDiff) {
				if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
					pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
					pFile->CurrentCluster += nClusterDiff;
				}
			}			
			
			nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
			nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);
			
			do {
				if(pIoman->pBlkDevice->fnWriteBlocks) {
					RetVal = pFile->pIoman->pBlkDevice->fnWriteBlocks(buffer, nItemLBA, sSectors, pIoman->pBlkDevice->pParam);
				}
				if(RetVal == FF_ERR_DRIVER_BUSY) {
					FF_Yield();
					FF_Sleep(FF_DRIVER_BUSY_SLEEP);
				}
			}while(RetVal == FF_ERR_DRIVER_BUSY);
			
			nBytesToWrite = sSectors * pIoman->BlkSize;
			pFile->FilePointer	+= nBytesToWrite;
			nBytes				-= nBytesToWrite;
			buffer				+= nBytesToWrite;
			nBytesWritten		+= nBytesToWrite;

		}

		//---------- Write (memcpy) Remaining Bytes
		if(nBytes > 0) {
			
			nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
			if(nClusterDiff) {
				if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
					pFile->AddrCurrentCluster = FF_TraverseFAT(pIoman, pFile->AddrCurrentCluster, nClusterDiff);
					pFile->CurrentCluster += nClusterDiff;
				}
			}
			
			nItemLBA = FF_Cluster2LBA(pIoman, pFile->AddrCurrentCluster);
			nItemLBA = FF_getRealLBA(pIoman, nItemLBA + FF_getMajorBlockNumber(pIoman, pFile->FilePointer, 1)) + FF_getMinorBlockNumber(pIoman, pFile->FilePointer, 1);
			pBuffer = FF_GetBuffer(pIoman, nItemLBA, FF_MODE_WRITE);
			{
				memcpy(pBuffer->pBuffer, buffer, nBytes);
			}
			FF_ReleaseBuffer(pIoman, pBuffer);

			nBytesToWrite = nBytes;
			pFile->FilePointer	+= nBytesToWrite;
			nBytes				-= nBytesToWrite;
			buffer				+= nBytesToWrite;
			nBytesWritten			+= nBytesToWrite;

		}
	}

	if(pFile->FilePointer > pFile->Filesize) {
		pFile->Filesize = pFile->FilePointer;
	}

	return nBytesWritten;

}



FF_T_SINT8 FF_PutC(FF_FILE *pFile, FF_T_UINT8 pa_cValue) {
	FF_BUFFER	*pBuffer;
	FF_T_UINT32 iItemLBA;
	FF_T_UINT32 iRelPos				= FF_getMinorBlockEntry		(pFile->pIoman, pFile->FilePointer, 1);
	FF_T_UINT32 nClusterDiff;
	
	if(!pFile) {	// Ensure we don't have a Null file pointer on a Public interface.
		return FF_ERR_NULL_POINTER;
	}
	
	// Handle File Space Allocation.
	FF_ExtendFile(pFile, pFile->FilePointer + 1);
	
	nClusterDiff = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1) - pFile->CurrentCluster;
	if(nClusterDiff) {
		if(pFile->CurrentCluster < FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1)) {
			pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->AddrCurrentCluster, nClusterDiff);
			pFile->CurrentCluster += nClusterDiff;
		}
	}

	iItemLBA = FF_Cluster2LBA(pFile->pIoman, pFile->AddrCurrentCluster) + FF_getMajorBlockNumber(pFile->pIoman, pFile->FilePointer, (FF_T_UINT16) 1);
	iItemLBA = FF_getRealLBA (pFile->pIoman, iItemLBA)			+ FF_getMinorBlockNumber(pFile->pIoman, pFile->FilePointer, (FF_T_UINT16) 1);
	
	pBuffer = FF_GetBuffer(pFile->pIoman, iItemLBA, FF_MODE_WRITE);
	{
		FF_putChar(pBuffer->pBuffer, (FF_T_UINT16) iRelPos, pa_cValue);
	}
	FF_ReleaseBuffer(pFile->pIoman, pBuffer);

	pFile->FilePointer += 1;
	if(pFile->Filesize < (pFile->FilePointer)) {
		pFile->Filesize += 1;
	}
	return 0;
}



/**
 *	@public
 *	@brief	Equivalent to fseek()
 *
 *	@param	pFile		FF_FILE object that was created by FF_Open().
 *	@param	Offset		An integer (+/-) to seek to, from the specified origin.
 *	@param	Origin		Where to seek from. (FF_SEEK_SET seek from start, FF_SEEK_CUR seek from current position, or FF_SEEK_END seek from end of file).
 *
 *	@return 0 on Sucess, 
 *	@return -2 if offset results in an invalid position in the file. 
 *	@return -1 if a FF_FILE pointer was not recieved.
 *	@return -3 if an invalid origin was provided.
 *	
 **/
FF_T_SINT8 FF_Seek(FF_FILE *pFile, FF_T_SINT32 Offset, FF_T_INT8 Origin) {
	if(!pFile) {
		return -1;
	}

	switch(Origin) {
		case FF_SEEK_SET:
			if((FF_T_UINT32) Offset <= pFile->Filesize && Offset >= 0) {
				pFile->FilePointer = Offset;
				pFile->CurrentCluster = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1);
				pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->ObjectCluster, pFile->CurrentCluster);
			} else {
				return -2;
			}
			break;

		case FF_SEEK_CUR:
			if((Offset + pFile->FilePointer) <= pFile->Filesize && (Offset + (FF_T_SINT32) pFile->FilePointer) >= 0) {
				pFile->FilePointer = Offset + pFile->FilePointer;
				pFile->CurrentCluster = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1);
				pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->ObjectCluster, pFile->CurrentCluster);
			} else {
				return -2;
			}
			break;
	
		case FF_SEEK_END:
			if((Offset + (FF_T_SINT32) pFile->Filesize) >= 0 && (Offset + pFile->Filesize) <= pFile->Filesize) {
				pFile->FilePointer = Offset + pFile->Filesize;
				pFile->CurrentCluster = FF_getClusterChainNumber(pFile->pIoman, pFile->FilePointer, 1);
				pFile->AddrCurrentCluster = FF_TraverseFAT(pFile->pIoman, pFile->ObjectCluster, pFile->CurrentCluster);
			} else {
				return -2;
			}
			break;

		default:
			return -3;
		
	}

	return 0;
}


/**
 *	@public
 *	@brief	Equivalent to fclose()
 *
 *	@param	pFile		FF_FILE object that was created by FF_Open().
 *
 *	@return 0 on sucess.
 *	@return -1 if a null pointer was provided.
 *
 **/
FF_T_SINT8 FF_Close(FF_FILE *pFile) {

	FF_FILE *pFileChain;
	FF_DIRENT OriginalEntry;

	if(!pFile) {
		return FF_ERR_NULL_POINTER;	
	}
	// UpDate Dirent if File-size has changed?

	// Update the Dirent!
	FF_GetEntry(pFile->pIoman, pFile->DirEntry, pFile->DirCluster, &OriginalEntry);
	
	if(!pFile->FileDeleted) {
		if(pFile->Filesize != OriginalEntry.Filesize) {
			OriginalEntry.Filesize = pFile->Filesize;
			FF_PutEntry(pFile->pIoman, pFile->DirEntry, pFile->DirCluster, &OriginalEntry);
		}
	}

	if(pFile->Mode == FF_MODE_WRITE) {
		FF_FlushCache(pFile->pIoman);		// Ensure all modfied blocks are flushed to disk!
	}
	
	// Handle Linked list!
	FF_PendSemaphore(pFile->pIoman->pSemaphore);
	{	// Semaphore is required, or linked list could become corrupted.
		if(pFile->pIoman->FirstFile == pFile) {
			pFile->pIoman->FirstFile = pFile->Next;
		} else {
			pFileChain = (FF_FILE *) pFile->pIoman->FirstFile;
			while(pFileChain->Next != pFile) {
				pFileChain = pFileChain->Next;
			}
			pFileChain->Next = pFile->Next;
		}
	}	// Semaphore released, linked list was shortened!
	FF_ReleaseSemaphore(pFile->pIoman->pSemaphore);

	// If file written, flush to disk
	free(pFile);
	// Simply free the pointer!
	return 0;
}
