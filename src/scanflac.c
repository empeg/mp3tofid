#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <FLAC/metadata.h>

#include "fids.h"
#include "mp3tofid.h"
#include "prototypes.h"

#define	min(a, b)	(a < b ? a : b)

extern struct progopts	progopts;

void
extract_vc_field(struct fidinfo *fidinfo, const FLAC__StreamMetadata_VorbisComment_Entry *entry)
{
	char	vc_field[256];
	int	vc_field_len;

	if (entry->entry != NULL)
	{
		vc_field_len = min(entry->length, 255);
		strncpy(vc_field, (char *)entry->entry, vc_field_len);
		vc_field[vc_field_len] = '\0';
		parsevorbiscomment(fidinfo, utf8tointernal(vc_field));
	}
}


void
extract_metadata(struct fidinfo *fidinfo, FLAC__StreamMetadata *block, size_t size)
{
	unsigned int	i;
	unsigned int	sample_rate;
	unsigned int	channels;
	unsigned int	bitrate;
	unsigned int	duration;
	uint64_t	total_samples;
	char		tagvalue[256];

	switch (block->type)
	{
		case FLAC__METADATA_TYPE_STREAMINFO:
			sample_rate   = block->data.stream_info.sample_rate;
			channels      = block->data.stream_info.channels;
			total_samples = block->data.stream_info.total_samples;
			bitrate       = (unsigned int) (size * 8.0 * sample_rate / total_samples / 1000.0);
			duration      = (unsigned int) (total_samples * 1000 / sample_rate);

			/* codec */
			fidinfo->tagvalues[TAG_CODEC_NUM] = "flac";

			/* bitrate */
			sprintf(tagvalue, "v%s%d", channels == 1 ? "m" : "s", bitrate);
			fidinfo->tagvalues[TAG_BITRATE_NUM] = strdup(tagvalue);

			/* duration */
			sprintf(tagvalue, "%u", duration);
			fidinfo->tagvalues[TAG_DURATION_NUM] = strdup(tagvalue);

			/* offset */
			fidinfo->tagvalues[TAG_OFFSET_NUM] = "0";

			/* trailer */
			fidinfo->tagvalues[TAG_TRAILER_NUM] = "0";

			/* samplerate */
			sprintf(tagvalue, "%u", sample_rate);
			fidinfo->tagvalues[TAG_SAMPLERATE_NUM] = strdup(tagvalue);
			break;

		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			for (i=0; i< block->data.vorbis_comment.num_comments; i++)
				extract_vc_field(fidinfo,
					&block->data.vorbis_comment.comments[i]);
			break;

		case FLAC__METADATA_TYPE_PADDING:
		case FLAC__METADATA_TYPE_APPLICATION:
		case FLAC__METADATA_TYPE_SEEKTABLE:
		case FLAC__METADATA_TYPE_CUESHEET:
		case FLAC__METADATA_TYPE_UNDEFINED:
			break;
	}
}

void
scanflac(struct fidinfo *fidinfo, struct statex *statex)
{
	FLAC__Metadata_Iterator	*iterator;
	FLAC__Metadata_Chain	*chain;
	FLAC__StreamMetadata	*block;
	FLAC__bool		nextok = true;
	FILE			*fp;

	chain = FLAC__metadata_chain_new();

	if (!FLAC__metadata_chain_read(chain, statex->path))
	{
		fprintf(stderr, "%s: error reading flac metadata from %s, status = \"%s\"\n",
			progopts.progname, statex->path,
			FLAC__Metadata_ChainStatusString[FLAC__metadata_chain_status(chain)]);
		return;
	}

	iterator = FLAC__metadata_iterator_new();

	for (FLAC__metadata_iterator_init(iterator, chain);
			nextok && (block  = FLAC__metadata_iterator_get_block(iterator));
			nextok = FLAC__metadata_iterator_next(iterator))
		extract_metadata(fidinfo, block, statex->size);

	FLAC__metadata_iterator_delete(iterator);
	FLAC__metadata_chain_delete(chain);

	/* calculating rid requires opening the file for the second time */
	fp = efopen(statex->path, "r");
	fidinfo->tagvalues[TAG_RID_NUM] = calculaterid(fp, 0, statex->size);
	fclose(fp);
}
