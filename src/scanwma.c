#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"
#include "asf.h"
#include "fids.h"
#include "mp3tofid.h"
#include "prototypes.h"



#define	ASF_LOAD_GUID_PREFIX(guid)	\
	((guid)[3] << 24 | (guid)[2] << 16 | (guid)[1] << 8 | (guid)[0])

#define ASF_GUID_PREFIX_audio_stream			0xf8699e40
#define ASF_GUID_PREFIX_video_stream			0xbc19efc0
#define ASF_GUID_PREFIX_audio_conceal_none		0x49f1a440
#define ASF_GUID_PREFIX_audio_conceal_interleave	0xbfc3cd50
#define ASF_GUID_PREFIX_header				0x75b22630
#define ASF_GUID_PREFIX_data_chunk			0x75b22636
#define ASF_GUID_PREFIX_index_chunk			0x33000890
#define ASF_GUID_PREFIX_stream_header			0xb7dc0791
#define ASF_GUID_PREFIX_header_2_0			0xd6e229d1
#define ASF_GUID_PREFIX_file_header			0x8cabdca1
#define	ASF_GUID_PREFIX_content_desc			0x75b22633
#define	ASF_GUID_PREFIX_stream_group			0x7bf875ce
#define ASF_GUID_PREFIX_clock				0x5fbf03b5
#define ASF_GUID_PREFIX_text_tags			0xd2d0a440
#define ASF_GUID_PREFIX_unknown_text_2			0x86d15240
#define ASF_GUID_PREFIX_unknown_drm_1			0x1efb1a30

/* external globals */
extern struct progopts progopts;


static ASF_header_t			asfh;
static ASF_obj_header_t			objh;
static ASF_file_header_t		fileh;
static ASF_stream_header_t		streamh;
static ASF_content_description_t	contenth;
static ASF_waveformatex_t		wfh;

int
asf_check_header(FILE *fp)
{
	unsigned char	asfhdrguid[16]={0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
					0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};

	fread((char*) &asfh, sizeof(asfh), 1, fp);
	le2me_ASF_header_t(&asfh);

	if (memcmp(asfhdrguid, asfh.objh.guid, 16))
	{
		fprintf(stdout, "ASF_check: not ASF guid!\n");
		return 0;
	}

	if (asfh.cno > 256)
	{
		fprintf(stdout, "ASF_check: invalid subchunks_no %d\n", (int) asfh.cno);
		return 0;
	}
	return 1;
}

void
parsewmatag(struct fidinfo *fidinfo, char *tagname, char *utf16string, size_t utf16len)
{
	if (strcmp(tagname, "WM/AlbumTitle") == 0)
		fidinfo->tagvalues[TAG_SOURCE_NUM] = utf16tointernal(utf16string, utf16len);
	else if (strcmp(tagname, "WM/Genre") == 0)
		fidinfo->tagvalues[TAG_GENRE_NUM] = utf16tointernal(utf16string, utf16len);
	else if (strcmp(tagname, "WM/Track") == 0)
		;
		/* FIXME
		 * can't reliably parse tracknumber
		 * What's the difference between Track and Tracknumber?
		 * and when is the value a binary, and when a string?
		 * emplode doesn't get it right, too, for that matter.
		 * Windows Explorer does get it right so there must be
		 * a way.
		 */
	else if (strcmp(tagname, "WM/TrackNumber") == 0)
		; /* ditto */
	else if (strcmp(tagname, "WM/Year") == 0)
		fidinfo->tagvalues[TAG_YEAR_NUM] = utf16tointernal(utf16string, utf16len);
}

void
extract_tags(struct fidinfo *fidinfo, char *buf, size_t len)
{
	int		i;
	char		*cp;
	uint16_t	num_tags;
	uint16_t	tag_size;
	uint16_t	value_size;
	uint16_t	unknown;
	char		*tagname;

	cp = buf;
	num_tags = *(uint16_t *)cp;
	le2me_16(num_tags);
	cp += sizeof(uint16_t);

	for (i=0; i<num_tags; i++)
	{
		tag_size = *(uint16_t *)cp;
		le2me_16(tag_size);
		cp += sizeof(uint16_t);

		tagname = utf16tointernal(cp, tag_size);
		cp += tag_size;

		unknown = *(uint16_t *)cp;
		cp += sizeof(uint16_t);

		value_size = *(uint16_t *)cp;
		cp += sizeof(uint16_t);

		parsewmatag(fidinfo, tagname, cp, value_size);
		cp += value_size;
	}
}

