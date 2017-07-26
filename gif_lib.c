// adapted from libungif 4.1.0 (MIT license)

/******************************************************************************
*   "Gif-Lib" - Yet another gif library.				      *
*									      *
* Written by:  Gershon Elber			IBM PC Ver 1.1,	Aug. 1990     *
*******************************************************************************
* The kernel of the GIF Decoding process can be found here.		      *
*******************************************************************************
* History:								      *
* 16 Jun 89 - Version 1.0 by Gershon Elber.				      *
*  3 Sep 90 - Version 1.1 by Gershon Elber (Support for Gif89, Unique names). *
******************************************************************************/


#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "gif_lib.h"

#define PROGRAM_NAME	"GIFLIB"

#define LZ_MAX_CODE	4095		/* Biggest code possible in 12 bits. */
#define LZ_BITS		12

#define FLUSH_OUTPUT		4096    /* Impossible code, to signal flush. */
#define FIRST_CODE		4097    /* Impossible code, to signal first. */
#define NO_SUCH_CODE		4098    /* Impossible code, to signal empty. */

#define FILE_STATE_WRITE    0x01
#define FILE_STATE_SCREEN   0x02
#define FILE_STATE_IMAGE    0x04
#define FILE_STATE_READ     0x08

#define IS_READABLE(Private)    (Private->FileState & FILE_STATE_READ)
#define IS_WRITEABLE(Private)   (Private->FileState & FILE_STATE_WRITE)


typedef struct GifFilePrivateType {
    int FileState,
	FileHandle,			     /* Where all this data goes to! */
	BitsPerPixel,	    /* Bits per pixel (Codes uses at least this + 1). */
	ClearCode,				       /* The CLEAR LZ code. */
	EOFCode,				         /* The EOF LZ code. */
	RunningCode,		    /* The next code algorithm can generate. */
	RunningBits,/* The number of bits required to represent RunningCode. */
	MaxCode1,  /* 1 bigger than max. possible code, in RunningBits bits. */
	LastCode,		        /* The code before the current code. */
	CrntCode,				  /* Current algorithm code. */
	StackPtr,		         /* For character stack (see below). */
	CrntShiftState;		        /* Number of bits in CrntShiftDWord. */
    unsigned long CrntShiftDWord;     /* For bytes decomposition into codes. */
    unsigned long PixelCount;		       /* Number of pixels in image. */
    FILE *File;						  /* File as stream. */
    InputFunc Read;                 /* function to read gif input (TVT) */
    OutputFunc Write;               /* function to write gif output (MRB) */
    GifByteType Buf[256];	       /* Compressed input is buffered here. */
    GifByteType Stack[LZ_MAX_CODE];	 /* Decoded pixels are stacked here. */
    GifByteType Suffix[LZ_MAX_CODE+1];	       /* So we can trace the codes. */
    unsigned int Prefix[LZ_MAX_CODE+1];
} GifFilePrivateType;

extern int _GifError;


#define COMMENT_EXT_FUNC_CODE	0xfe /* Extension function code for comment. */
#define GIF_STAMP	"GIFVER"	 /* First chars in file - GIF stamp. */
#define GIF_STAMP_LEN	sizeof(GIF_STAMP) - 1
#define GIF_VERSION_POS	3		/* Version first character in stamp. */

/* avoid extra function call in case we use fread (TVT) */
#define READ(_gif,_buf,_len)                                     \
  (((GifFilePrivateType*)_gif->Private)->Read ?                   \
    ((GifFilePrivateType*)_gif->Private)->Read(_gif,_buf,_len) : \
    fread(_buf,1,_len,((GifFilePrivateType*)_gif->Private)->File))

static int DGifGetWord(GifFileType *GifFile, int *Word);
static int DGifSetupDecompress(GifFileType *GifFile);
static int DGifDecompressLine(GifFileType *GifFile, GifPixelType *Line,
								int LineLen);
static int DGifGetPrefixChar(unsigned int *Prefix, int Code, int ClearCode);
static int DGifDecompressInput(GifFileType *GifFile, int *Code);
static int DGifBufferedInput(GifFileType *GifFile, GifByteType *Buf,
						     GifByteType *NextByte);

/******************************************************************************
*   Open a new gif file for read, given by its name.			      *
*   Returns GifFileType pointer dynamically allocated which serves as the gif *
* info record. _GifError is cleared if succesfull.			      *
******************************************************************************/
GifFileType *DGifOpenFileName(const char *FileName)
{
    int FileHandle;
    GifFileType *GifFile;

    if ((FileHandle = open(FileName, O_RDONLY)) == -1) {
	_GifError = D_GIF_ERR_OPEN_FAILED;
	return NULL;
    }

    GifFile = DGifOpenFileHandle(FileHandle);
    if (GifFile == (GifFileType *)NULL)
        close(FileHandle);
    return GifFile;
}

