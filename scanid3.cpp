#include <stdio.h>
#include <ctype.h>
#include <id3.h>
#include "id3genre.h"

char *
getid3field(ID3Tag *tag, ID3_FrameID frameid)
{
	ID3Frame	*frame;
	ID3Field	*field;
	static char	buf[1024];

	buf[0] = '\0';
	if ((frame = ID3Tag_FindFrameWithID(tag, frameid)) != NULL)
		if ((field = ID3Frame_GetField(frame, ID3FN_TEXT)) != NULL)
			(void) ID3Field_GetASCII(field, buf, 1024);
	return buf;
}

void
writeid3info(FILE *fp, char *path)
{
	char		*tagstring;
	char		*cp;
	int		genrenum = 0xff;
	ID3Tag		*tag;

	if ((tag = ID3Tag_New()) == NULL)
	{
		fprintf(stderr, "ID3Tag_New() failed\n");
		exit (1);
	}

	(void) ID3Tag_Link(tag, path);

	tagstring = getid3field(tag, ID3FID_TITLE);
	if (*tagstring)
		fprintf(fp, "title=%s\n", tagstring);
	tagstring = getid3field(tag, ID3FID_LEADARTIST);
	if (*tagstring)
		fprintf(fp, "artist=%s\n", tagstring);
	tagstring = getid3field(tag, ID3FID_ALBUM);
	if (*tagstring)
		fprintf(fp, "source=%s\n", tagstring);
	tagstring = getid3field(tag, ID3FID_YEAR);
	if (*tagstring)
		fprintf(fp, "year=%s\n", tagstring);
	tagstring = getid3field(tag, ID3FID_COMMENT);
	if (*tagstring)
		fprintf(fp, "comment=%s\n", tagstring);
	tagstring = getid3field(tag, ID3FID_TRACKNUM);
	if (*tagstring)
		fprintf(fp, "track=%s\n", tagstring);
	tagstring = getid3field(tag, ID3FID_CONTENTTYPE);
	if (*tagstring)
	{
		if (tagstring[0] == '(')
		{
			cp = &tagstring[1];
			while (isdigit(*cp))
				cp++;
			if (*cp == ')')
				genrenum = atoi(&tagstring[1]);
			if ((genrenum >= 0) && (genrenum < NID3GENRE))
				tagstring = id3genre[genrenum];
		}
		fprintf(fp, "genre=%s\n", tagstring);
	}
}
