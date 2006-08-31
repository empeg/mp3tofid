#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <gdbm.h>
#include <popt.h>
#include <regex.h>
#include <locale.h>
#include <langinfo.h>
#include <openssl/md5.h>

#include "version.h"
#include "fids.h"
#include "mp3tofid.h"
#include "prototypes.h"

#ifdef	__CYGWIN__
#define	reclen(x)	(strlen((x)->d_name))
#else
#define	reclen(x)	((x)->d_reclen)
#endif

/* program option related globals */
struct progopts progopts =
{
	NULL,		/* program name */
	NULL,		/* mp3 base directory */
	NULL,		/* fids base directory */
	NULL,		/* filesystem character encoding */
	"UTF-8",	/* internal character encoding */
	NULL,		/* database character encoding */
	50,		/* percentage of fids to put in drive 2 */
	255,		/* maximum tag length in database */
	0,		/* skip fids on drive 2 in playlists */
	0,		/* sort "intelligently" */
	0,		/* ignore case in directory sorts */
	0,		/* ignore case in regular exporessions */
	1,		/* rebuild mp3tofid database */
	0,		/* force creation of new mp3tofid database */
	0,		/* don't purge the mp3tofid database */
	0,		/* spread playlists over both drives */
	0,		/* old directory structure */
	"database3",	/* player database name */
	-1,		/* player version */
	0,		/* ignore symbolic links */
	0,		/* ignore windows shortcuts */
	0,		/* ignore m3u playlists */
	0,		/* ignore mp3 files */
	0,		/* ignore wave files */
	0,		/* ignore wma files */
	0,		/* ignore ogg files */
	0,		/* ignore flac files */
	0,		/* ogg nominal bitrate */
	NULL,		/* preferred codec */
	0,		/* set mtime on playlist fids */
	0,		/* force scanning of tunes */
	0,		/* show directories being scanned */
	0,		/* show operations on mp3tofid database */
	0,		/* show mp3's that do not need scanning */
	0,		/* show which fids are being deleted */
	1,		/* show stages of program */
	1,		/* show mp3's being scanned */
	1,		/* show statistics */
	1,		/* show exclusions */
	1,		/* show marks */
};


/* data structure related globals */
struct fidinfo	*fihead;		/* head of linked list */
struct fidinfo	*fitail;		/* tail of linked list */
struct tagprop	tagprops[MAXTAGS];	/* properties of tags we know of */
unsigned int	ntagprops;		/* number of detected tag names */
struct relist	*relisthead = NULL;	/* head of linked list */
struct relist	*relisttail = NULL;	/* tail of linked list */
struct cds	cds;			/* iconv conversion descriptors */
GDBM_FILE	dbf;			/* handle for inode-to-fid database */
char		*inotable[MAXFIDNUM];	/* inode-to-fid db in memory */
int		*codecweight = NULL;	/* order of codec preference */
dev_t		mp3stdev;		/* device containing mp3 root */

/* from emptool fids.h */
inline unsigned int getfidnumber(FID fid)
{
    return (fid >> 4);
}

/* from emptool fids.h */
inline unsigned int getfidtype(FID fid)
{
    return (fid & 0xf);
}

/* from emptool fids.h */
inline FID makefid(unsigned int n, unsigned int t)
{
    return (n << 4) | (t & 0xf);
}

/* helper for calculaterid() */
int
calculateridpart(FILE *fp, size_t startpos, size_t endpos, unsigned char *md5sum)
{
	unsigned char	buffer[RID_CHUNK];
	size_t		bytes_read;

	if (fseek(fp, startpos, SEEK_SET))
		return 0;

	bytes_read = fread(buffer, 1, endpos-startpos, fp);
	if ((bytes_read == 0) && !feof(fp))
		return 0;

	MD5((const unsigned char*)buffer, bytes_read, md5sum);

	return 1;
}

/* Calculate a unique ID for the interesting section of the file */
char *
calculaterid(FILE *fp, size_t startpos, size_t endpos)
{
	unsigned char	rid[16];
	unsigned char	ridpart[16];
	int		st;
	int		i;
	unsigned int	halfway;
	char		result[33];

	if ((endpos-startpos) <= RID_CHUNK)
	{
		/* Not big enough to do the whole thing: just md5 it all */
		st = calculateridpart(fp, startpos, endpos, rid);
	}
	else
	{
		/* 3-way MD5 (to catch songs that start the same as other songs but
		 * are different, without the overhead of md5ing the whole thing)
		 */
	
		st = calculateridpart(fp, startpos, startpos+RID_CHUNK, rid);

		if (st)
		{
			st = calculateridpart(fp, endpos-RID_CHUNK, endpos, ridpart);
			for (i=0; i<16; i++)
				rid[i] ^= ridpart[i];
		}

		if (st)
		{
			halfway = (startpos + (endpos-RID_CHUNK)) / 2;
			st = calculateridpart(fp, halfway, halfway+RID_CHUNK, ridpart);
			for (i=0; i<16; i++)
				rid[i] ^= ridpart[i];
		}
	}

	if (st)
	{
		result[0] = '\0';
		for (i=0; i<16; i++)
			sprintf(result,"%s%02x", result, rid[i]);
		result[32] = '\0';
		return (strdup(result));
	}

	return NULL;
}

/* allocate memory, print error and exit on failure */
void *
emalloc(size_t size)
{
	void	*memory;

	if ((memory = malloc(size)) == NULL)
	{
		fprintf(stderr, "%s: malloc() failed\n", progopts.progname);
		exit(1);
	}

	return (memory);
}

/* allocate memory, print error and exit on failure */
void *
ecalloc(size_t nmemb, size_t size)
{
	void	*memory;

	if ((memory = calloc(nmemb, size)) == NULL)
	{
		fprintf(stderr, "%s: calloc(%lu, %lu) failed\n",
			progopts.progname, nmemb, size);
		exit(1);
	}

	return (memory);
}

/* open a file, print error and exit on failure */
FILE *
efopen(char *path, char *mode)
{
	FILE	*fp;

	if ((fp = fopen(path, mode)) == NULL)
	{
		fprintf(stderr, "%s: can not open %s: %s\n",
				progopts.progname, path, strerror(errno));
		exit(1);
	}
	return fp;
}

/* return NULL if a string is length zero, otherwise the string itself */
char *
nullifempty(char *string)
{
	if (string && strlen(string))
		return string;
	else
		return NULL;
}

/* allocate a clear node in linked list */
struct fidinfo *
allocfidinfo()
{
	struct fidinfo	*fidinfo;
	size_t		fidinfosize;

	fidinfosize = sizeof(struct fidinfo);
	fidinfo = (struct fidinfo *) emalloc(fidinfosize);
	memset(fidinfo, 0, fidinfosize);

	return (fidinfo);
}