/******************************************************************************
*   Update a new gif file, given its file handle.                             *
*   Returns GifFileType pointer dynamically allocated which serves as the gif *
*   info record. _GifError is cleared if succesfull.                          *
******************************************************************************/
GifFileType *DGifOpenFileHandle(int FileHandle)
{
    char Buf[GIF_STAMP_LEN+1];
    GifFileType *GifFile;
    GifFilePrivateType *Private;
    FILE *f;

    if ((GifFile = (GifFileType *) malloc(sizeof(GifFileType))) == NULL) {
        _GifError = D_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    memset(GifFile, '\0', sizeof(GifFileType));

    if ((Private = (GifFilePrivateType *) malloc(sizeof(GifFilePrivateType)))
    == NULL) {
        _GifError = D_GIF_ERR_NOT_ENOUGH_MEM;
        free((char *) GifFile);
        return NULL;
    }

    f = fdopen(FileHandle, "r");           /* Make it into a stream: */

    GifFile->Private = (VoidPtr) Private;
    Private->FileHandle = FileHandle;
    Private->File = f;
    Private->FileState = FILE_STATE_READ;
    Private->Read     = 0;  /* don't use alternate input method (TVT) */
    GifFile->UserData = 0;  /* TVT */

    /* Lets see if this is a GIF file: */
    if (READ(GifFile, (GifByteType *)Buf, GIF_STAMP_LEN) != GIF_STAMP_LEN) {
        _GifError = D_GIF_ERR_READ_FAILED;
        fclose(f);
        free((char *) Private);
        free((char *) GifFile);
        return NULL;
    }

    /* The GIF Version number is ignored at this time. Maybe we should do    */
    /* something more useful with it.                         */
    Buf[GIF_STAMP_LEN] = 0;
    if (strncmp(GIF_STAMP, Buf, GIF_VERSION_POS) != 0) {
        _GifError = D_GIF_ERR_NOT_GIF_FILE;
        fclose(f);
        free((char *) Private);
        free((char *) GifFile);
        return NULL;
    }

    if (DGifGetScreenDesc(GifFile) == GIF_ERROR) {
        fclose(f);
        free((char *) Private);
        free((char *) GifFile);
        return NULL;
    }

    _GifError = 0;

    return GifFile;
}

/******************************************************************************
* GifFileType constructor with user supplied input function (TVT)             *
*                                                                             *
******************************************************************************/
GifFileType *DGifOpen( void* userData, InputFunc readFunc )
{
    char Buf[GIF_STAMP_LEN+1];
    GifFileType *GifFile;
    GifFilePrivateType *Private;


    if ((GifFile = (GifFileType *) malloc(sizeof(GifFileType))) == NULL) {
	  _GifError = D_GIF_ERR_NOT_ENOUGH_MEM;
	  return NULL;
    }

    memset(GifFile, '\0', sizeof(GifFileType));

    if (!(Private = (GifFilePrivateType*) malloc(sizeof(GifFilePrivateType)))){
	  _GifError = D_GIF_ERR_NOT_ENOUGH_MEM;
	  free((char *) GifFile);
	  return NULL;
    }

    GifFile->Private = (VoidPtr) Private;
    Private->FileHandle = 0;
    Private->File = 0;
    Private->FileState = FILE_STATE_READ;

	Private->Read     = readFunc;  /* TVT */
	GifFile->UserData = userData;    /* TVT */

    /* Lets see if this is a GIF file: */
    if ( READ( GifFile, (GifByteType *)Buf, GIF_STAMP_LEN) != GIF_STAMP_LEN) {
	  _GifError = D_GIF_ERR_READ_FAILED;
	  free((char *) Private);
	  free((char *) GifFile);
	  return NULL;
    }

    /* The GIF Version number is ignored at this time. Maybe we should do    */
    /* something more useful with it.					     */
    Buf[GIF_STAMP_LEN] = 0;
    if (strncmp(GIF_STAMP, Buf, GIF_VERSION_POS) != 0) {
	  _GifError = D_GIF_ERR_NOT_GIF_FILE;
	  free((char *) Private);
	  free((char *) GifFile);
	  return NULL;
    }

    if (DGifGetScreenDesc(GifFile) == GIF_ERROR) {
	  free((char *) Private);
	  free((char *) GifFile);
	  return NULL;
    }

    _GifError = 0;

    return GifFile;
}

/******************************************************************************
*   This routine should be called before any other DGif calls. Note that      *
* this routine is called automatically from DGif file open routines.	      *
******************************************************************************/
int DGifGetScreenDesc(GifFileType *GifFile)
{
    int i, BitsPerPixel;
    GifByteType Buf[3];
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    /* Put the screen descriptor into the file: */
    if (DGifGetWord(GifFile, &GifFile->SWidth) == GIF_ERROR ||
	DGifGetWord(GifFile, &GifFile->SHeight) == GIF_ERROR)
	return GIF_ERROR;

    if (READ( GifFile, Buf, 3) != 3) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }
    GifFile->SColorResolution = (((Buf[0] & 0x70) + 1) >> 4) + 1;
    BitsPerPixel = (Buf[0] & 0x07) + 1;
    GifFile->SBackGroundColor = Buf[1];
    if (Buf[0] & 0x80) {		     /* Do we have global color map? */

	GifFile->SColorMap = MakeMapObject(1 << BitsPerPixel, NULL);

	/* Get the global color map: */
	for (i = 0; i < GifFile->SColorMap->ColorCount; i++) {
	    if (READ(GifFile, Buf, 3) != 3) {
		_GifError = D_GIF_ERR_READ_FAILED;
		return GIF_ERROR;
	    }
	    GifFile->SColorMap->Colors[i].Red = Buf[0];
	    GifFile->SColorMap->Colors[i].Green = Buf[1];
	    GifFile->SColorMap->Colors[i].Blue = Buf[2];
	}
    }

    return GIF_OK;
}

/******************************************************************************
*   This routine should be called before any attemp to read an image.         *
******************************************************************************/
int DGifGetRecordType(GifFileType *GifFile, GifRecordType *Type)
{
    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    if (READ( GifFile, &Buf, 1) != 1) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }

    switch (Buf) {
	case ',':
	    *Type = IMAGE_DESC_RECORD_TYPE;
	    break;
	case '!':
	    *Type = EXTENSION_RECORD_TYPE;
	    break;
	case ';':
	    *Type = TERMINATE_RECORD_TYPE;
	    break;
	default:
	    *Type = UNDEFINED_RECORD_TYPE;
	    _GifError = D_GIF_ERR_WRONG_RECORD;
	    return GIF_ERROR;
    }

    return GIF_OK;
}

