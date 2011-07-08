#include <regex.h>
#include <iconv.h>

#define	NTAGS		21	/* number of known tags */
#define	MAXTAGS		256	/* maximum number of tags */
#define	MAXTAGLEN	255	/* maximum length of a tag value */
#define	MAXFIDNUM	131072	/* maximum number of fids */
#define	RID_CHUNK	65536	/* for calculaterid() */

#define	TAG_ARTIST_NUM		0
#define	TAG_BITRATE_NUM		1
#define	TAG_CODEC_NUM		2
#define	TAG_COMMENT_NUM		3
#define	TAG_CTIME_NUM		4
#define	TAG_DRM_NUM		5
#define	TAG_DURATION_NUM	6
#define	TAG_FILE_ID_NUM		7
#define	TAG_GENRE_NUM		8
#define	TAG_LENGTH_NUM		9
#define	TAG_LOADFROM_NUM	10
#define	TAG_OFFSET_NUM		11
#define	TAG_OPTIONS_NUM		12
#define	TAG_RID_NUM		13
#define	TAG_SAMPLERATE_NUM	14
#define	TAG_SOURCE_NUM		15
#define	TAG_TITLE_NUM		16
#define	TAG_TRACKNR_NUM		17
#define	TAG_TRAILER_NUM		18
#define	TAG_TYPE_NUM		19
#define	TAG_YEAR_NUM		20

#define	TAG_ARTIST_NAME		"artist"
#define	TAG_BITRATE_NAME	"bitrate"
#define	TAG_CODEC_NAME		"codec"
#define	TAG_COMMENT_NAME	"comment"
#define	TAG_CTIME_NAME		"ctime"
#define	TAG_DRM_NAME		"drm"
#define	TAG_DURATION_NAME	"duration"
#define	TAG_FILE_ID_NAME	"file_id"
#define	TAG_GENRE_NAME		"genre"
#define	TAG_LENGTH_NAME		"length"
#define	TAG_LOADFROM_NAME	"loadfrom"
#define	TAG_OFFSET_NAME		"offset"
#define	TAG_OPTIONS_NAME	"options"
#define	TAG_RID_NAME		"rid"
#define	TAG_SAMPLERATE_NAME	"samplerate"
#define	TAG_SOURCE_NAME		"source"
#define	TAG_TITLE_NAME		"title"
#define	TAG_TRACKNR_NAME	"tracknr"
#define	TAG_TRAILER_NAME	"trailer"
#define	TAG_TYPE_NAME		"type"
#define	TAG_YEAR_NAME		"year"

#define	TAG_ARTIST_SKIPDB	0
#define	TAG_BITRATE_SKIPDB	0
#define	TAG_CODEC_SKIPDB	0
#define	TAG_COMMENT_SKIPDB	1
#define	TAG_CTIME_SKIPDB	0
#define	TAG_DRM_SKIPDB		0
#define	TAG_DURATION_SKIPDB	0
#define	TAG_FILE_ID_SKIPDB	1
#define	TAG_GENRE_SKIPDB	0
#define	TAG_LENGTH_SKIPDB	0
#define	TAG_LOADFROM_SKIPDB	1
#define	TAG_OFFSET_SKIPDB	0
#define	TAG_OPTIONS_SKIPDB	0
#define	TAG_RID_SKIPDB		1
#define	TAG_SAMPLERATE_SKIPDB	0
#define	TAG_SOURCE_SKIPDB	0
#define	TAG_TITLE_SKIPDB	0
#define	TAG_TRACKNR_SKIPDB	1
#define	TAG_TRAILER_SKIPDB	0
#define	TAG_TYPE_SKIPDB		0
#define	TAG_YEAR_SKIPDB		0

/* tag types */
#define	TAG_TYPE_UNKNOWN	0
#define	TAG_TYPE_PLAYLIST	1
#define	TAG_TYPE_TUNE		2
#define	TAG_TYPE_ILLEGAL	3

/* codecs */
#define	CODEC_NONE		0
#define	CODEC_MP3		1
#define	CODEC_WAVE		2
#define	CODEC_WMA		3
#define	CODEC_VORBIS		4
#define	CODEC_FLAC		5
#define	NUM_CODECS		6

/* regular expression types */
#define	RE_INCLUDE		0
#define	RE_EXCLUDE		1
#define	RE_IGNORE_AS_CHILD	2
#define	RE_STEREO_BLEED		3

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
	int		codec;
	int		scanned;

	struct fidinfo	*next;
};

/* tag properties */
struct tagprop
{
	char		*name;	/* name of tag */
	int		skipdb;	/* whether to skip in database */
};

/* linked list or regular expressions */
struct relist
{
	char		*re;
	regex_t		recomp;
	int		retype;
	struct relist	*next;
};

#ifdef __APPLE__
#define ino_t int
#endif

/* stat extra */
struct statex
{
	dev_t		dev;
	ino_t		ino;
	off_t		size;
	time_t		mtime;
	time_t		ctime;
	char		*path;
	int		codec;
};

struct cds
{
	iconv_t		fstointernal;
	iconv_t		fstodb;
	iconv_t		utf16tointernal;
	iconv_t		utf8tointernal;
	iconv_t		ucs4tointernal;
};

/* program options */
struct progopts
{
	const char	*progname;		/* program name */
	const char	**mp3dirs;		/* mp3 base directories */
	const char	*fiddir;		/* fids base directory */
	char		*fscodeset;		/* filesystem character encoding */
	char		*internalcodeset;	/* internal character encoding */
	char		*dbcodeset;		/* database character encoding */
	unsigned int	drive2perc;		/* percentage of fids to put in drive 2 */
	unsigned int	maxtaglen;		/* maximum tag length in database */
	int		brokendrive2;		/* skip fids on drive 2 in playlists */
	int		intellisort;		/* sort "intelligently" */
	int		sortignorecase;		/* ignore case in directory sorts */
	int		reignorecase;		/* ignore case in regular expressions */
	int		rebuilddb;		/* rebuild mp3tofid database */
	int		newdb;			/* force creation of new mp3tofid database */
	int		nopurgedb;		/* don't purge the mp3tofid database */
	int		spreadplaylists;	/* spread playlists over both drives */
	int		olddirstruct;		/* old directory structure */
	char		*playerdbname;		/* player database name */
	int		playerversion;		/* player version */
	int		ignoresymlinks;		/* ignore symbolic links */
	int		ignoreshortcuts;	/* ignore windows shortcuts */
	int		ignorem3u;		/* ignore m3u playlists */
	int		ignoremp3;		/* ignore mp3 files */
	int		ignorewav;		/* ignore wave files */
	int		ignorewma;		/* ignore wma files */
	int		ignoreogg;		/* ignore ogg vorbis files */
	int		ignoreflac;		/* ignore flac files */
	int		oggnominalbr;		/* ogg nominal bitrate */
	char		*preferredcodec;	/* preferred codec */
	int		utimplaylist;		/* set mtime on playlist fids */
	int		forcescan;		/* force scanning of tunes */
	int		showscandir;		/* show directories being scanned */
	int		showinodedb;		/* show operations on mp3tofid database */
	int		showskiptune;		/* show mp3's that do not need scanning */
	int		showremovefid;		/* show which fids are being deleted */
	int		showstages;		/* show stages of program */
	int		showscantune;		/* show mp3's being scanned */
	int		showstatistics;		/* show statistics */
	int		showexclusions;		/* show exclusions */
	int		showmarks;		/* show marks */
	int		mainmp3diridx;		/* index to main mp3 directory */
};