/* initialiaze global data structures */
void
initdata()
{
	/* prefill the standard tag properties */
	ntagprops = NTAGS;

	tagprops[TAG_ARTIST_NUM].name		= TAG_ARTIST_NAME;
	tagprops[TAG_BITRATE_NUM].name		= TAG_BITRATE_NAME;
	tagprops[TAG_CODEC_NUM].name		= TAG_CODEC_NAME;
	tagprops[TAG_COMMENT_NUM].name		= TAG_COMMENT_NAME;
	tagprops[TAG_CTIME_NUM].name		= TAG_CTIME_NAME;
	tagprops[TAG_DRM_NUM].name		= TAG_DRM_NAME;
	tagprops[TAG_DURATION_NUM].name		= TAG_DURATION_NAME;
	tagprops[TAG_FILE_ID_NUM].name		= TAG_FILE_ID_NAME;
	tagprops[TAG_GENRE_NUM].name		= TAG_GENRE_NAME;
	tagprops[TAG_LENGTH_NUM].name		= TAG_LENGTH_NAME;
	tagprops[TAG_LOADFROM_NUM].name		= TAG_LOADFROM_NAME;
	tagprops[TAG_OFFSET_NUM].name		= TAG_OFFSET_NAME;
	tagprops[TAG_OPTIONS_NUM].name		= TAG_OPTIONS_NAME;
	tagprops[TAG_RID_NUM].name		= TAG_RID_NAME;
	tagprops[TAG_SAMPLERATE_NUM].name	= TAG_SAMPLERATE_NAME;
	tagprops[TAG_SOURCE_NUM].name		= TAG_SOURCE_NAME;
	tagprops[TAG_TITLE_NUM].name		= TAG_TITLE_NAME;
	tagprops[TAG_TRACKNR_NUM].name		= TAG_TRACKNR_NAME;
	tagprops[TAG_TRAILER_NUM].name		= TAG_TRAILER_NAME;
	tagprops[TAG_TYPE_NUM].name		= TAG_TYPE_NAME;
	tagprops[TAG_YEAR_NUM].name		= TAG_YEAR_NAME;

	tagprops[TAG_ARTIST_NUM].skipdb		= TAG_ARTIST_SKIPDB;
	tagprops[TAG_BITRATE_NUM].skipdb	= TAG_BITRATE_SKIPDB;
	tagprops[TAG_CODEC_NUM].skipdb		= TAG_CODEC_SKIPDB;
	tagprops[TAG_COMMENT_NUM].skipdb	= TAG_COMMENT_SKIPDB;
	tagprops[TAG_CTIME_NUM].skipdb		= TAG_CTIME_SKIPDB;
	tagprops[TAG_DRM_NUM].skipdb		= TAG_DRM_SKIPDB;
	tagprops[TAG_DURATION_NUM].skipdb	= TAG_DURATION_SKIPDB;
	tagprops[TAG_FILE_ID_NUM].skipdb	= TAG_FILE_ID_SKIPDB;
	tagprops[TAG_GENRE_NUM].skipdb		= TAG_GENRE_SKIPDB;
	tagprops[TAG_LENGTH_NUM].skipdb		= TAG_LENGTH_SKIPDB;
	tagprops[TAG_LOADFROM_NUM].skipdb	= TAG_LOADFROM_SKIPDB;
	tagprops[TAG_OFFSET_NUM].skipdb		= TAG_OFFSET_SKIPDB;
	tagprops[TAG_OPTIONS_NUM].skipdb	= TAG_OPTIONS_SKIPDB;
	tagprops[TAG_RID_NUM].skipdb		= TAG_RID_SKIPDB;
	tagprops[TAG_SAMPLERATE_NUM].skipdb	= TAG_SAMPLERATE_SKIPDB;
	tagprops[TAG_SOURCE_NUM].skipdb		= TAG_SOURCE_SKIPDB;
	tagprops[TAG_TITLE_NUM].skipdb		= TAG_TITLE_SKIPDB;
	tagprops[TAG_TRACKNR_NUM].skipdb	= TAG_TRACKNR_SKIPDB;
	tagprops[TAG_TRAILER_NUM].skipdb	= TAG_TRAILER_SKIPDB;
	tagprops[TAG_TYPE_NUM].skipdb		= TAG_TYPE_SKIPDB;
	tagprops[TAG_YEAR_NUM].skipdb		= TAG_YEAR_SKIPDB;

	/* initialize linked list */
	fihead = allocfidinfo();
	fitail = fihead;

	/* create fid 0, must exist in /drive0/var/database */
	fihead->fidnumber = 0;
	fihead->tagtype = TAG_TYPE_ILLEGAL;
	fihead->tagvalues = (char **) ecalloc(ntagprops, sizeof(char *));
	fihead->tagvalues[TAG_TYPE_NUM] = "illegal";
	fihead->ntagvalues = ntagprops;

	/* initialize character coding conversions */
	init_iconv();
}

char *
codecname(int codecnum)
{
	switch (codecnum)
	{
		case CODEC_NONE:	return ("none");
		case CODEC_MP3:		return ("mp3");
		case CODEC_WAVE:	return ("wave");
		case CODEC_WMA:		return ("wma");
		case CODEC_VORBIS:	return ("vorbis");
		case CODEC_FLAC:	return ("flac");
		default:		return ("illegal");
	}
}

int
codecnum(char *codecname)
{
	if (codecname == NULL)
		return CODEC_NONE;
	else if (strcmp(codecname, "mp3")    == 0)
		return CODEC_MP3;
	else if (strcmp(codecname, "wave")   == 0)
		return CODEC_WAVE;
	else if (strcmp(codecname, "wma")    == 0)
		return CODEC_WMA;
	else if (strcmp(codecname, "vorbis") == 0)
		return CODEC_VORBIS;
	else if (strcmp(codecname, "flac")   == 0)
		return CODEC_FLAC;
	else 
		return CODEC_NONE;
}

int
islink(char *path)
{
	struct stat	statbuf;

	return ((lstat(path, &statbuf) == 0) && (S_ISLNK(statbuf.st_mode)));
}

/* create a directory, don't bother if it exists, complain loudly about
 * other failures */
void
emkdir(char *dir)
{
	if ((mkdir(dir, (mode_t) 0777) < 0) && (errno != EEXIST))
	{
		fprintf(stderr, "%s: can not create directory '%s': %s\n",
				progopts.progname, dir, strerror(errno));
		exit(1);
	}
}

/* create subdirectory in fids tree */
void
mksubdir(char *subdir)
{
	char fidsubdir[PATH_MAX];

	sprintf(fidsubdir, "%s/%s", progopts.fiddir, subdir);
	emkdir(fidsubdir);
}

/* strip directory from path */
char *
stripdirfrompath(char *path)
{
	char	*filename;

	if ((filename = strrchr(path, '/')) == NULL)
		filename = path;
	else
		filename++;

	return filename;
}

/* convert fid pathname to FID */
FID
fidpathtofid(char *path)
{
	FID	fid;
	char	*endptr;
	char	*startnewscheme;
	char	fidmajor[6];
	char	fidminor[4];

	/* try to detect a new-scheme fid path */
	startnewscheme = path + strlen(path) - 10;
	if ((startnewscheme[0] == '_') && (startnewscheme[6] == '/'))
	{
		strncpy(fidmajor, startnewscheme + 1, 5);
		strncpy(fidminor, startnewscheme + 7, 3);
		fidmajor[5] = '\0';
		fidminor[3] = '\0';
		fid = (strtoul(fidmajor, NULL, 16) << 12) +
			strtoul(fidminor, NULL, 16);

	}
	else
	{
		/* turn the filename part into an integer */
		fid = strtoul(stripdirfrompath(path), &endptr, 16);
		if (*endptr)
			return 0;	
	}

	return fid;
}

/* calculate which drive to store fid on */
unsigned int
fidnumbertodrive(unsigned int fidnumber, int tagtype)
{
	if ((progopts.spreadplaylists == 0) && (tagtype == TAG_TYPE_PLAYLIST))
		return 0;
	return ((fidnumber % 100) > (100 - progopts.drive2perc));
}

/* calculate fid number from source file properties */
unsigned int
sourcefiletofidnumber(struct statex *se, unsigned int infidnumber)
{
	datum		key;
	datum		content;
	char		inostring[64];
	char		fidstring[16];
	unsigned int	fidnumber = 0;
	int		i;

	if (se->dev == mp3stdev)
		sprintf(inostring, "%lu", se->ino);
	else
		sprintf(inostring, "%d,%d,%lu",
			major(se->dev), minor(se->dev), se->ino);
	key.dptr = inostring;
	key.dsize = strlen(inostring) + 1;

	content = gdbm_fetch(dbf, key);

	if (content.dptr)
	{
		fidnumber = atoi(content.dptr);
		if (progopts.showinodedb)
			printf("fetched inode = %s, fidnumber = %x\n",
				inostring, fidnumber);
	}
	else
	{
		if (infidnumber && progopts.rebuilddb)
			fidnumber = infidnumber;
		else
		{
			for (i=getfidnumber(FID_FIRSTNORMAL); i<MAXFIDNUM; i++)
			{
				if (inotable[i] == NULL)
				{
					fidnumber = i;
					break;
				}
			}

			if (fidnumber == 0)
			{
				fprintf(stderr, "%s: fidnumber overflow\n",
						progopts.progname);
				exit(1);
			}
		}

		sprintf(fidstring, "%u", fidnumber);
		content.dptr = fidstring;
		content.dsize = strlen(fidstring) + 1;

		gdbm_store(dbf, key, content, GDBM_INSERT);
		inotable[fidnumber] = strdup(inostring);
		if (progopts.showinodedb)
			printf("stored inode = %s, fidnumber = %x\n",
				inostring, fidnumber);
	}

	return (fidnumber);
}