/******************************************************************************
*   This routine should be called before any attemp to read an image.         *
*   Note it is assumed the Image desc. header (',') has been read.	      *
******************************************************************************/
int DGifGetImageDesc(GifFileType *GifFile)
{
    int i, BitsPerPixel;
    GifByteType Buf[3];
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;
	SavedImage	*sp;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    if (DGifGetWord(GifFile, &GifFile->Image.Left) == GIF_ERROR ||
	DGifGetWord(GifFile, &GifFile->Image.Top) == GIF_ERROR ||
	DGifGetWord(GifFile, &GifFile->Image.Width) == GIF_ERROR ||
	DGifGetWord(GifFile, &GifFile->Image.Height) == GIF_ERROR)
	return GIF_ERROR;
    if (READ(GifFile,Buf, 1) != 1) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }
    BitsPerPixel = (Buf[0] & 0x07) + 1;
    GifFile->Image.Interlace = (Buf[0] & 0x40);
    if (Buf[0] & 0x80) {	    /* Does this image have local color map? */

	if (GifFile->Image.ColorMap && GifFile->SavedImages == NULL)
	    FreeMapObject(GifFile->Image.ColorMap);

	GifFile->Image.ColorMap = MakeMapObject(1 << BitsPerPixel, NULL);
    
	/* Get the image local color map: */
	for (i = 0; i < GifFile->Image.ColorMap->ColorCount; i++) {
	    if (READ(GifFile,Buf, 3) != 3) {
		_GifError = D_GIF_ERR_READ_FAILED;
		return GIF_ERROR;
	    }
	    GifFile->Image.ColorMap->Colors[i].Red = Buf[0];
	    GifFile->Image.ColorMap->Colors[i].Green = Buf[1];
	    GifFile->Image.ColorMap->Colors[i].Blue = Buf[2];
	}
    }

    if (GifFile->SavedImages) {
	    if ((GifFile->SavedImages = (SavedImage *)realloc(GifFile->SavedImages,
		     sizeof(SavedImage) * (GifFile->ImageCount + 1))) == NULL) {
	        _GifError = D_GIF_ERR_NOT_ENOUGH_MEM;
	        return GIF_ERROR;
	    }
    } else {
        if ((GifFile->SavedImages =
             (SavedImage *)malloc(sizeof(SavedImage))) == NULL) {
            _GifError = D_GIF_ERR_NOT_ENOUGH_MEM;
            return GIF_ERROR;
        }
    }

	sp = &GifFile->SavedImages[GifFile->ImageCount];
	memcpy(&sp->ImageDesc, &GifFile->Image, sizeof(GifImageDesc));
    if (GifFile->Image.ColorMap != NULL) {
	    sp->ImageDesc.ColorMap =
               (ColorMapObject *)malloc(sizeof (ColorMapObject));
	    memcpy(&sp->ImageDesc.ColorMap, &GifFile->Image.ColorMap,
               sizeof(ColorMapObject));
	    sp->ImageDesc.ColorMap->Colors =
               (GifColorType *)malloc(sizeof (GifColorType));
	    memcpy(&sp->ImageDesc.ColorMap->Colors,
               &GifFile->Image.ColorMap->Colors, sizeof(GifColorType));
    }
	sp->RasterBits = NULL;
	sp->ExtensionBlockCount = 0;
	sp->ExtensionBlocks = (ExtensionBlock *)NULL;

    GifFile->ImageCount++;

    Private->PixelCount = (long) GifFile->Image.Width *
			    (long) GifFile->Image.Height;

    DGifSetupDecompress(GifFile);  /* Reset decompress algorithm parameters. */

    return GIF_OK;
}

/******************************************************************************
*  Get one full scanned line (Line) of length LineLen from GIF file.	      *
******************************************************************************/
int DGifGetLine(GifFileType *GifFile, GifPixelType *Line, int LineLen)
{
    GifByteType *Dummy;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    if (!LineLen) LineLen = GifFile->Image.Width;

//#if defined(__MSDOS__) || defined(__GNUC__)
    //if ((Private->PixelCount -= LineLen) > 0xffff0000UL) {
//#else
    if ((Private->PixelCount -= LineLen) > 0xffff0000) {
//#endif /* __MSDOS__ */
	_GifError = D_GIF_ERR_DATA_TOO_BIG;
	return GIF_ERROR;
    }

    if (DGifDecompressLine(GifFile, Line, LineLen) == GIF_OK) {
	if (Private->PixelCount == 0) {
	    /* We probably would not be called any more, so lets clean 	     */
	    /* everything before we return: need to flush out all rest of    */
	    /* image until empty block (size 0) detected. We use GetCodeNext.*/
	    do if (DGifGetCodeNext(GifFile, &Dummy) == GIF_ERROR)
		return GIF_ERROR;
	    while (Dummy != NULL);
	}
	return GIF_OK;
    }
    else
	return GIF_ERROR;
}

/******************************************************************************
* Put one pixel (Pixel) into GIF file.					      *
******************************************************************************/
int DGifGetPixel(GifFileType *GifFile, GifPixelType Pixel)
{
    GifByteType *Dummy;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

//#if defined(__MSDOS__) || defined(__GNUC__)
    //if (--Private->PixelCount > 0xffff0000UL)
//#else
    if (--Private->PixelCount > 0xffff0000)
//#endif /* __MSDOS__ */
    {
	_GifError = D_GIF_ERR_DATA_TOO_BIG;
	return GIF_ERROR;
    }

    if (DGifDecompressLine(GifFile, &Pixel, 1) == GIF_OK) {
	if (Private->PixelCount == 0) {
	    /* We probably would not be called any more, so lets clean 	     */
	    /* everything before we return: need to flush out all rest of    */
	    /* image until empty block (size 0) detected. We use GetCodeNext.*/
	    do if (DGifGetCodeNext(GifFile, &Dummy) == GIF_ERROR)
		return GIF_ERROR;
	    while (Dummy != NULL);
	}
	return GIF_OK;
    }
    else
	return GIF_ERROR;
}

/******************************************************************************
*   Get an extension block (see GIF manual) from gif file. This routine only  *
* returns the first data block, and DGifGetExtensionNext shouldbe called      *
* after this one until NULL extension is returned.			      *
*   The Extension should NOT be freed by the user (not dynamically allocated).*
*   Note it is assumed the Extension desc. header ('!') has been read.	      *
******************************************************************************/
int DGifGetExtension(GifFileType *GifFile, int *ExtCode,
						    GifByteType **Extension)
{
    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    if (READ(GifFile,&Buf, 1) != 1) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }
    *ExtCode = Buf;

    return DGifGetExtensionNext(GifFile, Extension);
}

/******************************************************************************
*   Get a following extension block (see GIF manual) from gif file. This      *
* routine sould be called until NULL Extension is returned.		      *
*   The Extension should NOT be freed by the user (not dynamically allocated).*
******************************************************************************/
int DGifGetExtensionNext(GifFileType *GifFile, GifByteType **Extension)
{
    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (READ(GifFile,&Buf, 1) != 1) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }
    if (Buf > 0) {
	*Extension = Private->Buf;           /* Use private unused buffer. */
	(*Extension)[0] = Buf;  /* Pascal strings notation (pos. 0 is len.). */
	if (READ(GifFile,&((*Extension)[1]), Buf) != Buf) {
	    _GifError = D_GIF_ERR_READ_FAILED;
	    return GIF_ERROR;
	}
    }
    else
	*Extension = NULL;

    return GIF_OK;
}

