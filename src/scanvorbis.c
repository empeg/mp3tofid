#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <vorbis/vorbisfile.h>

#include "fids.h"
#include "mp3tofid.h"
#include "prototypes.h"

extern struct progopts progopts;

void
parsevorbiscomment(struct fidinfo *fidinfo, char *vc)
{
	char	*cp;

	if ((cp = strchr(vc, '=')) == NULL)
		return;
	*cp++ = '\0';
	
	if (*vc == '\0')
		fidinfo->tagvalues[TAG_COMMENT_NUM] = cp;
	else if (strcasecmp(vc, "album") == 0)
		fidinfo->tagvalues[TAG_SOURCE_NUM]  = cp;
	else if (strcasecmp(vc, "artist") == 0)
		fidinfo->tagvalues[TAG_ARTIST_NUM]  = cp;
	else if (strcasecmp(vc, "date") == 0)
		fidinfo->tagvalues[TAG_YEAR_NUM]    = cp;
	else if (strcasecmp(vc, "description") == 0)
		fidinfo->tagvalues[TAG_COMMENT_NUM] = cp;
	else if (strcasecmp(vc, "genre") == 0)
		fidinfo->tagvalues[TAG_GENRE_NUM]   = cp;
	else if (strcasecmp(vc, "title") == 0)
		fidinfo->tagvalues[TAG_TITLE_NUM]   = cp;
	else if (strcasecmp(vc, "tracknumber") == 0)
		fidinfo->tagvalues[TAG_TRACKNR_NUM] = cp;
}


void
scanvorbis(struct fidinfo *fidinfo, struct statex *statex)
{
	char		**ptr;
	long		bitrate;
	OggVorbis_File	vf;
	vorbis_info	*vi;
	FILE		*fp;
	char		tagvalue[256];

	/* open stream */
	fp = efopen(statex->path, "r");
	if (ov_open(fp, &vf, NULL, 0) < 0)
	{
		fprintf(stderr, "%s: %s is not a Vorbis file\n",
			progopts.progname, statex->path);
		fclose(fp);
		fidinfo->scanned = 0;
		return;
	}

	/* collect information about the stream */
	vi = ov_info(&vf, -1);

	/* codec */
	fidinfo->tagvalues[TAG_CODEC_NUM] = "vorbis";

	/* bitrate */
	if (progopts.oggnominalbr)
		bitrate = vi->bitrate_nominal / 1000; /* nominal bitrate */
	else
		bitrate = ov_bitrate(&vf, -1) / 1000; /* average bitrate */
	sprintf(tagvalue, "v%s%ld", vi->channels == 1 ? "m" : "s", bitrate);
	fidinfo->tagvalues[TAG_BITRATE_NUM] = strdup(tagvalue);

	/* duration */
	sprintf(tagvalue, "%ld", (long) (ov_time_total(&vf, -1) * 1000));
	fidinfo->tagvalues[TAG_DURATION_NUM] = strdup(tagvalue);

	/* offset */
	fidinfo->tagvalues[TAG_OFFSET_NUM] = "0";

	/* trailer */
	fidinfo->tagvalues[TAG_TRAILER_NUM] = "0";

	/* samplerate */
	sprintf(tagvalue, "%ld", vi->rate);
	fidinfo->tagvalues[TAG_SAMPLERATE_NUM] = strdup(tagvalue);

	/* vorbis comments */
	for (ptr = ov_comment(&vf, -1)->user_comments; *ptr; ptr++)
		parsevorbiscomment(fidinfo, utf8tointernal(*ptr));

	/* rid */
	fidinfo->tagvalues[TAG_RID_NUM] = calculaterid(fp, 0, statex->size);

	/* close stream */
	ov_clear(&vf); /* this seems to close fp as well */
}
