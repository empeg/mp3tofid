#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mad.h>

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
writemp3info(FILE *fpinfo, char *path)
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
	FILE			*fpmp3;

	/* open the MP3 file */
	if ((fpmp3 = fopen(path, "r")) == NULL)
	{
		fprintf(stderr, "can not open '%s': %s\n",
				path, strerror(errno));
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
			
			ReadSize = fread(ReadStart, 1, ReadSize, fpmp3);
			if(ReadSize <= 0)
			{
				if(ferror(fpmp3))
				{
					fprintf(stderr, "read error on '%s': %s\n",
						path, strerror(errno));
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
					fprintf(stderr, "'%s': unrecoverable frame level error (%s).\n",
						path, mad_stream_errorstr(&Stream));
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

	/* Accounting report if no error occured. */
	if(!Status)
	{
/*
		fprintf(fpinfo, "frames=%lu\n", FrameCount);
*/
		fprintf(fpinfo, "bitrate=%s%s%lu\n",
					BitrateChanges ? "v" : "f",
					GetAudioMode(&Header),
					TotalBitrate / FrameCount);
		fprintf(fpinfo, "duration=%lu\n",
				mad_timer_count(Timer, MAD_UNITS_MILLISECONDS));
		fprintf(fpinfo, "offset=%lu\n", Offset);
		fprintf(fpinfo, "samplerate=%u\n", Header.samplerate);
	}

	mad_header_finish(&Header);
	mad_stream_finish(&Stream);

	fclose(fpmp3);

}