/******************************************************************************
*   This routine should be called last, to close the GIF file.		      *
******************************************************************************/
int DGifCloseFile(GifFileType *GifFile)
{
    GifFilePrivateType *Private;
    FILE *File;

    if (GifFile == NULL) return GIF_ERROR;

    Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    File = Private->File;

    if (GifFile->Image.ColorMap)
	FreeMapObject(GifFile->Image.ColorMap);
    if (GifFile->SColorMap)
	FreeMapObject(GifFile->SColorMap);
    if (Private)
	free((char *) Private);
    if (GifFile->SavedImages)
	FreeSavedImages(GifFile);
    free(GifFile);

    if ( File && (fclose(File) != 0)) {
	  _GifError = D_GIF_ERR_CLOSE_FAILED;
	  return GIF_ERROR;
    }
    return GIF_OK;
}

/******************************************************************************
*   Get 2 bytes (word) from the given file:				      *
******************************************************************************/
static int DGifGetWord(GifFileType *GifFile, int *Word)
{
    unsigned char c[2];

    if (READ(GifFile,c, 2) != 2) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }

    *Word = (((unsigned int) c[1]) << 8) + c[0];
    return GIF_OK;
}

/******************************************************************************
*   Get the image code in compressed form.  his routine can be called if the  *
* information needed to be piped out as is. Obviously this is much faster     *
* than decoding and encoding again. This routine should be followed by calls  *
* to DGifGetCodeNext, until NULL block is returned.			      *
*   The block should NOT be freed by the user (not dynamically allocated).    *
******************************************************************************/
int DGifGetCode(GifFileType *GifFile, int *CodeSize, GifByteType **CodeBlock)
{
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    *CodeSize = Private->BitsPerPixel;

    return DGifGetCodeNext(GifFile, CodeBlock);
}

/******************************************************************************
*   Continue to get the image code in compressed form. This routine should be *
* called until NULL block is returned.					      *
*   The block should NOT be freed by the user (not dynamically allocated).    *
******************************************************************************/
int DGifGetCodeNext(GifFileType *GifFile, GifByteType **CodeBlock)
{
    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (READ(GifFile,&Buf, 1) != 1) {
	_GifError = D_GIF_ERR_READ_FAILED;
	return GIF_ERROR;
    }

    if (Buf > 0) {
	*CodeBlock = Private->Buf;	       /* Use private unused buffer. */
	(*CodeBlock)[0] = Buf;  /* Pascal strings notation (pos. 0 is len.). */
	if (READ(GifFile,&((*CodeBlock)[1]), Buf) != Buf) {
	    _GifError = D_GIF_ERR_READ_FAILED;
	    return GIF_ERROR;
	}
    }
    else {
	*CodeBlock = NULL;
	Private->Buf[0] = 0;		   /* Make sure the buffer is empty! */
	Private->PixelCount = 0;   /* And local info. indicate image read. */
    }

    return GIF_OK;
}

/******************************************************************************
*   Setup the LZ decompression for this image:				      *
******************************************************************************/
static int DGifSetupDecompress(GifFileType *GifFile)
{
    int i, BitsPerPixel;
    GifByteType CodeSize;
    unsigned int *Prefix;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    READ(GifFile,&CodeSize, 1);    /* Read Code size from file. */
    BitsPerPixel = CodeSize;

    Private->Buf[0] = 0;			      /* Input Buffer empty. */
    Private->BitsPerPixel = BitsPerPixel;
    Private->ClearCode = (1 << BitsPerPixel);
    Private->EOFCode = Private->ClearCode + 1;
    Private->RunningCode = Private->EOFCode + 1;
    Private->RunningBits = BitsPerPixel + 1;	 /* Number of bits per code. */
    Private->MaxCode1 = 1 << Private->RunningBits;     /* Max. code + 1. */
    Private->StackPtr = 0;		    /* No pixels on the pixel stack. */
    Private->LastCode = NO_SUCH_CODE;
    Private->CrntShiftState = 0;	/* No information in CrntShiftDWord. */
    Private->CrntShiftDWord = 0;

    Prefix = Private->Prefix;
    for (i = 0; i <= LZ_MAX_CODE; i++) Prefix[i] = NO_SUCH_CODE;

    return GIF_OK;
}