void
releasefid(unsigned int fidnumber)
{
	inotable[fidnumber] = NULL;
}


/* build full path to fid file from fid number and type */
char *
fidnumbertofidpath(unsigned int fidnumber, unsigned int fidtype, int tagtype)
{
	char	fidpath[PATH_MAX];
	FID	fid;

	fid = makefid(fidnumber, fidtype);

	if (progopts.olddirstruct)
		sprintf(fidpath, "%s/drive%u/fids/%x",
			progopts.fiddir,
			fidnumbertodrive(fidnumber, tagtype), fid);
	else
		sprintf(fidpath, "%s/drive%u/fids/_%05x/%03x",
			progopts.fiddir, fidnumbertodrive(fidnumber, tagtype),
			fid >> 12, fid & 0xfff);
	return strdup(fidpath);
}

/* search the linked list for a fidnumber */
struct fidinfo *
searchfidinfo(unsigned int fidnumber)
{
	struct fidinfo	*fidinfo;

	fidinfo = fihead;
	do 
	{
		if (fidinfo->fidnumber == fidnumber)
			return fidinfo;
		fidinfo = fidinfo->next;
	}
	while (fidinfo);
	return NULL;
}

char *
titlefromfilename(char *filename, int strip)
{
	char	*title;
	char	*dotpos;
	size_t	titlelen = 0;

	if (strip)
	{
		title = stripdirfrompath(filename);
		if ((dotpos = strrchr(title, '.')) != NULL)
			titlelen = dotpos - title;
	}
	else
		title = filename;

	if (titlelen == 0)
		titlelen = strlen(title);

	return fstodb(title, titlelen);
}

int
checkmark(char *path, int marktype)
{
	struct relist	*rlp;

	for (rlp = relisthead; rlp; rlp = rlp->next)
		if ((rlp->retype == marktype) && regexec(&rlp->recomp, path, 0, 0, 0) == 0)
			return 1;

	return 0;
}

int
checkexclude(char *path)
{
	struct relist	*rlp;

	/* check excludes */
	for (rlp = relisthead; rlp; rlp = rlp->next)
	{
		if (regexec(&rlp->recomp, path, 0, 0, 0) == 0)
		{
			switch (rlp->retype)
			{
				case RE_EXCLUDE:
					return 1;
					break;
				case RE_INCLUDE:
					return 0;
					break;
			}
		}
	}

	return 0;
}

/* go through all in-memory fids, discard illegal ones */
void
cleanfids()
{
	int		ndiscarded = 0;
	struct fidinfo	*fidinfo;
	struct fidinfo	*prev;

	if (progopts.showstages)
		puts("cleaning fids");

	for (prev = fidinfo = fihead; fidinfo && fidinfo->ntagvalues; fidinfo = fidinfo->next)
	{
		if (fidinfo->tagtype == TAG_TYPE_TUNE && fidinfo->scanned == 0)
		{
			prev->next = fidinfo->next;
			ndiscarded++;
		}
		else
			prev = fidinfo;
	}

	if (progopts.showstages &&  ndiscarded)
		printf("discarded %d cached fids\n", ndiscarded);
}

/* go through all fids, loaded or scanned and set marks */
void
markfids()
{
	char		*options;
	char		*path;
	struct fidinfo	*fidinfo;

	if (progopts.showstages)
		puts("marking fid options");

	for (fidinfo = fihead; fidinfo && fidinfo->ntagvalues; fidinfo = fidinfo->next)
	{
		path = fidinfo->tagvalues[TAG_LOADFROM_NUM];
		options = NULL;

		switch (fidinfo->tagtype)
		{
			case TAG_TYPE_TUNE:
				if (checkmark(path, RE_STEREO_BLEED))
				{
					options  = "0x200";
					if (progopts.showmarks)
						printf("marking %s as stereo bleed\n", path);
				}
				break;
			case TAG_TYPE_PLAYLIST:
				if (checkmark(path, RE_IGNORE_AS_CHILD))
				{
					options = "0x20";
					if (progopts.showmarks)
						printf("marking %s as ignore as child\n", path);
				}
				break;
		}

		fidinfo->tagvalues[TAG_OPTIONS_NUM] = options;
	}
}

/* do sanity checks on in-memory fid data */
void
checkfids()
{
	int		i;
	int		errors = 0;
	struct fidinfo	*fidinfo;

	/* show our stage */
	if (progopts.showstages)
		puts("performing sanity checks");

	for (fidinfo = fihead; fidinfo && fidinfo->ntagvalues; fidinfo = fidinfo->next)
	{
		if (fidinfo->tagtype == TAG_TYPE_PLAYLIST)
		{
			/* check for illegal entries in playlists */
			for (i=0; i<fidinfo->pllength; i++)
			{
				if (searchfidinfo(getfidnumber(fidinfo->playlist[i])) == NULL)
				{
					fprintf(stderr, "%s: playlist %x (%s) points to non-existent fid %x\n",
						progopts.progname,
						fidinfo->fidnumber,
						fidinfo->tagvalues[TAG_TITLE_NUM],
						getfidnumber(fidinfo->playlist[i]));
					errors++;
				}
			}
		}
		else if (fidinfo->tagtype == TAG_TYPE_TUNE)
		{
			/* check for illegal duration */
			if (atoll(fidinfo->tagvalues[TAG_DURATION_NUM]) == 0LL)
			{
				fprintf(stderr, "%s: tune %x (%s) has duration 0\n",
					progopts.progname,
					fidinfo->fidnumber,
					fidinfo->tagvalues[TAG_LOADFROM_NUM]);
				errors++;
			}
		}
	}

	/* test for disconnected tunes */
	/* TODO */

	/* test for recursion in playlists */
	/* TODO */

	/* jump out if errors are detected */
	if (errors)
	{
		fprintf(stderr, "%s: %d sanity errors detected\n", progopts.progname, errors);
		exit(1);
	}
}

/* scan an mp3 file for its properties */
void
tunescan(struct statex *se, unsigned int fidnumber)
{
	char		**tagvalues;
	struct fidinfo	*fidinfo;
	char		tagvalue[MAXTAGLEN+1];

	/* lookup the tune in the cache */
	if ((fidinfo = searchfidinfo(fidnumber)) != NULL)
	{
		if (progopts.showskiptune)
			printf("no need to scan %s\n", se->path);
		fidinfo->scanned++;
		return;
	}

	if (progopts.showscantune)
		printf("scanning %05x %s\n", fidnumber, se->path);

	/* allocate data */
	fidinfo = fitail;
	if (fidinfo->ntagvalues)
	{
		fidinfo->next = allocfidinfo();
		fidinfo = fidinfo->next;
		fitail = fidinfo;
	}
	tagvalues = (char **) ecalloc(ntagprops, sizeof(char *));

	/* fill the allocated data */
	tagvalues[TAG_TYPE_NUM]     = "tune";
	tagvalues[TAG_LOADFROM_NUM] = se->path;
	sprintf(tagvalue, "%lu", se->ctime);
	tagvalues[TAG_CTIME_NUM]    = strdup(tagvalue);
	sprintf(tagvalue, "%lu", (unsigned long) se->size);
	tagvalues[TAG_LENGTH_NUM]   = strdup(tagvalue);
	tagvalues[TAG_DURATION_NUM] = "0";

	fidinfo->fidnumber   = fidnumber;
	fidinfo->tagtype     = TAG_TYPE_TUNE;
	fidinfo->tagvalues   = tagvalues;
	fidinfo->ntagvalues  = ntagprops;
	fidinfo->sourcemtime = se->mtime;
	fidinfo->codec       = se->codec;
	fidinfo->scanned++;

	/* get codec-specific tags */
	switch (se->codec)
	{
		case CODEC_MP3:
			scanmp3(fidinfo, se);
			break;
		case CODEC_WAVE:
			scanwave(fidinfo, se);
			break;
		case CODEC_WMA:
			scanwma(fidinfo, se);
			break;
		case CODEC_VORBIS:
			scanvorbis(fidinfo, se);
			break;
		case CODEC_FLAC:
			scanflac(fidinfo, se);
			break;
	}

	/* copy tracknr to file_id */
	fidinfo->tagvalues[TAG_FILE_ID_NUM] = fidinfo->tagvalues[TAG_TRACKNR_NUM];

	/* make sure we have at least a title */
	if (!tagvalues[TAG_TITLE_NUM])
		tagvalues[TAG_TITLE_NUM] = titlefromfilename(se->path, 1);
}

