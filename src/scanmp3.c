/*
 * get mp3 properties from a tune using libmad
 * get id3 properties from a tune using libid3tag
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <mad.h>
#include <id3tag.h>

#include "fids.h"
#include "mp3tofid.h"
#include "prototypes.h"

#define INPUT_BUFFER_SIZE	(65536)

extern struct progopts progopts;

void
scanid3(struct fidinfo *fidinfo, FILE *fp)
{
	int			i;
	struct id3_file		*id3file;
	struct id3_tag		*id3tag;
	struct id3_frame	*id3frame;
	const id3_ucs4_t	*id3ucs4;
	id3_utf8_t		*utf8 = NULL;

	struct
	{
		char		*id;
		int		tagidx;
	} info[] =
	{
		{ID3_FRAME_TITLE,  TAG_TITLE_NUM  },
		{ID3_FRAME_ARTIST, TAG_ARTIST_NUM },
		{ID3_FRAME_ALBUM,  TAG_SOURCE_NUM },
		{ID3_FRAME_YEAR,   TAG_YEAR_NUM   },
		{ID3_FRAME_TRACK,  TAG_TRACKNR_NUM},
		{ID3_FRAME_GENRE,  TAG_GENRE_NUM  },
		{ID3_FRAME_COMMENT,TAG_COMMENT_NUM}
	};


	if ((id3file = id3_file_fdopen(fileno(fp), ID3_FILE_MODE_READONLY)) == NULL)
		return;

	if ((id3tag = id3_file_tag(id3file)) != NULL)
	{
		for (i = 0; i < sizeof(info) / sizeof(info[0]); i++)
		{
			if ((id3frame = id3_tag_findframe(id3tag, info[i].id, 0)) == NULL)
				continue;

			if (strcmp(info[i].id, ID3_FRAME_COMMENT) == 0)
				id3ucs4 = id3_field_getfullstring(&id3frame->fields[3]);
			else
			{
				id3ucs4 = id3_field_getstrings(&id3frame->fields[1], 0);
				if (id3ucs4 && (strcmp(info[i].id, ID3_FRAME_GENRE) == 0))
					id3ucs4 = id3_genre_name(id3ucs4);
			}

			if (id3ucs4 && (utf8 = id3_ucs4_utf8duplicate(id3ucs4)))
				fidinfo->tagvalues[info[i].tagidx] = nullifempty(utf8tointernal((char *)utf8));

			if (utf8)
			{
				free(utf8);
				utf8 = NULL;
			}
		}
	}

	id3_file_close(id3file);
	return;
}

void
scanmp3(struct fidinfo *fidinfo, struct statex *statex)
{
	struct mad_stream	madstream;
	struct mad_header	madheader;
	mad_timer_t		madtimer;
	unsigned char		inputbuffer[INPUT_BUFFER_SIZE];
	int			errorcondition = 0;
	long			bitratechanges = -1;
	unsigned long		framecount = 0;
	unsigned long		bitrate = 0;
	unsigned long		totalbitrate = 0;
	unsigned long		offset = 0;
	unsigned long		trailer = 0;
	FILE			*fp;
	char			tagvalue[256];

	/* open the MP3 file */
	fp = efopen(statex->path, "r");

	/* First the structures used by libmad must be initialized. */
	mad_stream_init(&madstream);
	mad_header_init(&madheader);
	mad_timer_reset(&madtimer);

	for (;;)
	{
		/* The input bucket must be filled if it becomes empty or if
		 * it's the first execution of the loop.
		 */
		if (madstream.buffer == NULL || madstream.error == MAD_ERROR_BUFLEN)
		{
			size_t		readsize, remaining;
			unsigned char	*readstart;

			if (madstream.next_frame != NULL)
			{
				remaining = madstream.bufend - madstream.next_frame;
				memmove(inputbuffer, madstream.next_frame, remaining);
				readstart = inputbuffer + remaining;
				readsize = INPUT_BUFFER_SIZE - remaining;
			}
			else
			{
				readsize = INPUT_BUFFER_SIZE;
				readstart = inputbuffer;
				remaining = 0;
			}
			
			readsize = fread(readstart, 1, readsize, fp);
			if (readsize <= 0)
			{
				if (ferror(fp))
				{
					fprintf(stderr, "read error on '%s': %s\n",
						statex->path, strerror(errno));
					errorcondition = 1;
				}
				break;
			}

			mad_stream_buffer(&madstream, inputbuffer, readsize + remaining);
			madstream.error = MAD_ERROR_NONE;
		}

		if (mad_header_decode(&madheader, &madstream))
		{
			if (MAD_RECOVERABLE(madstream.error))
				continue;
			else
			{
				if (madstream.error == MAD_ERROR_BUFLEN)
					continue;
				else
				{
					fprintf(stderr,
						"'%s': unrecoverable frame level error (%s).\n",
						statex->path, mad_stream_errorstr(&madstream));
					errorcondition = 1;
					break;
				}
			}
		}

		if (framecount == 0)
			offset = madstream.this_frame - madstream.buffer;
		if (madheader.bitrate != bitrate)
			bitratechanges++;
		bitrate = madheader.bitrate;
		totalbitrate += bitrate / 1000;
		mad_timer_add(&madtimer, madheader.duration);
		framecount++;
	}

	/* add tags if no error occurred */
	if (!errorcondition && framecount)
	{
		/* codec */
		fidinfo->tagvalues[TAG_CODEC_NUM] = "mp3";

		/* bitrate */
		sprintf(tagvalue, "%s%s%lu",
				bitratechanges ? "v" : "f",
				madheader.mode == MAD_MODE_SINGLE_CHANNEL ? "m" : "s",
				totalbitrate / framecount);
		fidinfo->tagvalues[TAG_BITRATE_NUM] = strdup(tagvalue);

		/* duration */
		sprintf(tagvalue, "%lu",
				mad_timer_count(madtimer, MAD_UNITS_MILLISECONDS));
		fidinfo->tagvalues[TAG_DURATION_NUM] = strdup(tagvalue);

		/* offset */
		sprintf(tagvalue, "%lu", offset);
		fidinfo->tagvalues[TAG_OFFSET_NUM] = strdup(tagvalue);

		/* trailer */
		trailer = madstream.bufend - madstream.this_frame;
		sprintf(tagvalue, "%lu", trailer);
		fidinfo->tagvalues[TAG_TRAILER_NUM] = strdup(tagvalue);

		/* samplerate */
		sprintf(tagvalue, "%u", madheader.samplerate);
		fidinfo->tagvalues[TAG_SAMPLERATE_NUM] = strdup(tagvalue);

		/* rid */
		fidinfo->tagvalues[TAG_RID_NUM] = calculaterid(fp, offset, statex->size - trailer);
	}
	else
	{
		fprintf(stderr, "%s: %s is not an mp3 file\n", progopts.progname, statex->path);
		fidinfo->scanned=0;
	}

	mad_header_finish(&madheader);
	mad_stream_finish(&madstream);

	/* now get id3 tags */
	scanid3(fidinfo, fp);

	fclose(fp);
}