/******************************************************************************
*   The LZ decompression routine:					      *
*   This version decompress the given gif file into Line of length LineLen.   *
*   This routine can be called few times (one per scan line, for example), in *
* order the complete the whole image.					      *
******************************************************************************/
static int DGifDecompressLine(GifFileType *GifFile, GifPixelType *Line,
								int LineLen)
{
    int i = 0, j, CrntCode, EOFCode, ClearCode, CrntPrefix, LastCode, StackPtr;
    GifByteType *Stack, *Suffix;
    unsigned int *Prefix;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    StackPtr = Private->StackPtr;
    Prefix = Private->Prefix;
    Suffix = Private->Suffix;
    Stack = Private->Stack;
    EOFCode = Private->EOFCode;
    ClearCode = Private->ClearCode;
    LastCode = Private->LastCode;

    if (StackPtr != 0) {
	/* Let pop the stack off before continueing to read the gif file: */
	while (StackPtr != 0 && i < LineLen) Line[i++] = Stack[--StackPtr];
    }

    while (i < LineLen) {			    /* Decode LineLen items. */
	if (DGifDecompressInput(GifFile, &CrntCode) == GIF_ERROR)
    	    return GIF_ERROR;

	if (CrntCode == EOFCode) {
	    /* Note however that usually we will not be here as we will stop */
	    /* decoding as soon as we got all the pixel, or EOF code will    */
	    /* not be read at all, and DGifGetLine/Pixel clean everything.   */
	    if (i != LineLen - 1 || Private->PixelCount != 0) {
		_GifError = D_GIF_ERR_EOF_TOO_SOON;
		return GIF_ERROR;
	    }
	    i++;
	}
	else if (CrntCode == ClearCode) {
	    /* We need to start over again: */
	    for (j = 0; j <= LZ_MAX_CODE; j++) Prefix[j] = NO_SUCH_CODE;
	    Private->RunningCode = Private->EOFCode + 1;
	    Private->RunningBits = Private->BitsPerPixel + 1;
	    Private->MaxCode1 = 1 << Private->RunningBits;
	    LastCode = Private->LastCode = NO_SUCH_CODE;
	}
	else {
	    /* Its regular code - if in pixel range simply add it to output  */
	    /* stream, otherwise trace to codes linked list until the prefix */
	    /* is in pixel range:					     */
	    if (CrntCode < ClearCode) {
		/* This is simple - its pixel scalar, so add it to output:   */
		Line[i++] = CrntCode;
	    }
	    else {
		/* Its a code to needed to be traced: trace the linked list  */
		/* until the prefix is a pixel, while pushing the suffix     */
		/* pixels on our stack. If we done, pop the stack in reverse */
		/* (thats what stack is good for!) order to output.	     */
		if (Prefix[CrntCode] == NO_SUCH_CODE) {
		    /* Only allowed if CrntCode is exactly the running code: */
		    /* In that case CrntCode = XXXCode, CrntCode or the	     */
		    /* prefix code is last code and the suffix char is	     */
		    /* exactly the prefix of last code!			     */
		    if (CrntCode == Private->RunningCode - 2) {
			CrntPrefix = LastCode;
			Suffix[Private->RunningCode - 2] =
			Stack[StackPtr++] = DGifGetPrefixChar(Prefix,
							LastCode, ClearCode);
		    }
		    else {
			_GifError = D_GIF_ERR_IMAGE_DEFECT;
			return GIF_ERROR;
		    }
		}
		else
		    CrntPrefix = CrntCode;

		/* Now (if image is O.K.) we should not get an NO_SUCH_CODE  */
		/* During the trace. As we might loop forever, in case of    */
		/* defective image, we count the number of loops we trace    */
		/* and stop if we got LZ_MAX_CODE. obviously we can not      */
		/* loop more than that.					     */
		j = 0;
		while (j++ <= LZ_MAX_CODE &&
		       CrntPrefix > ClearCode &&
		       CrntPrefix <= LZ_MAX_CODE) {
		    Stack[StackPtr++] = Suffix[CrntPrefix];
		    CrntPrefix = Prefix[CrntPrefix];
		}
		if (j >= LZ_MAX_CODE || CrntPrefix > LZ_MAX_CODE) {
		    _GifError = D_GIF_ERR_IMAGE_DEFECT;
		    return GIF_ERROR;
		}
		/* Push the last character on stack: */
		Stack[StackPtr++] = CrntPrefix;

		/* Now lets pop all the stack into output: */
		while (StackPtr != 0 && i < LineLen)
		    Line[i++] = Stack[--StackPtr];
	    }
	    if (LastCode != NO_SUCH_CODE) {
		Prefix[Private->RunningCode - 2] = LastCode;

		if (CrntCode == Private->RunningCode - 2) {
		    /* Only allowed if CrntCode is exactly the running code: */
		    /* In that case CrntCode = XXXCode, CrntCode or the	     */
		    /* prefix code is last code and the suffix char is	     */
		    /* exactly the prefix of last code!			     */
		    Suffix[Private->RunningCode - 2] =
			DGifGetPrefixChar(Prefix, LastCode, ClearCode);
		}
		else {
		    Suffix[Private->RunningCode - 2] =
			DGifGetPrefixChar(Prefix, CrntCode, ClearCode);
		}
	    }
	    LastCode = CrntCode;
	}
    }

    Private->LastCode = LastCode;
    Private->StackPtr = StackPtr;

    return GIF_OK;
}

/******************************************************************************
* Routine to trace the Prefixes linked list until we get a prefix which is    *
* not code, but a pixel value (less than ClearCode). Returns that pixel value.*
* If image is defective, we might loop here forever, so we limit the loops to *
* the maximum possible if image O.k. - LZ_MAX_CODE times.		      *
******************************************************************************/
static int DGifGetPrefixChar(unsigned int *Prefix, int Code, int ClearCode)
{
    int i = 0;

    while (Code > ClearCode && i++ <= LZ_MAX_CODE) Code = Prefix[Code];
    return Code;
}

/******************************************************************************
*   Interface for accessing the LZ codes directly. Set Code to the real code  *
* (12bits), or to -1 if EOF code is returned.				      *
******************************************************************************/
int DGifGetLZCodes(GifFileType *GifFile, int *Code)
{
    GifByteType *CodeBlock;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_READABLE(Private)) {
	/* This file was NOT open for reading: */
	_GifError = D_GIF_ERR_NOT_READABLE;
	return GIF_ERROR;
    }

    if (DGifDecompressInput(GifFile, Code) == GIF_ERROR)
	return GIF_ERROR;

    if (*Code == Private->EOFCode) {
	/* Skip rest of codes (hopefully only NULL terminating block): */
	do if (DGifGetCodeNext(GifFile, &CodeBlock) == GIF_ERROR)
    	    return GIF_ERROR;
	while (CodeBlock != NULL);

	*Code = -1;
    }
    else if (*Code == Private->ClearCode) {
	/* We need to start over again: */
	Private->RunningCode = Private->EOFCode + 1;
	Private->RunningBits = Private->BitsPerPixel + 1;
	Private->MaxCode1 = 1 << Private->RunningBits;
    }

    return GIF_OK;
}

/******************************************************************************
*   The LZ decompression input routine:					      *
*   This routine is responsable for the decompression of the bit stream from  *
* 8 bits (bytes) packets, into the real codes.				      *
*   Returns GIF_OK if read succesfully.					      *
******************************************************************************/
static int DGifDecompressInput(GifFileType *GifFile, int *Code)
{
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    GifByteType NextByte;
    static unsigned int CodeMasks[] = {
	0x0000, 0x0001, 0x0003, 0x0007,
	0x000f, 0x001f, 0x003f, 0x007f,
	0x00ff, 0x01ff, 0x03ff, 0x07ff,
	0x0fff
    };

    while (Private->CrntShiftState < Private->RunningBits) {
	/* Needs to get more bytes from input stream for next code: */
	if (DGifBufferedInput(GifFile, Private->Buf, &NextByte)
	    == GIF_ERROR) {
	    return GIF_ERROR;
	}
	Private->CrntShiftDWord |=
		((unsigned long) NextByte) << Private->CrntShiftState;
	Private->CrntShiftState += 8;
    }
    *Code = Private->CrntShiftDWord & CodeMasks[Private->RunningBits];

    Private->CrntShiftDWord >>= Private->RunningBits;
    Private->CrntShiftState -= Private->RunningBits;

    /* If code cannt fit into RunningBits bits, must raise its size. Note */
    /* however that codes above 4095 are used for special signaling.      */
    if (++Private->RunningCode > Private->MaxCode1 &&
	Private->RunningBits < LZ_BITS) {
	Private->MaxCode1 <<= 1;
	Private->RunningBits++;
    }
    return GIF_OK;
}

