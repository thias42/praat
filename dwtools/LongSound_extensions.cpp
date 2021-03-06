/* LongSound_extensions.c
 *
 * Copyright (C) 1993-2013, 2015 David Weenink
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This code is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this work. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 djmw 20020627 GPL header
 djmw 20030913 changed 'f' to 'file' as argument in Melder_checkSoundFile
 djmw 20060206 Set errno = 0:  "An application that needs to examine the value
 	of errno to determine the error should set it to 0 before a function call,
 	then inspect it before a subsequent function call."
 djmw 20061213 MelderFile_truncate also works for MacOS X
 djmw 20061212 Header unistd.h for MacOS X added.
 djmw 20070129 Sounds may be multichannel
 djmw 20071030 MelderFile->wpath to  MelderFile->path
*/

#include "LongSound_extensions.h"

#if defined (_WIN32)
#include "winport_on.h"
#include <windows.h>
#include "winport_off.h"
#elif defined(linux)
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#elif defined (macintosh)
#include <unistd.h>
#endif

#include "NUM2.h"
#include <errno.h>

/*
  Precondition: size (my buffer) >= nbuf
*/
static void _LongSound_to_multichannel_buffer (LongSound me, short *buffer, long nbuf, int nchannels, int ichannel, long ibuf) {
	long numberOfReads = (my nx - 1) / nbuf + 1;
	long n_to_read = 0;

	if (ibuf <= numberOfReads) {
		n_to_read = ibuf == numberOfReads ? (my nx - 1) % nbuf + 1 : nbuf;
		long imin = (ibuf - 1) * nbuf + 1;
		LongSound_readAudioToShort (me, my buffer, imin, n_to_read);

		for (long i = 1; i <= n_to_read; i++) {
			buffer[nchannels * (i - 1) + ichannel] = my buffer[i];
		}
	}
	if (ibuf >= numberOfReads) {
		for (long i = n_to_read + 1; i <= nbuf; i++) {
			buffer[nchannels * (i - 1) + ichannel] = 0;
		}
	}
}

void LongSounds_writeToStereoAudioFile16 (LongSound me, LongSound thee, int audioFileType, MelderFile file) {
	try {
		long nbuf = my nmax < thy nmax ? my nmax : thy nmax;
		long nx = my nx > thy nx ? my nx : thy nx;
		long numberOfReads = (nx - 1) / nbuf + 1, numberOfBitsPerSamplePoint = 16;

		if (thy numberOfChannels != my numberOfChannels || my numberOfChannels != 1) {
			Melder_throw (U"LongSounds should be mono.");
		}

		if (my sampleRate != thy sampleRate) {
			Melder_throw (U"Sample rates should be equal.");
		}

		/*
			Allocate a stereo buffer of size (2 * the smallest)!
			WE SUPPOSE THAT SMALL IS LARGE ENOUGH.
			Read the same number of samples from both files, despite
			potential differences in internal buffer size.
		*/

		long nchannels = 2;
		autoNUMvector<short> buffer (1, nchannels * nbuf);

		autoMelderFile f  = MelderFile_create (file);
		MelderFile_writeAudioFileHeader (file, audioFileType, (long) floor (my sampleRate), nx, nchannels, numberOfBitsPerSamplePoint);

		for (long i = 1; i <= numberOfReads; i++) {
			long n_to_write = i == numberOfReads ? (nx - 1) % nbuf + 1 : nbuf;
			_LongSound_to_multichannel_buffer (me, buffer.peek(), nbuf, nchannels, 1, i);
			_LongSound_to_multichannel_buffer (thee, buffer.peek(), nbuf, nchannels, 2, i);
			MelderFile_writeShortToAudio (file, nchannels, Melder_defaultAudioFileEncoding (audioFileType,
                numberOfBitsPerSamplePoint), buffer.peek(), n_to_write);
		}
		MelderFile_writeAudioFileTrailer (file, audioFileType, (long) floor (my sampleRate), nx, nchannels, numberOfBitsPerSamplePoint);
		f.close ();
	} catch (MelderError) {
		Melder_throw (me, U": no stereo audio file created.");
	}
}


/*
	BSD systems provide ftruncate, several others supply chsize, and a few
	may provide a (possibly undocumented) fcntl option F_FREESP. Under MS-DOS,
	you can sometimes use write(fd, "", 0). However, there is no portable
	solution, nor a way to delete blocks at the beginning.
*/
static void MelderFile_truncate (MelderFile me, long size) {
#if defined(_WIN32)

	HANDLE hFile;
	DWORD fdwAccess = GENERIC_READ | GENERIC_WRITE, fPos;
	DWORD fdwShareMode = 0; /* File cannot be shared */
	LPSECURITY_ATTRIBUTES lpsa = nullptr;
	DWORD fdwCreate = OPEN_EXISTING;
	LARGE_INTEGER fileSize;

	MelderFile_close (me);

	hFile = CreateFileW (Melder_peek32toW (my path), fdwAccess, fdwShareMode, lpsa, fdwCreate, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		Melder_throw (U"Can't open file ", me, U".");
	}

	// Set current file pointer to position 'size'

	fileSize.LowPart = size;
	fileSize.HighPart = 0; /* Limit the file size to 2^32 - 2 bytes */
	fPos = SetFilePointer (hFile, fileSize.LowPart, &fileSize.HighPart, FILE_BEGIN);
	if (fPos == 0xFFFFFFFF) {
		Melder_throw (U"Can't set the position at size ", size, U"for file ", me, U".");
	}

	// Limit the file size as the current position of the file pointer.

	SetEndOfFile (hFile);
	CloseHandle (hFile);

#elif defined(linux) || defined(macintosh)

	MelderFile_close (me);
	if (truncate (Melder_peek32to8 (my path), size) == -1) {
		Melder_throw (U"Truncating failed for file ", MelderFile_messageName (me), U" (", Melder_peek8to32 (strerror (errno)), U").");
	}
#else
	Melder_throw (U"Don't know what to do.");
#endif
}