/* check whether string ends with <end> */
int
endswith(const char *string, const char *end)
{
	return (strcasecmp(string + strlen(string) - strlen(end), end) == 0);
}

/* check whether string begins with <begin> */
int
beginswith(const char *string, const char *begin)
{
	return (strncasecmp(string, begin, strlen(begin)) == 0);
}

/* check whether two filenames are the same, disregarding the extension */
int
basenamecmp(const char *namea, const char *nameb)
{
	char *cp;

	if ((cp = strrchr(namea, '.')) == NULL)
		return strcasecmp(namea, nameb);
	else
		return (strncasecmp(namea, nameb, cp - namea));
}

/* check whether a symlink in the mp3 tree is valid */
int
checklink(char *linkpath)
{
	FILE *fp;
	char targetpath[PATH_MAX];

	memset(targetpath, 0, PATH_MAX / sizeof(int));

	if (readlink(linkpath, targetpath, PATH_MAX) < 0)
	{
		fprintf(stderr, "%s: can not read symbolic link '%s': %s\n",
				progopts.progname, linkpath, strerror(errno));
		return 0;
	}

	if ((fp = fopen(linkpath, "r")) == NULL)
	{
		fprintf(stderr,
			"%s: '%s' is a symbolic link to '%s' which can not be opened: %s\n",
				progopts.progname, linkpath, targetpath, strerror(errno));
		return 0;
	}
	else
		fclose(fp);

	return 1;
}

/* comparison routine for use with scandir(3) */
int
direntcompar(const void *a, const void *b)
{
	int			i, j;
	int			offset[2];
	const char		*name[2];
	const struct dirent	*entarray[2];
	char			*ignoreinsort[] =
				{"A ", "An ", "Le ", "Les ", "The ", NULL};

	entarray[0] = *(const struct dirent **)a;
	entarray[1] = *(const struct dirent **)b;

	for (i=0; i<2; i++)
	{
		name[i] = entarray[i]->d_name;
		offset[i] = 0;

		if (progopts.intellisort)
		{
			for (j=0; ignoreinsort[j]; j++)
			{
				if (beginswith(name[i], ignoreinsort[j]))
				{
					offset[i] = strlen(ignoreinsort[j]);
					break;
				}
			}
		}
	}

	if (progopts.sortignorecase)
		return strcasecmp(name[0] + offset[0], name[1] + offset[1]);
	else
		return strcmp(name[0] + offset[0], name[1] + offset[1]);
}

/* recursively scan a directory for MP3's */
int
dirscan(const char *dir, unsigned int parentfidnumber, char *title)
{
	int		i, j, n;
	int		addfid;
	FID		*fids;
	int		nfids = 0;
	int		includethis = 0;
	char		*childpath;
	unsigned int	fidnumber = 0;
	struct dirent	**namelist;
	struct stat	statbuf;
	struct fidinfo	*fidinfo;
	char		**tagvalues;
	char		tagvalue[MAXTAGLEN+1];
	struct statex	**se;

	/* show what's going on */
	if (progopts.showscandir)
		printf("scanning %s\n", dir);

	/* count and sort directory entries */
	if ((n = scandir(dir, &namelist, NULL, direntcompar)) < 0)
	{

		fprintf(stderr, "%s: cannot scandir '%s': %s\n",
				progopts.progname, dir, strerror(errno));
		return (0);
	}

	/* allocate playlist */
	fids = (FID *) ecalloc(n, sizeof(FID));

	/* allocate array of file info */
	se = ecalloc(n, sizeof(struct statex *));

	/* first pass: check each directory entry */
	for (i=0; i<n; i++)
	{
		if (namelist[i]->d_name == NULL)
			continue;
		if (strcmp(namelist[i]->d_name, ".")  == 0)
			continue;
		if (strcmp(namelist[i]->d_name, "..") == 0)
			continue;

		/* build full pathname to directory entry */
		childpath = (char *) emalloc(strlen(dir) + reclen(namelist[i]) + 2);
		sprintf(childpath, "%s/%s", dir, namelist[i]->d_name);

		/* check this one isn't excluded, report later */
		includethis = !checkexclude(childpath);

		addfid = 0;
		if (stat(childpath, &statbuf) == 0)
		{
			se[i] = emalloc(sizeof(struct statex));
			se[i]->path  = childpath;
			se[i]->dev   = statbuf.st_dev;
			se[i]->ino   = statbuf.st_ino;
			se[i]->size  = statbuf.st_size;
			se[i]->mtime = statbuf.st_mtime;
			se[i]->ctime = statbuf.st_ctime;
			se[i]->codec = CODEC_NONE;

			/* if it's a symlink, just add it to the playlist */
			if (islink(childpath))
			{
				/* maybe ignore symlinks */
				if (!progopts.ignoresymlinks)
				{
					if (includethis)
					{
						addfid = checklink(childpath);
						if (addfid)
							fidnumber = sourcefiletofidnumber(se[i], 0);
					}
					else if (progopts.showexclusions)
						printf("excluding link %s\n", childpath);
				}
			}
			/* if it's a directory, recurse into it */
			else if (S_ISDIR(statbuf.st_mode))
			{
				if (includethis)
				{
					fidnumber = sourcefiletofidnumber(se[i], 0);
					if (dirscan(childpath, fidnumber,
							stripdirfrompath(childpath)))
						addfid++;
					else
						releasefid(fidnumber);
				}
				else if (progopts.showexclusions)
					printf("excluding directory %s\n", childpath);
			}
			/* windows shortcuts */
			else if (!progopts.ignoreshortcuts && endswith(childpath, ".lnk"))
			{
				/* TODO */
			}
			/* m3u playlists */
			else if (!progopts.ignorem3u && endswith(childpath, ".m3u"))
			{
				/* TODO */
			}
			/* tunes */
			else if (!progopts.ignoremp3  && endswith(childpath, ".mp2"))
				se[i]->codec = CODEC_MP3;
			else if (!progopts.ignoremp3  && endswith(childpath, ".mp3"))
				se[i]->codec = CODEC_MP3;
			else if (!progopts.ignoreogg  && endswith(childpath, ".ogg"))
				se[i]->codec = CODEC_VORBIS;
			else if (!progopts.ignorewav  && endswith(childpath, ".wav"))
				se[i]->codec = CODEC_WAVE;
			else if (!progopts.ignorewma  && endswith(childpath, ".wma"))
				se[i]->codec = CODEC_WMA;
			else if (!progopts.ignoreflac && endswith(childpath, ".flac"))
				se[i]->codec = CODEC_FLAC;
			else if (!progopts.ignoreflac && endswith(childpath, ".fla"))
				se[i]->codec = CODEC_FLAC;
		}
		else
			fprintf(stderr, "%s: can not stat '%s': %s\n",
						progopts.progname, childpath, strerror(errno));

		/* add passed directories and links */
		if (addfid)
			fids[nfids++] = makefid(fidnumber, FIDTYPE_TUNE);

		/* remove excluded tunes */
		if ((se[i]->codec != CODEC_NONE) && (!includethis))
		{
			free(se[i]);
			se[i] = NULL;
			if (progopts.showexclusions)
				printf("excluding tune %s\n", childpath);
		}
	}

	/* second pass: remove tunes with lesser codec weights */
	for (i=0; progopts.preferredcodec && i<n; i++)
	{
		if (se[i] && (se[i]->codec != CODEC_NONE))
		{
			for (j=0; j<i; j++)
			{
				if (se[i] && se[j] && (se[j]->codec != CODEC_NONE) && (basenamecmp(se[i]->path, se[j]->path) == 0))
				{
					if (codecweight[se[i]->codec] > codecweight[se[j]->codec])
					{
						free(se[j]);
						se[j] = NULL;
					}
					else if (codecweight[se[i]->codec] < codecweight[se[j]->codec])
					{
						free(se[i]);
						se[i] = NULL;
					}
				}
			}
		}
	}

	/* third pass: add the remaining tunes */
	for (i=0; i<n; i++)
	{
		if (se[i] && (se[i]->codec != CODEC_NONE))
		{
			fidnumber = sourcefiletofidnumber(se[i], 0);
			tunescan(se[i], fidnumber);
			if ((progopts.brokendrive2 == 0) ||
					(fidnumbertodrive(fidnumber, TAG_TYPE_TUNE) == 0))
				fids[nfids++] = makefid(fidnumber, FIDTYPE_TUNE);
		}
	}

	/* fourth pass: free memory */
	for (i=0; i<n; i++)
	{
		free(namelist[i]);
		free(se[i]);
	}
	free(namelist);
	free(se);

	/* warn about empty directories */
	if (nfids == 0)
	{
		fprintf(stderr, "%s: nothing found in directory %s\n",
				progopts.progname, dir);
		return nfids;
	}

	/* allocate data */
	fidinfo = fitail;
	if (fidinfo->ntagvalues)
	{
		fidinfo->next = allocfidinfo();
		fidinfo = fidinfo->next;
		fitail = fidinfo;
	}
	tagvalues = (char **) ecalloc(ntagprops, sizeof(char *));

	/* fill the allocated data */
	tagvalues[TAG_TYPE_NUM]     = "playlist";
	tagvalues[TAG_TITLE_NUM]    = titlefromfilename(title, 0);
	tagvalues[TAG_LOADFROM_NUM] = strdup(dir);
	stat(dir, &statbuf);
	sprintf(tagvalue, "%lu", statbuf.st_ctime);
	tagvalues[TAG_CTIME_NUM]    = strdup(tagvalue);
	sprintf(tagvalue, "%d", (int)(nfids * sizeof(FID)));
	tagvalues[TAG_LENGTH_NUM]   = strdup(tagvalue);

	fidinfo->fidnumber   = parentfidnumber;
	fidinfo->tagtype     = TAG_TYPE_PLAYLIST;
	fidinfo->tagvalues   = tagvalues;
	fidinfo->ntagvalues  = ntagprops;
	fidinfo->playlist    = fids;
	fidinfo->pllength    = nfids;
	fidinfo->sourcemtime = statbuf.st_mtime;

	return (nfids);
}