/******************************************************************************
*   This routines read one gif data block at a time and buffers it internally *
* so that the decompression routine could access it.			      *
*   The routine returns the next byte from its internal buffer (or read next  *
* block in if buffer empty) and returns GIF_OK if succesful.		      *
******************************************************************************/
static int DGifBufferedInput(GifFileType *GifFile, GifByteType *Buf,
						      GifByteType *NextByte)
{
    if (Buf[0] == 0) {
	/* Needs to read the next buffer - this one is empty: */
	if (READ(GifFile, Buf, 1) != 1)
	{
	    _GifError = D_GIF_ERR_READ_FAILED;
	    return GIF_ERROR;
	}
	if (READ(GifFile,&Buf[1], Buf[0]) != Buf[0])
	{
	    _GifError = D_GIF_ERR_READ_FAILED;
	    return GIF_ERROR;
	}
	*NextByte = Buf[1];
	Buf[1] = 2;	   /* We use now the second place as last char read! */
	Buf[0]--;
    }
    else {
	*NextByte = Buf[Buf[1]++];
	Buf[0]--;
    }

    return GIF_OK;
}

/******************************************************************************
* This routine reads an entire GIF into core, hanging all its state info off  *
* the GifFileType pointer.  Call DGifOpenFileName() or DGifOpenFileHandle()   *
* first to initialize I/O.  Its inverse is EGifSpew().			      *
* 
 ******************************************************************************/
int DGifSlurp(GifFileType *GifFile)
{
    int ImageSize;
    GifRecordType RecordType;
    SavedImage *sp;
    GifByteType *ExtData;
    SavedImage temp_save;

    temp_save.ExtensionBlocks=NULL;
    temp_save.ExtensionBlockCount=0;
  
    do {
	if (DGifGetRecordType(GifFile, &RecordType) == GIF_ERROR)
	    return(GIF_ERROR);

	switch (RecordType) {
	    case IMAGE_DESC_RECORD_TYPE:
		if (DGifGetImageDesc(GifFile) == GIF_ERROR)
		    return(GIF_ERROR);

		sp = &GifFile->SavedImages[GifFile->ImageCount-1];
		ImageSize = sp->ImageDesc.Width * sp->ImageDesc.Height;

		sp->RasterBits
		    //= (char *) malloc(ImageSize * sizeof(GifPixelType));
		    = (GifPixelType*) malloc(ImageSize * sizeof(GifPixelType));

		if (DGifGetLine(GifFile, (GifPixelType *)(sp->RasterBits), ImageSize)
		    == GIF_ERROR)
		    return(GIF_ERROR);

        if (temp_save.ExtensionBlocks) {
            sp->ExtensionBlocks = temp_save.ExtensionBlocks;
            sp->ExtensionBlockCount = temp_save.ExtensionBlockCount;

            temp_save.ExtensionBlocks = NULL;
            temp_save.ExtensionBlockCount=0;

        /* FIXME: The following is wrong.  It is left in only for backwards
         * compatibility.  Someday it should go away.  Use the
         * sp->ExtensionBlocks->Function variable instead.
         */
            sp->Function = sp->ExtensionBlocks[0].Function;

        }

		break;

	    case EXTENSION_RECORD_TYPE:
		if (DGifGetExtension(GifFile,&temp_save.Function,&ExtData)==GIF_ERROR)
		    return(GIF_ERROR);
		while (ExtData != NULL) {
            
            /* Create an extension block with our data */
            if (AddExtensionBlock(&temp_save, ExtData[0], (char *)&ExtData[1])
               == GIF_ERROR)
                return (GIF_ERROR); 
            
		    if (DGifGetExtensionNext(GifFile, &ExtData) == GIF_ERROR)
			    return(GIF_ERROR);
            temp_save.Function = 0;
		}
		break;

	    case TERMINATE_RECORD_TYPE:
		break;

	    default:	/* Should be trapped by DGifGetRecordType */
		break;
	}
    } while (RecordType != TERMINATE_RECORD_TYPE);

    /* Just in case the Gif has an extension block without an associated
     * image... (Should we save this into a savefile structure with no image
     * instead?  Have to check if the present writing code can handle that as
     * well....
     */
    if (temp_save.ExtensionBlocks)
        FreeExtension(&temp_save);
    
    return(GIF_OK);
}



/******************************************************************************
* Miscellaneous utility functions					      *
******************************************************************************/

#define MAX(x, y)	(((x) > (y)) ? (x) : (y))

int BitSize(int n)
/* return smallest bitfield size n will fit in */
{
    int	i;

    for (i = 1; i <= 8; i++)
	if ((1 << i) >= n)
	    break;
    return(i);
}


/******************************************************************************
* Color map object functions						      *
******************************************************************************/

ColorMapObject *MakeMapObject(int ColorCount, GifColorType *ColorMap)
/*
 * Allocate a color map of given size; initialize with contents of
 * ColorMap if that pointer is non-NULL.
 */
{
    ColorMapObject *Object;

    if (ColorCount != (1 << BitSize(ColorCount)))
	return((ColorMapObject *)NULL);

    Object = (ColorMapObject *)malloc(sizeof(ColorMapObject));
    if (Object == (ColorMapObject *)NULL)
	return((ColorMapObject *)NULL);

    Object->Colors = (GifColorType *)calloc(ColorCount, sizeof(GifColorType));
    if (Object->Colors == (GifColorType *)NULL)
	return((ColorMapObject *)NULL);

    Object->ColorCount = ColorCount;
    Object->BitsPerPixel = BitSize(ColorCount);

    if (ColorMap)
	memcpy((char *)Object->Colors,
	       (char *)ColorMap, ColorCount * sizeof(GifColorType));

    return(Object);
}