int
read_asf_header(struct fidinfo *fidinfo, struct statex *statex, FILE *fp)
{
	static unsigned char	buffer[2048];
	char			tagvalue[256];
	int			pos, endpos;
	char			*string = NULL;
	int			bitrate = 0;
	int			channels = 0;
	int			duration;
	int			samplerate;
	char			*text_buf;

	while(!feof(fp))
	{
		pos = ftell(fp);
		fread((char*) &objh, sizeof(objh), 1, fp);
		le2me_ASF_obj_header_t(&objh);
		if (feof(fp))
			break;
		endpos = pos + objh.size;

		switch(ASF_LOAD_GUID_PREFIX(objh.guid))
		{
			case ASF_GUID_PREFIX_file_header:
				/* get bitrate and duration from this header */
				fread((char*) &fileh, sizeof(fileh), 1, fp);
				le2me_ASF_file_header_t(&fileh);
				bitrate = (int)(fileh.max_bitrate / 1000);
				duration = (int)(fileh.play_duration / 10000 - fileh.preroll);
				sprintf(tagvalue, "%d", duration);
				fidinfo->tagvalues[TAG_DURATION_NUM] = strdup(tagvalue);
				break;

			case ASF_GUID_PREFIX_content_desc:
				/* get title, artist and comment from this header */
				fread((char*) &contenth, sizeof(contenth), 1, fp);
				le2me_ASF_content_description_t(&contenth);
				/* title */
				if (contenth.title_size != 0)
				{
					string = (char*)malloc(contenth.title_size);
					fread(string, contenth.title_size, 1, fp);
					if (contenth.title_size)
						fidinfo->tagvalues[TAG_TITLE_NUM] =
							strdup(utf16tointernal(string, contenth.title_size)); 
				}
				/* author */
				if (contenth.author_size != 0)
				{
					string = (char*)realloc((void*)string, contenth.author_size);
					fread(string, contenth.author_size, 1, fp);
					if (contenth.author_size)
						fidinfo->tagvalues[TAG_ARTIST_NUM] =
							strdup(utf16tointernal(string, contenth.author_size)); 
				}
				/* copyright */
				if (contenth.copyright_size != 0)
				{
					string = (char*)realloc((void*)string, contenth.copyright_size);
					fread(string, contenth.copyright_size, 1, fp);
					/* ignore */
				}
				/* comment */
				if (contenth.comment_size != 0)
				{
					string = (char*)realloc((void*)string, contenth.comment_size);
					fread(string, contenth.comment_size, 1, fp);
					if (contenth.comment_size)
						fidinfo->tagvalues[TAG_COMMENT_NUM] =
							strdup(utf16tointernal(string, contenth.comment_size)); 
				}
				/* rating */
				if (contenth.rating_size != 0)
				{
					string = (char*)realloc((void*)string, contenth.rating_size);
					fread(string, contenth.rating_size, 1, fp);
					/* ignore */
				}
				free(string);
				break;

			case ASF_GUID_PREFIX_stream_header:
				/* get number of channels and samplerate from this header */
				fread((char*) &streamh, sizeof(streamh), 1, fp);
				le2me_ASF_stream_header_t(&streamh);

				if (streamh.type_size > 2048 || streamh.stream_size > 2048)
				{
					fprintf(stderr, "%s: ASF header size bigger than 2048 bytes (%d, %d)!\n"
						"Please contact %s authors, and upload/send this file.\n",
						progopts.progname, (int)streamh.type_size,
						(int)streamh.stream_size, progopts.progname);
					return 0;
				}

				fread((char*) buffer, streamh.type_size, 1, fp);
				if (ASF_LOAD_GUID_PREFIX(streamh.type) == ASF_GUID_PREFIX_audio_stream)
				{
					memcpy(&wfh, buffer, sizeof(ASF_waveformatex_t));
					le2me_ASF_waveformatex_t(wfh);
					channels   = (int) wfh.nChannels;
					samplerate = (int) wfh.nSamplesPerSec;
					sprintf(tagvalue, "%d", samplerate);
					fidinfo->tagvalues[TAG_SAMPLERATE_NUM] = strdup(tagvalue);
				}
				break;

			case ASF_GUID_PREFIX_text_tags:
				/* get id3-like tags from this header */
				text_buf = (char*) malloc(objh.size);
				fread((char*) text_buf, objh.size, 1, fp);
				extract_tags(fidinfo, text_buf, objh.size);
				free(text_buf);
				break;

			case ASF_GUID_PREFIX_unknown_drm_1:
				/* if this header exists, then the tune is drm-protected */
				fidinfo->tagvalues[TAG_DRM_NUM] = "msasf";
				break;

		}
		fseek(fp, endpos, SEEK_SET);
	}

	/* codec */
	fidinfo->tagvalues[TAG_CODEC_NUM] = "wma";

	/* offset */
	fidinfo->tagvalues[TAG_OFFSET_NUM] = "0";

	/* trailer */
	fidinfo->tagvalues[TAG_TRAILER_NUM] = "0";

	/* bitrate */
	sprintf(tagvalue, "f%s%d", channels > 1 ? "s" : "m", bitrate);
	fidinfo->tagvalues[TAG_BITRATE_NUM] = strdup(tagvalue);

	/* rid */
	fidinfo->tagvalues[TAG_RID_NUM] = calculaterid(fp, 0, statex->size);

	return 1;
}

void
scanwma(struct fidinfo *fidinfo, struct statex *statex)
{
	FILE	*fp;

	fp = efopen(statex->path, "r");
	if (asf_check_header(fp))
		read_asf_header(fidinfo, statex, fp);
	fclose(fp);
}