/* store valid tune tags into memory in order to prevent rescanning
 * the corresponding mp3's */
int
fidpathtofidinfo(struct fidinfo *fidinfo, char *fidpath)
{
	int		i;
	int		reasonstoinvalidate = 0;
	FILE		*fp;
	struct stat	fidstatbuf;
	struct stat	mp3statbuf;
	struct statex	se;
	char		*tok;
	char		*fidbuf;
	char		*fidname;
	FID		fid;
	size_t		fidsize;
	char		**tagvalues;
	int		ntagvalues;
	unsigned int	taglen;

	/* we may not need to bother */
	if (progopts.forcescan)
		return 0;

	/* strip directory from path */
	fidname = stripdirfrompath(fidpath);

	/* convert fidname to FID */
	if ((fid = fidpathtofid(fidpath)) == 0)
		return 0;	

	/* only tag fids */
	if (getfidtype(fid) != FIDTYPE_TAGS)
		return 0;

	/* open the fid file and read into memory */
	fp = efopen(fidpath, "r");
	fstat(fileno(fp), &fidstatbuf);
	fidsize = fidstatbuf.st_size;
	fidbuf = (char *) emalloc(fidsize + 1);
	if (fread(fidbuf, sizeof(char), fidsize, fp) != fidsize)
	{
		free(fidbuf);
		return 0;
	}
	fclose(fp);
	fidbuf[fidsize] = '\0';

	/* allocate tags */
	tagvalues = (char **) ecalloc(ntagprops, sizeof(char *));
	ntagvalues = ntagprops;

	/* parse memory contents */
	for (tok = strtok(fidbuf, "\n"); tok; tok = strtok(NULL, "\n"))
	{
		for (i=0; i<ntagprops; i++)
		{
			taglen = strlen(tagprops[i].name);
			if ((strncmp(tok, tagprops[i].name, taglen) == 0) &&
					(tok[taglen] = '='))
				tagvalues[i] = tok + taglen + 1;
		}
	}

	/* only tune fids are worth saving */
	if (tagvalues[TAG_TYPE_NUM] == NULL)
		reasonstoinvalidate++;
	else
		if (strcmp(tagvalues[TAG_TYPE_NUM], "tune") != 0)
			reasonstoinvalidate++;

	/* sanity checks */
	if (tagvalues[TAG_CTIME_NUM] == NULL)
		reasonstoinvalidate++;
	if (tagvalues[TAG_LENGTH_NUM] == NULL)
		reasonstoinvalidate++;
	if (tagvalues[TAG_LOADFROM_NUM] == NULL)
		reasonstoinvalidate++;

	if (reasonstoinvalidate)
	{
		free(fidbuf);
		free(tagvalues);
		return 0;
	}

	/* check whether source mp3 exists */
	if (stat(tagvalues[TAG_LOADFROM_NUM], &mp3statbuf) < 0)
	{
		free(fidbuf);
		free(tagvalues);
		return 1;
	}

	/* mtime of mp3 and fid should be same */
	if (mp3statbuf.st_mtime != fidstatbuf.st_mtime)
		reasonstoinvalidate++;

	/* check ctime stored in fid */
	if (mp3statbuf.st_ctime != atol(tagvalues[TAG_CTIME_NUM]))
		reasonstoinvalidate++;

	/* check length stored in fid */
	if (mp3statbuf.st_size != atol(tagvalues[TAG_LENGTH_NUM]))
		reasonstoinvalidate++;

	/* check fid value */
	se.ino = mp3statbuf.st_ino;
	se.dev = mp3statbuf.st_dev;
	if (sourcefiletofidnumber(&se, getfidnumber(fid)) != getfidnumber(fid))
		reasonstoinvalidate++;

	if (reasonstoinvalidate)
	{
		free(tagvalues);
		free(fidbuf);
		return 0;
	}

	/* OK, this fid passed; prepare data for return */
	fidinfo->fidnumber   = getfidnumber(fid);
	fidinfo->tagtype     = TAG_TYPE_TUNE;
	fidinfo->sourcemtime = mp3statbuf.st_mtime;
	fidinfo->tagvalues   = tagvalues;
	fidinfo->ntagvalues  = ntagvalues;
	fidinfo->codec       = codecnum(tagvalues[TAG_CODEC_NUM]);

	return 1;
}

/* recursively scan fids directories for valid fid files */
void
loadfidsfromdir(char *dirpath)
{
	DIR		*dirstream;
	struct dirent	*dirent;
	struct fidinfo	*fidinfo;
	char		fidpath[PATH_MAX];

	if ((dirstream = opendir(dirpath)) == NULL)
		return;

	while ((dirent = readdir(dirstream)) != NULL)
	{
		sprintf(fidpath, "%s/%s", dirpath, dirent->d_name);

		if (dirent->d_name[0] == '_')
			loadfidsfromdir(fidpath);
		else
		{

			fidinfo = fitail;
			if (fidinfo->ntagvalues)
			{
				fidinfo->next = allocfidinfo();
				fidinfo = fidinfo->next;
				fitail = fidinfo;
			}
			fidpathtofidinfo(fidinfo, fidpath);
		}
	}
	closedir(dirstream);
}

/* run loadfidsfromdir on all top fids dirs */
void
loadfids()
{
	int		drive;
	char		dirpath[PATH_MAX];

	if (progopts.showstages)
		puts("reading existings fids");

	for (drive=0; drive<2; drive++)
	{
		sprintf(dirpath, "%s/drive%d/fids", progopts.fiddir, drive);
		loadfidsfromdir(dirpath);
	}
}

/* delete everything from a fids directory */
void
delfidsfromdir(char *dirpath)
{
	char		fidpath[PATH_MAX];
	DIR		*dirstream;
	struct dirent	*dirent;


	if ((dirstream = opendir(dirpath)) == NULL)
		return;

	while ((dirent = readdir(dirstream)) != NULL)
	{
		sprintf(fidpath, "%s/%s", dirpath, dirent->d_name);
		if (dirent->d_name[0] == '_')
		{
			delfidsfromdir(fidpath);
			if (progopts.showremovefid)
				printf("removing directory %s\n", fidpath);
			rmdir(fidpath);
		}
		else
		{
			if (progopts.showremovefid)
				printf("unlinking %s\n", fidpath);
			unlink(fidpath);
		}
	}
	closedir(dirstream);
}