static void writePartToOpenFile16 (LongSound me, int audioFileType, long imin, long n, MelderFile file) {
	long offset = imin;
	long numberOfBuffers = (n - 1) / my nmax + 1, numberOfBitsPerSamplePoint = 16;
	long numberOfSamplesInLastBuffer = (n - 1) % my nmax + 1;
	if (file -> filePointer) {
		for (long ibuffer = 1; ibuffer <= numberOfBuffers; ibuffer ++) {
			long numberOfSamplesToCopy = ibuffer < numberOfBuffers ? my nmax : numberOfSamplesInLastBuffer;
			LongSound_readAudioToShort (me, my buffer, offset, numberOfSamplesToCopy);
			offset += numberOfSamplesToCopy;
			MelderFile_writeShortToAudio (file, my numberOfChannels, Melder_defaultAudioFileEncoding (audioFileType, numberOfBitsPerSamplePoint), my buffer, numberOfSamplesToCopy);
		}
	}

	//  We "have" no samples any longer.
	
	my imin = 1;
	my imax = 0;
}

void LongSounds_appendToExistingSoundFile (OrderedOf<structSampled>* me, MelderFile file) {
	long pre_append_endpos = 0, numberOfBitsPerSamplePoint = 16;
	try {
		if (my size < 1) {
			Melder_throw (U"No Sound or LongSound objects to append.");
		}

		/*
			We have to open with "r+" mode because this will position the stream
			at the beginning of the file. The "a" mode does not allow us to
			seek before the end-of-file.

			For Linux: If the file is already opened (e.g. by a LongSound) object we
			should deny access!
			Under Windows deny access is default?!
		*/

		autofile f = Melder_fopen (file, "r+b");
		file -> filePointer = f; // essential !!
		double sampleRate_d;
		long startOfData;
		int32 numberOfSamples;
		int numberOfChannels, encoding;
		int audioFileType = MelderFile_checkSoundFile (file, &numberOfChannels, &encoding, &sampleRate_d, &startOfData, &numberOfSamples);

		if (audioFileType == 0) {
			Melder_throw (U"Not a sound file.");
		}

		// Check whether all the sample rates and channels match.

		long sampleRate = (long) floor (sampleRate_d);
		for (long i = 1; i <= my size; i ++) {
			bool sampleRatesMatch, numbersOfChannelsMatch;
			Sampled data = my at [i];
			if (data -> classInfo == classSound) {
				Sound sound = (Sound) data;
				sampleRatesMatch = floor (1.0 / sound -> dx + 0.5) == sampleRate;
				numbersOfChannelsMatch = sound -> ny == numberOfChannels;
				numberOfSamples += sound -> nx;
			} else {
				LongSound longSound = (LongSound) data;
				sampleRatesMatch = longSound -> sampleRate == sampleRate;
				numbersOfChannelsMatch = longSound -> numberOfChannels == numberOfChannels;
				numberOfSamples += longSound -> nx;
			}
			if (! sampleRatesMatch) {
				Melder_throw (U"Sample rates do not match.");
			}
			if (! numbersOfChannelsMatch) {
				Melder_throw (U"Cannot mix stereo and mono.");
			}
		}

		// Search the end of the file, count the number of bytes and append.

		MelderFile_seek (file, 0, SEEK_END);

		pre_append_endpos = MelderFile_tell (file);

		errno = 0;
		for (long i = 1; i <= my size; i ++) {
			Sampled data = my at [i];
			if (data -> classInfo == classSound) {
				Sound sound = (Sound) data;
				MelderFile_writeFloatToAudio (file, sound -> ny, Melder_defaultAudioFileEncoding
				    (audioFileType, numberOfBitsPerSamplePoint), sound -> z, sound -> nx, true);
			} else {
				LongSound longSound = (LongSound) data;
				writePartToOpenFile16 (longSound, audioFileType, 1, longSound -> nx, file);
			}
			if (errno != 0) {
				Melder_throw (U"Error during writing.");
			}
		}

		// Update header

		MelderFile_rewind (file);
		MelderFile_writeAudioFileHeader (file, audioFileType, sampleRate, numberOfSamples, numberOfChannels, numberOfBitsPerSamplePoint);
		MelderFile_writeAudioFileTrailer (file, audioFileType, sampleRate, numberOfSamples, numberOfChannels, numberOfBitsPerSamplePoint);
		f.close (file);
		return;
	} catch (MelderError) {
		if (errno != 0 && pre_append_endpos > 0) {
			// Restore file at original size
			int error = errno;
			MelderFile_truncate (file, pre_append_endpos);
			Melder_throw (U"File ", MelderFile_messageName (file), U" restored to original size (", Melder_peek8to32 (strerror (error)), U").");
		} throw;
	}
}

/* End of file LongSound_extensions.cpp */
