#define	NTAGS		17	/* number of known tags */
#define	MAXTAGS		256	/* maximum number of tags */
#define	MAXTAGLEN	255	/* maximum length of a tag value */

#define	TAG_TYPE_NUM		0
#define	TAG_ARTIST_NUM		1
#define	TAG_BITRATE_NUM		2
#define	TAG_CODEC_NUM		3
#define	TAG_COMMENT_NUM		4
#define	TAG_CTIME_NUM		5
#define	TAG_DURATION_NUM	6
#define	TAG_FRAMES_NUM		7
#define	TAG_GENRE_NUM		8
#define	TAG_LENGTH_NUM		9
#define	TAG_LOADFROM_NUM	10
#define	TAG_OFFSET_NUM		11
#define	TAG_SAMPLERATE_NUM	12
#define	TAG_SOURCE_NUM		13
#define	TAG_TITLE_NUM		14
#define	TAG_TRACKNR_NUM		15
#define	TAG_YEAR_NUM		16

#define	TAG_TYPE_NAME		"type"
#define	TAG_ARTIST_NAME		"artist"
#define	TAG_BITRATE_NAME	"bitrate"
#define	TAG_CODEC_NAME		"codec"
#define	TAG_COMMENT_NAME	"comment"
#define	TAG_CTIME_NAME		"ctime"
#define	TAG_DURATION_NAME	"duration"
#define	TAG_FRAMES_NAME		"frames"
#define	TAG_GENRE_NAME		"genre"
#define	TAG_LENGTH_NAME		"length"
#define	TAG_LOADFROM_NAME	"loadfrom"
#define	TAG_OFFSET_NAME		"offset"
#define	TAG_SAMPLERATE_NAME	"samplerate"
#define	TAG_SOURCE_NAME		"source"
#define	TAG_TITLE_NAME		"title"
#define	TAG_TRACKNR_NAME	"tracknr"
#define	TAG_YEAR_NAME		"year"

#define	TAG_TYPE_SKIPDB		0
#define	TAG_ARTIST_SKIPDB	0
#define	TAG_BITRATE_SKIPDB	0
#define	TAG_CODEC_SKIPDB	0
#define	TAG_COMMENT_SKIPDB	1
#define	TAG_CTIME_SKIPDB	0
#define	TAG_DURATION_SKIPDB	0
#define	TAG_FRAMES_SKIPDB	1
#define	TAG_GENRE_SKIPDB	0
#define	TAG_LENGTH_SKIPDB	0
#define	TAG_LOADFROM_SKIPDB	1
#define	TAG_OFFSET_SKIPDB	0
#define	TAG_SAMPLERATE_SKIPDB	0
#define	TAG_SOURCE_SKIPDB	0
#define	TAG_TITLE_SKIPDB	0
#define	TAG_TRACKNR_SKIPDB	0
#define	TAG_YEAR_SKIPDB		0

/* tag types */
#define	TAG_TYPE_UNKNOWN	0
#define	TAG_TYPE_PLAYLIST	1
#define	TAG_TYPE_TUNE		2
#define	TAG_TYPE_ILLEGAL	3

/* linked list data structure */
struct fidinfo
{
	unsigned int	fidnumber;
	int		tagtype;
	time_t		sourcemtime;
	char		**tagvalues;
	char		ntagvalues;
	FID		*playlist;
	unsigned int	pllength;

	struct fidinfo	*next;
};

struct tagprop
{
	char		*name;	/* name of tag */
	int		skipdb;	/* whether to skip in database */
};