/* delete everything from the top fids directories */
void
delfids()
{
	int		drive;
	char		dirpath[PATH_MAX];

	if (progopts.showstages)
		puts("deleting existing fids");

	for (drive=0; drive<2; drive++)
	{
		sprintf(dirpath, "%s/drive%d/fids", progopts.fiddir, drive);
		delfidsfromdir(dirpath);
	}
}

/* create the necessary subdirectories in fid tree */
void
mksubdirs()
{
	if (progopts.showstages)
		puts("creating directories");
	mksubdir("drive0");
	mksubdir("drive1");
	mksubdir("drive0/fids");
	mksubdir("drive1/fids");
	mksubdir("drive0/lost+found");
	mksubdir("drive1/lost+found");
	mksubdir("drive0/var");
}

/* make a new-scheme fid subdirectory needed for a fid file */
void
mkfidsubdir(char *fidpath)
{
	char	*lastslash;
	char	*fiddir;

	if (progopts.olddirstruct)
		return;

	fiddir = strdup(fidpath);
	if ((lastslash = strrchr(fiddir, '/')) != NULL)
	{
		*lastslash = '\0';
		emkdir(fiddir);
	}

	free(fiddir);
}

/* comparison routine for use with qsort(3) */
int
fidinfocompar(const void *a, const void *b)
{
	unsigned int	fidnumbera, fidnumberb;

	fidnumbera = (*(struct fidinfo **)a)->fidnumber;
	fidnumberb = (*(struct fidinfo **)b)->fidnumber;

	if (fidnumbera == fidnumberb)
		return 0;
	else
		return (fidnumbera > fidnumberb) ? 1 : -1;
}

unsigned int
umin(unsigned int a, unsigned int b)
{
	if (a < b)
		return a;
	else
		return b;
}

char *
printduration(long long duration)
{
	time_t		seconds;
	static char	ret[128];
	struct tm	tm;

	seconds = duration / 1000;
	gmtime_r(&seconds, &tm);

	if (tm.tm_yday)
		sprintf(ret, "%3d ", tm.tm_yday);
	else
		sprintf(ret, "    ");

	sprintf(ret, "%s%02d:%02d:%02d",
			ret, tm.tm_hour, tm.tm_min, tm.tm_sec);
	return ret;
}

char *
printthousands(long long thousands)
{
	int		i, j=0;
	static char	ret[64];
	static char	tmp[64];
	int		len;


	sprintf(tmp, "%lld", thousands);
	len = strlen(tmp);
	for (i=0; i<len; i++)
	{
		if (i && (((len - i) % 3) == 0))
			ret[j++] = '.'; /* should be THOUSEP(LC_NUMERIC); */
		ret[j++] = tmp[i];
	}
	ret[j] = '\0';
	return ret;
}

void
savefids()
{
	int		i, j;
	int		lastfidnumber = 0;
	unsigned int	nfids = 0;
	unsigned int	emptyfids = 0;
	unsigned int	netemptyfids;
	unsigned int	tunefids = 0;
	unsigned int	codecfids[NUM_CODECS] = {0};
	unsigned int	playlistfids = 0;
	unsigned int	taglen;
	long long	length;
	long long	duration;
	long long	tunelength = 0;
	long long	tuneduration = 0;
	long long	codeclength[NUM_CODECS] = {0};
	long long	codecduration[NUM_CODECS] = {0};
	struct fidinfo	*fidinfo;
	struct fidinfo	**fidinfoarray;
	struct utimbuf	utimbuf;
	struct stat	statbuf;
	FID		fid;
	FILE		*fptags;
	FILE		*fpfids;
	FILE		*fpdatabase;
	FILE		*fpplaylists;
	char		path[PATH_MAX];
	char		*fidpath;

	/* show what's going on */
	if (progopts.showstages)
	puts("writing fids and databases");

	/* open database files */
	sprintf(path, "%s/drive0/var/%s", progopts.fiddir, progopts.playerdbname);
	fpdatabase  = efopen(path, "w");
	sprintf(path, "%s/drive0/var/playlists", progopts.fiddir);
	fpplaylists = efopen(path, "w");
	sprintf(path, "%s/drive0/var/tags",      progopts.fiddir);
	fptags      = efopen(path, "w");

	/* write tags file */
	for (i=0; i<MAXTAGS; i++)
	fprintf(fptags, "%s\n", tagprops[i].name ? tagprops[i].name : "");
	fclose(fptags);

	/* count fids */
	for (fidinfo = fihead; fidinfo && fidinfo->ntagvalues; fidinfo = fidinfo->next)
	nfids++;

	/* allocate an array for them */
	fidinfoarray = (struct fidinfo **) ecalloc(nfids, sizeof(struct fidinfo *));

	/* fill the array */
	i=0;
	for (fidinfo = fihead; fidinfo && fidinfo->ntagvalues; fidinfo = fidinfo->next)
	fidinfoarray[i++] = fidinfo;

	/* sort the array */
	qsort(fidinfoarray, nfids, sizeof(struct fidinfo *), fidinfocompar);

	/* save all fids */
	for (i=0; i<nfids; i++)
	{
		fidinfo = fidinfoarray[i];
		fidpath = fidnumbertofidpath(fidinfo->fidnumber, FIDTYPE_TUNE,
			fidinfo->tagtype);

		mkfidsubdir(fidpath);

		if (fidinfo->tagtype == TAG_TYPE_PLAYLIST)
		{
			/* create the playlist "tune" fid */
			fpfids = efopen(fidpath, "w");
			for (j=0; j<fidinfo->pllength; j++)
			{
				fid = fidinfo->playlist[j];
				fwrite(&fid, sizeof(fid), 1, fpfids);
				fwrite(&fid, sizeof(fid), 1, fpplaylists);
			}
			fclose(fpfids);
			playlistfids++;

			/* set the mtime */
			if (progopts.utimplaylist && fidinfo->sourcemtime)
			{
				utimbuf.actime  = fidinfo->sourcemtime;
				utimbuf.modtime = fidinfo->sourcemtime;
				utime(fidpath, &utimbuf);
			}
		}
		else if (fidinfo->tagtype == TAG_TYPE_TUNE)
		{
			/* create symlink from mp3 file to fid */
			if (symlink(fidinfo->tagvalues[TAG_LOADFROM_NUM], fidpath) < 0)
				fprintf(stderr, "%s: cannot symlink '%s' to '%s': %s\n",
					progopts.progname, fidinfo->tagvalues[TAG_LOADFROM_NUM],
					fidpath, strerror(errno));

			/* gather statistics while we can */
			length = atoll(fidinfo->tagvalues[TAG_LENGTH_NUM]);
			duration = atoll(fidinfo->tagvalues[TAG_DURATION_NUM]);
			tunefids++;
			tunelength += length;
			tuneduration += duration;
			codecfids[fidinfo->codec]++;
			codeclength[fidinfo->codec] += length;
			codecduration[fidinfo->codec] += duration;
		}

		/* create the tag fid */
		if ((fidinfo->tagtype == TAG_TYPE_PLAYLIST) ||
			(fidinfo->tagtype == TAG_TYPE_TUNE))
		{
			fidpath = fidnumbertofidpath(fidinfo->fidnumber, FIDTYPE_TAGS,
					fidinfo->tagtype);
			fpfids = efopen(fidpath, "w");
			for (j=0; j<fidinfo->ntagvalues; j++)
				if (fidinfo->tagvalues[j])
					fprintf(fpfids, "%s=%s\n", tagprops[j].name,
							fidinfo->tagvalues[j]);
			fclose(fpfids);
		}

		/* set the fid's mtime; makes things easy for rsync and ourselves */
		if (fidinfo->sourcemtime)
		{
			utimbuf.actime  = fidinfo->sourcemtime;
			utimbuf.modtime = fidinfo->sourcemtime;
			utime(fidpath, &utimbuf);
		}

		/* fill up empty database entries */
		for (j=lastfidnumber; j<fidinfo->fidnumber; j++)
		{
			fputc(0xff, fpdatabase);
			emptyfids++;
		}

		/* append entry to database */
		for (j=0; j<fidinfo->ntagvalues; j++)
		{
			if ((fidinfo->tagvalues[j]) && !tagprops[j].skipdb)
			{
				taglen = umin(progopts.maxtaglen,
						strlen(fidinfo->tagvalues[j]));
				fputc(j, fpdatabase);
				fputc(taglen, fpdatabase);
				fwrite(fidinfo->tagvalues[j], 1, taglen, fpdatabase);
			}
		}

		/* terminate entry */
		fputc(0xff, fpdatabase);

		lastfidnumber = fidinfo->fidnumber + 1;
	}

	/* display statistics */
	if (progopts.showstatistics)
	{
		fflush(fpdatabase);
		fstat(fileno(fpdatabase), &statbuf);
		netemptyfids = emptyfids - 16;

		printf("database statistics:\n");
		printf("  database size:\t%16s\n", printthousands(statbuf.st_size));
		printf("  highest fid number:\t%16x\n", nfids);

		printf("  number of empty fids:\t\t%8u\n", netemptyfids);
		printf("  number of playlists:\t\t%8u\n", playlistfids);

		printf("  number of    all tunes:\t%8u\n", tunefids);
		for (i=0; i<NUM_CODECS; i++)
			if (codecfids[i])
				printf("  number of %6s tunes:\t%8u\n",
						codecname(i), codecfids[i]);

		printf("  size of    all tunes:\t%16s\n", printthousands(tunelength));
		for (i=0; i<NUM_CODECS; i++)
			if (codecfids[i])
				printf("  size of %6s tunes:\t%16s\n",
						codecname(i), printthousands(codeclength[i]));

		printf("  duration of    all tunes: %s\n", printduration(tuneduration));
		for (i=0; i<NUM_CODECS; i++)
			if (codecfids[i])
				printf("  duration of %6s tunes: %s\n",
						codecname(i), printduration(codecduration[i]));

	}

	/* close database files */
	fclose(fpdatabase);
	fclose(fpplaylists);
}

