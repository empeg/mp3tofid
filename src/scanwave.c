#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sndfile.h>

#include "fids.h"
#include "mp3tofid.h"
#include "prototypes.h"

extern struct progopts	progopts;

void
scanwave(struct fidinfo *fidinfo, struct statex *statex)
{
	SNDFILE		*sf;
	SF_INFO		si;
	FILE		*fp;
	unsigned int	bitrate;
	char		tagvalue[256];

	fp = efopen(statex->path, "r");
	if ((sf = sf_open_fd(fileno(fp), SFM_READ, &si, 0)) == NULL)
	{
		fprintf(stderr, "%s: sfopen(%s) failed: %s\n",
			progopts.progname, statex->path, sf_strerror(sf));
		fidinfo->scanned=0;
		return;
	}

	/* check it is a wave file */
	if ((si.format & SF_FORMAT_WAV) == 0)
	{

		fprintf(stderr, "%s: %s is not a WAV file\n", progopts.progname, statex->path);
		sf_close(sf);
		fidinfo->scanned=0;
		return;
	}

	/* must be pcm subtype */
	switch (si.format & SF_FORMAT_SUBMASK)
	{
		case SF_FORMAT_PCM_S8:
		case SF_FORMAT_PCM_16:
		case SF_FORMAT_PCM_24:
		case SF_FORMAT_PCM_32:
		case SF_FORMAT_PCM_U8:
			break;
		default:
			fprintf(stderr, "%s: %s is not a PCM WAV file\n", progopts.progname, statex->path);
			sf_close(sf);
			fidinfo->scanned=0;
			return;
	}


	/* codec */
	fidinfo->tagvalues[TAG_CODEC_NUM] = "wave";

	/* bitrate */
	bitrate = (unsigned int) (statex->size * 8.0 * si.samplerate / si.frames / 1000.0);
	sprintf(tagvalue, "f%s%u", si.channels > 1 ? "s" : "m", bitrate);
	fidinfo->tagvalues[TAG_BITRATE_NUM] = strdup(tagvalue);

	/* duration */
	sprintf(tagvalue, "%lu", (unsigned long) (si.frames * 1000 / si.samplerate));
	fidinfo->tagvalues[TAG_DURATION_NUM] = strdup(tagvalue);

	/* offset */
	fidinfo->tagvalues[TAG_OFFSET_NUM] = "0";

	/* samplerate */
	sprintf(tagvalue, "%lu", (unsigned long) si.samplerate);
	fidinfo->tagvalues[TAG_SAMPLERATE_NUM] = strdup(tagvalue);

	/* rid */
	fidinfo->tagvalues[TAG_RID_NUM] = calculaterid(fp, 0, statex->size);

	/* close */
	sf_close(sf);
	fclose(fp);
}