void FreeMapObject(ColorMapObject *Object)
/*
 * Free a color map object
 */
{
    free(Object->Colors);
    free(Object);
}

#ifdef DEBUG
void DumpColorMap(ColorMapObject *Object, FILE *fp)
{
    if (Object)
    {
	int i, j, Len = Object->ColorCount;

	for (i = 0; i < Len; i+=4) {
	    for (j = 0; j < 4 && j < Len; j++) {
		fprintf(fp,
			"%3d: %02x %02x %02x   ", i + j,
		       Object->Colors[i + j].Red,
		       Object->Colors[i + j].Green,
		       Object->Colors[i + j].Blue);
	    }
	    fprintf(fp, "\n");
	}
    }
}
#endif /* DEBUG */

ColorMapObject *UnionColorMap(
			 ColorMapObject *ColorIn1,
			 ColorMapObject *ColorIn2,
			 GifPixelType ColorTransIn2[])
/*
 * Compute the union of two given color maps and return it.  If result can't 
 * fit into 256 colors, NULL is returned, the allocated union otherwise.
 * ColorIn1 is copied as is to ColorUnion, while colors from ColorIn2 are
 * copied iff they didn't exist before.  ColorTransIn2 maps the old
 * ColorIn2 into ColorUnion color map table.
 */
{
    int i, j, CrntSlot, RoundUpTo, NewBitSize;
    ColorMapObject *ColorUnion;

    /*
     * Allocate table which will hold the result for sure.
     */
    ColorUnion
	= MakeMapObject(MAX(ColorIn1->ColorCount,ColorIn2->ColorCount)*2,NULL);

    if (ColorUnion == NULL)
	return(NULL);

    /* Copy ColorIn1 to ColorUnionSize; */
    for (i = 0; i < ColorIn1->ColorCount; i++)
	ColorUnion->Colors[i] = ColorIn1->Colors[i];
    CrntSlot = ColorIn1->ColorCount;

    /*
     * Potentially obnoxious hack:
     *
     * Back CrntSlot down past all contiguous {0, 0, 0} slots at the end
     * of table 1.  This is very useful if your display is limited to
     * 16 colors.
     */
    while (ColorIn1->Colors[CrntSlot-1].Red == 0
	   && ColorIn1->Colors[CrntSlot-1].Green == 0
	   && ColorIn1->Colors[CrntSlot-1].Red == 0)
	CrntSlot--;

    /* Copy ColorIn2 to ColorUnionSize (use old colors if they exist): */
    for (i = 0; i < ColorIn2->ColorCount && CrntSlot<=256; i++)
    {
	/* Let's see if this color already exists: */
	for (j = 0; j < ColorIn1->ColorCount; j++)
	    if (memcmp(&ColorIn1->Colors[j], &ColorIn2->Colors[i], sizeof(GifColorType)) == 0)
		break;

	if (j < ColorIn1->ColorCount)
	    ColorTransIn2[i] = j;	/* color exists in Color1 */
	else
	{
	    /* Color is new - copy it to a new slot: */
	    ColorUnion->Colors[CrntSlot] = ColorIn2->Colors[i];
	    ColorTransIn2[i] = CrntSlot++;
	}
    }

    if (CrntSlot > 256)
    {
	FreeMapObject(ColorUnion);
	return((ColorMapObject *)NULL);
    }

    NewBitSize = BitSize(CrntSlot);
    RoundUpTo = (1 << NewBitSize);

    if (RoundUpTo != ColorUnion->ColorCount)
    {
	register GifColorType	*Map = ColorUnion->Colors;

	/*
	 * Zero out slots up to next power of 2.
	 * We know these slots exist because of the way ColorUnion's
	 * start dimension was computed.
	 */
	for (j = CrntSlot; j < RoundUpTo; j++)
	    Map[j].Red = Map[j].Green = Map[j].Blue = 0;

	/* perhaps we can shrink the map? */
	if (RoundUpTo < ColorUnion->ColorCount)
	    ColorUnion->Colors 
		= (GifColorType *)realloc(Map, sizeof(GifColorType)*RoundUpTo);
    }

    ColorUnion->ColorCount = RoundUpTo;
    ColorUnion->BitsPerPixel = NewBitSize;

    return(ColorUnion);
}

void ApplyTranslation(SavedImage *Image, GifPixelType Translation[])
/*
 * Apply a given color translation to the raster bits of an image
 */
{
    register int i;
    register int RasterSize = Image->ImageDesc.Height * Image->ImageDesc.Width;

    for (i = 0; i < RasterSize; i++)
	Image->RasterBits[i] = Translation[(int)(Image->RasterBits[i])];
}

/******************************************************************************
* Extension record functions						      *
******************************************************************************/

void MakeExtension(SavedImage *New, int Function)
{
    New->Function = Function;
    /*
     * Someday we might have to deal with multiple extensions.
     */
}

int AddExtensionBlock(SavedImage *New, int Len, char ExtData[])
{
    ExtensionBlock	*ep;

    if (New->ExtensionBlocks == NULL)
	New->ExtensionBlocks = (ExtensionBlock *)malloc(sizeof(ExtensionBlock));
    else
	New->ExtensionBlocks =
	    (ExtensionBlock *)realloc(New->ExtensionBlocks,
		      sizeof(ExtensionBlock) * (New->ExtensionBlockCount + 1));

    if (New->ExtensionBlocks == NULL)
	return(GIF_ERROR);

    ep = &New->ExtensionBlocks[New->ExtensionBlockCount++];

    if ((ep->Bytes = (char *)malloc(ep->ByteCount = Len)) == NULL)
	return(GIF_ERROR);

    if (ExtData) {
	    memcpy(ep->Bytes, ExtData, Len);
        ep->Function = New->Function;
    }

    return(GIF_OK);
}

void FreeExtension(SavedImage *Image)
{
    ExtensionBlock	*ep;

    for (ep = Image->ExtensionBlocks;
	 ep < Image->ExtensionBlocks + Image->ExtensionBlockCount;
	 ep++)
	(void) free((char *)ep->Bytes);
    free((char *)Image->ExtensionBlocks);
    Image->ExtensionBlocks = NULL;
}