/* get major and minor number of root of mp3 tree */
void
getmp3stdev(const char *dir)
{
	struct stat	statbuf;

	if (stat(dir, &statbuf) == 0)
		mp3stdev = statbuf.st_dev;
}

/* scan mp3 tree */
void
scanfids()
{
	if (progopts.showstages)
		puts("scanning MP3 directories");
	dirscan(progopts.mp3dir, getfidnumber(FID_ROOTPLAYLIST) , "All Music");
}

/* load the inode-to-fid database */
void
loaddb()
{
	char		dbpath[PATH_MAX];
	datum		key;
	datum		nextkey;
	datum		content;
	unsigned int	fidnumber;
	unsigned int	minfidnumber;
	int		gdbmopenmode;

	minfidnumber = getfidnumber(FID_FIRSTNORMAL);

	sprintf(dbpath, "%s/drive0/var/mp3tofid.gdbm", progopts.fiddir);
	if (access(dbpath, F_OK) && !progopts.newdb)
		progopts.rebuilddb++;

	gdbmopenmode = progopts.newdb || progopts.rebuilddb ?  GDBM_NEWDB : GDBM_WRITER;
	if ((dbf = gdbm_open(dbpath, 0, gdbmopenmode, 0644, NULL)) == NULL)
	{
		gdbm_strerror(gdbm_errno);
		exit(1);
	}

	key = gdbm_firstkey(dbf);
	while (key.dptr)
	{
		nextkey = gdbm_nextkey(dbf, key);
		content = gdbm_fetch(dbf, key);
		fidnumber = atoi(content.dptr);
		if ((fidnumber >= minfidnumber) && (fidnumber < MAXFIDNUM))
		{
			inotable[fidnumber] = key.dptr;
			if (progopts.showinodedb)
				printf("loading inode = %s, fidnumber %x\n",
					key.dptr, fidnumber);
		}
		else
			free(key.dptr);
		free(content.dptr);
		key = nextkey;
	}
}

/* purge and close the inode-to-fid database */
void
purgedb()
{
	unsigned int	fidnumber;
	datum		key;
	int		npurges = 0;

	if (!progopts.nopurgedb)
	{
		/* show what's going on */
		if (progopts.showstages)
			puts("purging mp3tofid database");

		for (fidnumber=0; fidnumber<MAXFIDNUM; fidnumber++)
		{
			if ((inotable[fidnumber]) && (!searchfidinfo(fidnumber)))
			{
				key.dptr = inotable[fidnumber];
				key.dsize = strlen(inotable[fidnumber]) + 1;
				gdbm_delete(dbf, key);
				npurges++;
				if (progopts.showinodedb)
					printf("deleting inode = %s, fidnumber %x\n",
						key.dptr, fidnumber);
			}
		}

		if (npurges)
			printf("purged %d database entries\n", npurges);
	}
	gdbm_close(dbf);
}

/* show we're done */
void
finish()
{
	/* show what's going on */
	if (progopts.showstages)
		puts("done");
}

/* prepare regular expression search pattern */
void
addre(char *re, int retype)
{
	struct relist	*relist;
	size_t		relistsize;
	int		cflags;

	relistsize = sizeof(struct relist);
	relist = (struct relist *) emalloc(relistsize);
	memset(relist, 0, relistsize);

	if (relisttail != NULL)
		relisttail->next = relist;
	relisttail = relist;
	if (relisthead == NULL)
		relisthead = relist;

	relist->re     = re;
	relist->retype = retype;

	/* compile regular expression */
	if (progopts.reignorecase)
		cflags = REG_NOSUB | REG_ICASE;
	else
		cflags = REG_NOSUB;
	regcomp(&relist->recomp, re, cflags);
}

/* print usage, an error message and quit */
void
usage(poptContext optCon, char *message)
{
	poptPrintUsage(optCon, stderr, 0);
	if (message)
		fprintf(stderr, "%s: %s\n", progopts.progname, message);
	exit(1);
}

void
parsepreferredcodec(char *codecs, poptContext optCon)
{
	char	*tok;
	int	weight = NUM_CODECS - 1;

	codecweight = (int *) ecalloc(NUM_CODECS, sizeof(int));

	for (tok = strtok(codecs, ","); tok; tok = strtok(NULL, ","))
	{
		if (strcasecmp(tok, "ogg") == 0 || strcasecmp(tok, "vorbis") == 0)
			codecweight[CODEC_VORBIS] = weight--;
		else if (strncasecmp(tok, "fla", 3) == 0)
			codecweight[CODEC_FLAC]   = weight--;
		else if (strncasecmp(tok, "wav", 3) == 0)
			codecweight[CODEC_WAVE]   = weight--;
		else if (strcasecmp(tok, "wma") == 0)
			codecweight[CODEC_WMA]    = weight--;
		else if (strcasecmp(tok, "mp3") == 0)
			codecweight[CODEC_MP3]    = weight--;
		else
			usage(optCon, "cannot parse codec string");
	}
}

