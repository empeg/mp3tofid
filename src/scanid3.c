/*
 * get id3 tags from mp3 files using libid3tag.
 * vaguely based on alsaplayer,
 * which in turn is based on the sample mad player
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <id3tag.h>

#include "fids.h"
#include "mp3tofid.h"

void
getid3info(struct fidinfo *fidinfo)
{
	int			i;
	struct id3_file		*id3file;
	struct id3_tag		*id3tag;
	struct id3_frame	*id3frame;
	const id3_ucs4_t	*id3ucs4;
	id3_latin1_t		*latin1;

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


	if ((id3file = id3_file_open(fidinfo->tagvalues[TAG_LOADFROM_NUM],
					ID3_FILE_MODE_READONLY)) == NULL)
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

			if (id3ucs4 && (latin1 = id3_ucs4_latin1duplicate(id3ucs4)))
				fidinfo->tagvalues[info[i].tagidx] = latin1;
		}
	}

	id3_file_close(id3file);
	return;
}