/******************************************************************************
* Image block allocation functions					      *
******************************************************************************/
SavedImage *MakeSavedImage(GifFileType *GifFile, SavedImage *CopyFrom)
/*
 * Append an image block to the SavedImages array  
 */
{
    SavedImage	*sp;

    if (GifFile->SavedImages == NULL)
	GifFile->SavedImages = (SavedImage *)malloc(sizeof(SavedImage));
    else
	GifFile->SavedImages = (SavedImage *)realloc(GifFile->SavedImages,
				sizeof(SavedImage) * (GifFile->ImageCount+1));

    if (GifFile->SavedImages == NULL)
	return((SavedImage *)NULL);
    else
    {
	sp = &GifFile->SavedImages[GifFile->ImageCount++];
	memset((char *)sp, '\0', sizeof(SavedImage));

	if (CopyFrom)
	{
	    memcpy((char *)sp, CopyFrom, sizeof(SavedImage));

	    /*
	     * Make our own allocated copies of the heap fields in the
	     * copied record.  This guards against potential aliasing
	     * problems.
	     */

	    /* first, the local color map */
	    if (sp->ImageDesc.ColorMap)
		sp->ImageDesc.ColorMap =
		    MakeMapObject(CopyFrom->ImageDesc.ColorMap->ColorCount,
				  CopyFrom->ImageDesc.ColorMap->Colors);

	    /* next, the raster */
	    sp->RasterBits = (GifByteType *)malloc(sizeof(GifPixelType)
				* CopyFrom->ImageDesc.Height
				* CopyFrom->ImageDesc.Width);
	    memcpy(sp->RasterBits,
		   CopyFrom->RasterBits,
		   sizeof(GifPixelType)
			* CopyFrom->ImageDesc.Height
			* CopyFrom->ImageDesc.Width);

	    /* finally, the extension blocks */
	    if (sp->ExtensionBlocks)
	    {
		sp->ExtensionBlocks
		    = (ExtensionBlock*)malloc(sizeof(ExtensionBlock)
					      * CopyFrom->ExtensionBlockCount);
		memcpy(sp->ExtensionBlocks,
		   CopyFrom->ExtensionBlocks,
		   sizeof(ExtensionBlock)
		   	* CopyFrom->ExtensionBlockCount);

		/*
		 * For the moment, the actual blocks can take their
		 * chances with free().  We'll fix this later. 
		 */
	    }
	}

	return(sp);
    }
}

void FreeSavedImages(GifFileType *GifFile)
{
    SavedImage	*sp;

    for (sp = GifFile->SavedImages;
	 sp < GifFile->SavedImages + GifFile->ImageCount;
	 sp++)
    {
	if (sp->ImageDesc.ColorMap)
	    FreeMapObject(sp->ImageDesc.ColorMap);

	if (sp->RasterBits)
	    free((char *)sp->RasterBits);

	if (sp->ExtensionBlocks)
	    FreeExtension(sp);
    }
    free((char *) GifFile->SavedImages);
}



int _GifError = 0;


/*****************************************************************************
* Return the last GIF error (0 if none) and reset the error.		     *
*****************************************************************************/
int GifLastError(void)
{
    int i = _GifError;

    _GifError = 0;

    return i;
}

/*****************************************************************************
* Print the last GIF error to stderr.					     *
*****************************************************************************/
void PrintGifError(void)
{
    char *Err;

    switch(_GifError) {
	case E_GIF_ERR_OPEN_FAILED:
	    Err = "Failed to open given file";
	    break;
	case E_GIF_ERR_WRITE_FAILED:
	    Err = "Failed to Write to given file";
	    break;
	case E_GIF_ERR_HAS_SCRN_DSCR:
	    Err = "Screen Descriptor already been set";
	    break;
	case E_GIF_ERR_HAS_IMAG_DSCR:
	    Err = "Image Descriptor is still active";
	    break;
	case E_GIF_ERR_NO_COLOR_MAP:
	    Err = "Neither Global Nor Local color map";
	    break;
	case E_GIF_ERR_DATA_TOO_BIG:
	    Err = "#Pixels bigger than Width * Height";
	    break;
	case E_GIF_ERR_NOT_ENOUGH_MEM:
	    Err = "Fail to allocate required memory";
	    break;
	case E_GIF_ERR_DISK_IS_FULL:
	    Err = "Write failed (disk full?)";
	    break;
	case E_GIF_ERR_CLOSE_FAILED:
	    Err = "Failed to close given file";
	    break;
	case E_GIF_ERR_NOT_WRITEABLE:
	    Err = "Given file was not opened for write";
	    break;
	case D_GIF_ERR_OPEN_FAILED:
	    Err = "Failed to open given file";
	    break;
	case D_GIF_ERR_READ_FAILED:
	    Err = "Failed to Read from given file";
	    break;
	case D_GIF_ERR_NOT_GIF_FILE:
	    Err = "Given file is NOT GIF file";
	    break;
	case D_GIF_ERR_NO_SCRN_DSCR:
	    Err = "No Screen Descriptor detected";
	    break;
	case D_GIF_ERR_NO_IMAG_DSCR:
	    Err = "No Image Descriptor detected";
	    break;
	case D_GIF_ERR_NO_COLOR_MAP:
	    Err = "Neither Global Nor Local color map";
	    break;
	case D_GIF_ERR_WRONG_RECORD:
	    Err = "Wrong record type detected";
	    break;
	case D_GIF_ERR_DATA_TOO_BIG:
	    Err = "#Pixels bigger than Width * Height";
	    break;
	case D_GIF_ERR_NOT_ENOUGH_MEM:
	    Err = "Fail to allocate required memory";
	    break;
	case D_GIF_ERR_CLOSE_FAILED:
	    Err = "Failed to close given file";
	    break;
	case D_GIF_ERR_NOT_READABLE:
	    Err = "Given file was not opened for read";
	    break;
	case D_GIF_ERR_IMAGE_DEFECT:
	    Err = "Image is defective, decoding aborted";
	    break;
	case D_GIF_ERR_EOF_TOO_SOON:
	    Err = "Image EOF detected, before image complete";
	    break;
	default:
	    Err = NULL;
	    break;
    }
    if (Err != NULL)
	fprintf(stderr, "\nGIF-LIB error: %s.\n", Err);
    else
	fprintf(stderr, "\nGIF-LIB undefined error %d.\n", _GifError);
}