/* parse the command line */
void
popt(const int argc, const char **argv)
{
	char		c;
	char		*cp;
	char		*re = NULL;
	poptContext	optCon;

	struct poptOption optionsTable[] =
	{
		{"drive2-percentage",	'2',  POPT_ARG_INT,  &progopts.drive2perc,
				 0,   "percentage of files on second drive", "percentage"},
		{"broken-drive2",	'b',  POPT_ARG_NONE, &progopts.brokendrive2,
				 0,   "skip fids on drive 2 in playlists"},
		{"sort-ignore-case",	'i',  POPT_ARG_NONE, &progopts.sortignorecase,
				 0,   "ignore case in directory sorting"},
		{"intelligent-sort",	'I',  POPT_ARG_NONE, &progopts.intellisort,
				 0,   "\"intelligent\" directory sorting"},
		{"ignore-symlinks",	'l',  POPT_ARG_NONE, &progopts.ignoresymlinks,
				 0,   "ignore symbolic links"},
		{"ignore-shortcuts",	'\0', POPT_ARG_NONE, &progopts.ignoreshortcuts,
				 0,   "ignore windows shortcuts"},
		{"ignore-m3u",		'\0', POPT_ARG_NONE, &progopts.ignorem3u,
				 0,   "ignore m3u playlists"},
		{"ignore-mp3",		'\0', POPT_ARG_NONE, &progopts.ignoremp3,
				 0,   "ignore mp3 files"},
		{"ignore-wav",		'\0', POPT_ARG_NONE, &progopts.ignorewav,
				 0,   "ignore wave files"},
		{"ignore-wma",		'\0', POPT_ARG_NONE, &progopts.ignorewma,
				 0,   "ignore wma files"},
		{"ignore-ogg",		'\0', POPT_ARG_NONE, &progopts.ignoreogg,
				 0,   "ignore ogg vorbis files"},
		{"ignore-flac",		'\0', POPT_ARG_NONE, &progopts.ignoreflac,
				 0,   "ignore flac files"},
		{"ogg-nominal-bitrate",	'\0', POPT_ARG_NONE, &progopts.oggnominalbr,
				 0,   "use ogg nominal bitrate, not average"},
		{"preferred-codec",	 0,   POPT_ARG_STRING, &progopts.preferredcodec,
				 0,   "preferred codec", "codec[,codec]"},
		{"max-tag-length",	'm',  POPT_ARG_INT,  &progopts.maxtaglen,
				 0,   "maximum length of value of tag", "length"},
		{"rebuild-database",	'r',  POPT_ARG_NONE, &progopts.rebuilddb,
				 0,   "rebuild mp3tofid database"},
		{"force-new-database",	'n',  POPT_ARG_NONE, &progopts.newdb,
				 0,   "force creation of new mp3tofid database"},
		{"no-purge-database",	 0,   POPT_ARG_NONE, &progopts.nopurgedb,
				 0,   "don't purge the mp3tofid database"},
		{"force-scan",		 0,   POPT_ARG_NONE, &progopts.forcescan,
				 0,   "force scanning of tunes"},
		{"old-dir-struct",	'o',  POPT_ARG_NONE, &progopts.olddirstruct,
				 0,   "use old (pre-2.0b13) directory structure"},
		{"player-database-name", 0,   POPT_ARG_STRING, &progopts.playerdbname,
				 0,   "player database name", "name"},
		{"player-version",	 0,   POPT_ARG_INT,  &progopts.playerversion,
				 0,   "player version", "number"},
		{"utime-playlists",	'p',  POPT_ARG_NONE, &progopts.utimplaylist,
				 0,   "reset modification time of playlist fids"},
		{"spread-playlists",	's',  POPT_ARG_NONE, &progopts.spreadplaylists,
				 0,   "spread playlists over both drives"},
		{"version",		'v',  POPT_ARG_NONE, NULL,
				'v',  "show version"},
		{"quiet",		'q',  POPT_ARG_NONE, NULL,
				'q',  "disable all informational messages"},
		{"show-scan-dir",	'\0', POPT_ARG_NONE, &progopts.showscandir,
				 0,   "show directories being scanned"},
		{"show-inodedb",	'\0', POPT_ARG_NONE, &progopts.showinodedb,
				 0,   "show operations on mp3tofid database"},
		{"show-skip-tune",	'\0', POPT_ARG_NONE, &progopts.showskiptune,
				 0,   "show tunes that do not need scanning"},
		{"show-remove-fid",	'\0', POPT_ARG_NONE, &progopts.showremovefid,
				 0,   "show which fids are being deleted"},
		{"no-show-stages",	'\0', POPT_ARG_VAL,  &progopts.showstages,
				 0,   "don't show stages of program"},
		{"no-show-scantune",	'\0', POPT_ARG_VAL,  &progopts.showscantune,
				 0,   "don't show scanning of tunes"},
		{"no-show-statistics",	'\0', POPT_ARG_VAL,  &progopts.showstatistics,
				 0,   "don't show database statistics"},
		{"no-show-exclusions",	'\0', POPT_ARG_VAL,  &progopts.showexclusions,
				 0,   "don't show exclusions"},
		{"no-show-marks",	'\0', POPT_ARG_VAL,  &progopts.showmarks,
				 0,   "don't show marks"},
		{"exclude",		'x',  POPT_ARG_STRING, &re,
				'x',  "exclude a pattern", "re"},
		{"include",		'X',  POPT_ARG_STRING, &re,
				'X',  "include a pattern", "re"},
		{"mark-ignore-as-child",'C',  POPT_ARG_STRING, &re,
				'C',  "mark ignore as child", "re"},
		{"mark-stereo-bleed",	'B',  POPT_ARG_STRING, &re,
				'B',  "mark stereo bleed", "re"},
		{"re-ignore-case",	'\0', POPT_ARG_NONE, &progopts.reignorecase,
				 0,   "inore case in regular expressions"},
		{"internal-codeset",	'\0', POPT_ARG_STRING, &progopts.internalcodeset,
				'\0', "internal character encoding", "codeset-name"},
		{"filesystem-codeset",	'\0', POPT_ARG_STRING, &progopts.fscodeset,
				'\0', "filesystem character encoding", "codeset-name"},
		{"database-codeset",	'\0', POPT_ARG_STRING, &progopts.dbcodeset,
				'\0', "database character encoding", "codeset-name"},
		POPT_AUTOHELP
		POPT_TABLEEND
	};

	/* save program name */
	cp = strdup(argv[0]);
	if ((progopts.progname = strrchr(cp , '/')) == NULL)
		progopts.progname = cp;
	else
		progopts.progname++;

	optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
	poptSetOtherOptionHelp(optCon, "[options] <mp3 base directory> <fid base directory>");

	/* special options processing */
	while ((c = poptGetNextOpt(optCon)) >= 0)
	{
		switch (c)
		{
			case 'B':
				/* mark stereo bleed */
				addre(strdup(re), RE_STEREO_BLEED);
				break;
			case 'C':
				/* mark ignore as child */
				addre(strdup(re), RE_IGNORE_AS_CHILD);
				break;
			case 'q':
				/* quiet */
				progopts.showstages     = 0;
				progopts.showscantune   = 0;
				progopts.showstatistics = 0;
				progopts.showexclusions = 0;
				progopts.showmarks	= 0;
				break;
			case 'v':
				/* version */
				printf("%s version %s\n", progopts.progname, VERSION);
				exit(0);
				break;
			case 'x':
				/* exclude */
				addre(strdup(re), RE_EXCLUDE);
				break;
			case 'X':
				/* include */
				addre(strdup(re), RE_INCLUDE);
				break;
		}
	}

	if (c < -1)
	{
		/* an error occurred during option processing */
		fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
		usage(optCon, NULL);
	}

	switch (progopts.playerversion)
	{
		case -1:
			break;
		case 1:
			progopts.playerdbname  = "database";
			progopts.internalcodeset = "iso8859-1";
			progopts.olddirstruct  = 1;
			progopts.ignoreogg     = 1;
			progopts.ignoreflac    = 1;
			break;
		case 2:
			progopts.playerdbname  = "database";
			progopts.internalcodeset = "iso8859-1";
			progopts.olddirstruct  = 0;
			progopts.ignoreogg     = 1;
			progopts.ignoreflac    = 1;
			break;
		case 3:
			progopts.playerdbname  = "database3";
			progopts.internalcodeset = "utf-8";
			progopts.olddirstruct  = 0;
			progopts.ignoreogg     = 0;
			progopts.ignoreflac    = 0;
			break;
		default:
			usage(optCon, "player version must be 1, 2 or 3");
		break;
	}

	if ((progopts.dbcodeset != NULL) && (strcasecmp(progopts.internalcodeset, "utf-8") == 0))
		usage(optCon, "local database encoding not possible when using utf-8 internally");

	if (progopts.preferredcodec)
		parsepreferredcodec(progopts.preferredcodec, optCon);

	if ((progopts.drive2perc > 100) || (progopts.drive2perc < 0))
		usage(optCon, "percentage must be between 0 and 100");

	if ((progopts.mp3dir = poptGetArg(optCon)) == NULL)
		usage(optCon, "no arguments given");

	if ((progopts.fiddir = poptGetArg(optCon)) == NULL)
		usage(optCon, "not enough arguments");

	if (poptGetArg(optCon))
		usage(optCon, "too many arguments");

	poptFreeContext(optCon);
}

/* main routine */
int
main(const int argc, const char **argv)
{
	popt(argc, argv);
	getmp3stdev(progopts.mp3dir);
	initdata();
	mksubdirs();
	loaddb();
	loadfids();
	scanfids();
	cleanfids();
	markfids();
	checkfids();
	delfids();
	savefids();
	purgedb();
	finish();

	return 0;
}
