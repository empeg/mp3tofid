/*
 * get mp3 properties from a file using libmad
 * inspired by madlld
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <mad.h>

#include "fids.h"
#include "mp3tofid.h"

#define INPUT_BUFFER_SIZE	(5*8192)

const char
*GetAudioMode(struct mad_header *Header)
{
	const char	*Mode;

	/* Convert the audio mode to it's printed representation. */
	switch(Header->mode)
	{
		case MAD_MODE_SINGLE_CHANNEL:
			Mode = "m";
			break;
		case MAD_MODE_DUAL_CHANNEL:
		case MAD_MODE_JOINT_STEREO:
		case MAD_MODE_STEREO:
			Mode = "s";
			break;
		default:
			Mode = "?";
			fprintf(stderr, "unknown audio mode\n");
			break;
	}

	return(Mode);
}

void
getmp3info(struct fidinfo *fidinfo)
{
	struct mad_stream	Stream;
	struct mad_header	Header;
	mad_timer_t		Timer;
	unsigned char		InputBuffer[INPUT_BUFFER_SIZE];
	int			Status = 0;
	long			BitrateChanges = -1;
	unsigned long		FrameCount = 0;
	unsigned long		Bitrate = 0;
	unsigned long		TotalBitrate = 0;
	unsigned long		Offset = 0;
	FILE			*fp;
	char			tagvalue[256];

	/* open the MP3 file */
	if ((fp = fopen(fidinfo->tagvalues[TAG_LOADFROM_NUM], "r")) == NULL)
	{
		fprintf(stderr, "can not open '%s': %s\n",
				fidinfo->tagvalues[TAG_LOADFROM_NUM],
				strerror(errno));
		return;
	}

	/* First the structures used by libmad must be initialized. */
	mad_stream_init(&Stream);
	mad_header_init(&Header);
	mad_timer_reset(&Timer);

	for (;;)
	{
		/* The input bucket must be filled if it becomes empty or if
		 * it's the first execution of the loop.
		 */
		if(Stream.buffer == NULL || Stream.error == MAD_ERROR_BUFLEN)
		{
			size_t		ReadSize, Remaining;
			unsigned char	*ReadStart;

			if(Stream.next_frame != NULL)
			{
				Remaining = Stream.bufend - Stream.next_frame;
				memmove(InputBuffer, Stream.next_frame, Remaining);
				ReadStart = InputBuffer + Remaining;
				ReadSize = INPUT_BUFFER_SIZE - Remaining;
			}
			else
			{
				ReadSize = INPUT_BUFFER_SIZE;
				ReadStart = InputBuffer;
				Remaining = 0;
			}
			
			ReadSize = fread(ReadStart, 1, ReadSize, fp);
			if(ReadSize <= 0)
			{
				if(ferror(fp))
				{
					fprintf(stderr, "read error on '%s': %s\n",
						fidinfo->tagvalues[TAG_LOADFROM_NUM],
						strerror(errno));
					Status = 1;
				}
				break;
			}

			mad_stream_buffer(&Stream, InputBuffer, ReadSize + Remaining);
			Stream.error = MAD_ERROR_NONE;
		}

		if(mad_header_decode(&Header, &Stream))
		{
			if(MAD_RECOVERABLE(Stream.error))
				continue;
			else
			{
				if(Stream.error == MAD_ERROR_BUFLEN)
					continue;
				else
				{
					fprintf(stderr,
						"'%s': unrecoverable frame level error (%s).\n",
						fidinfo->tagvalues[TAG_LOADFROM_NUM],
						mad_stream_errorstr(&Stream));
					Status = 1;
					break;
				}
			}
		}

		if (FrameCount == 0)
			Offset = Stream.this_frame - Stream.buffer;
		if (Header.bitrate != Bitrate)
			BitrateChanges++;
		Bitrate = Header.bitrate;
		TotalBitrate += Bitrate / 1000;
		mad_timer_add(&Timer, Header.duration);
		FrameCount++;
	}

	/* add tags if no error occurred */
	if(!Status)
	{
		/* frames */
		sprintf(tagvalue, "%lu", FrameCount);
		fidinfo->tagvalues[TAG_FRAMES_NUM] = strdup(tagvalue);

		/* bitrate */
		sprintf(tagvalue, "%s%s%lu",
				BitrateChanges ? "v" : "f",
				GetAudioMode(&Header),
				TotalBitrate / FrameCount);
		fidinfo->tagvalues[TAG_BITRATE_NUM] = strdup(tagvalue);

		/* duration */
		sprintf(tagvalue, "%lu",
				mad_timer_count(Timer, MAD_UNITS_MILLISECONDS));
		fidinfo->tagvalues[TAG_DURATION_NUM] = strdup(tagvalue);

		/* offset */
		sprintf(tagvalue, "%lu", Offset);
		fidinfo->tagvalues[TAG_OFFSET_NUM] = strdup(tagvalue);

		/* samplerate */
		sprintf(tagvalue, "%u", Header.samplerate);
		fidinfo->tagvalues[TAG_SAMPLERATE_NUM] = strdup(tagvalue);
	}

	mad_header_finish(&Header);
	mad_stream_finish(&Stream);

	fclose(fp);
}
