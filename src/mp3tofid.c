#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <error.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "version.h"
#include "fids.h"
#include "mp3tofid.h"

/* program option related globals */
struct progopts
{
	char		*progname;	/* program name */
	char		*mp3dir;	/* mp3 base directory */
	char		*fiddir;	/* fids base directory */
	unsigned int	drive2perc;	/* percentage of fids to put in drive 2 */
	char		*debugoptions;	/* verbosity options */
	int		brokendrive2;	/* skip fids on drive 2 in playlists */
	int		intellisort;	/* sort "intelligently" */
	int		ignorecase;	/* ignore case in directory sorts */
	int		spreadplaylists;/* spread playlists over both drives */
} progopts = {NULL, NULL, NULL, 50, "sm", 0, 0, 0, 0};

/* data structure related globals */
struct fidinfo	*fihead;		/* head of linked list */
struct fidinfo	*fitail;		/* tail of linked list */
struct tagprop	tagprops[MAXTAGS];	/* properties of tags we know of */
unsigned int	ntagprops;		/* number of detected tag names */

/* prototypes */
extern void	getid3info(struct fidinfo *);
extern void	getmp3info(struct fidinfo *);


/* from emptool fids.h */
inline unsigned int GetFidNumber(FID fid)
{
    return (fid >> 4);
}

/* from emptool fids.h */
inline unsigned int GetFidType(FID fid)
{
    return (fid & 0xf);
}

/* from emptool fids.h */
inline FID MakeFid(unsigned int n, unsigned int t)
{
    return (n << 4) | (t & 0xf);
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
		fprintf(stderr, "%s: calloc() failed\n", progopts.progname);
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

	tagprops[TAG_TYPE_NUM].name		= TAG_TYPE_NAME;
	tagprops[TAG_ARTIST_NUM].name		= TAG_ARTIST_NAME;
	tagprops[TAG_BITRATE_NUM].name		= TAG_BITRATE_NAME;
	tagprops[TAG_CODEC_NUM].name		= TAG_CODEC_NAME;
	tagprops[TAG_COMMENT_NUM].name		= TAG_COMMENT_NAME;
	tagprops[TAG_CTIME_NUM].name		= TAG_CTIME_NAME;
	tagprops[TAG_DURATION_NUM].name		= TAG_DURATION_NAME;
	tagprops[TAG_FRAMES_NUM].name		= TAG_FRAMES_NAME;
	tagprops[TAG_GENRE_NUM].name		= TAG_GENRE_NAME;
	tagprops[TAG_LENGTH_NUM].name		= TAG_LENGTH_NAME;
	tagprops[TAG_LOADFROM_NUM].name		= TAG_LOADFROM_NAME;
	tagprops[TAG_OFFSET_NUM].name		= TAG_OFFSET_NAME;
	tagprops[TAG_SAMPLERATE_NUM].name	= TAG_SAMPLERATE_NAME;
	tagprops[TAG_SOURCE_NUM].name		= TAG_SOURCE_NAME;
	tagprops[TAG_TITLE_NUM].name		= TAG_TITLE_NAME;
	tagprops[TAG_TRACKNR_NUM].name		= TAG_TRACKNR_NAME;
	tagprops[TAG_YEAR_NUM].name		= TAG_YEAR_NAME;

	tagprops[TAG_TYPE_NUM].skipdb		= TAG_TYPE_SKIPDB;
	tagprops[TAG_ARTIST_NUM].skipdb		= TAG_ARTIST_SKIPDB;
	tagprops[TAG_BITRATE_NUM].skipdb	= TAG_BITRATE_SKIPDB;
	tagprops[TAG_CODEC_NUM].skipdb		= TAG_CODEC_SKIPDB;
	tagprops[TAG_COMMENT_NUM].skipdb	= TAG_COMMENT_SKIPDB;
	tagprops[TAG_CTIME_NUM].skipdb		= TAG_CTIME_SKIPDB;
	tagprops[TAG_DURATION_NUM].skipdb	= TAG_DURATION_SKIPDB;
	tagprops[TAG_FRAMES_NUM].skipdb		= TAG_FRAMES_SKIPDB;
	tagprops[TAG_GENRE_NUM].skipdb		= TAG_GENRE_SKIPDB;
	tagprops[TAG_LENGTH_NUM].skipdb		= TAG_LENGTH_SKIPDB;
	tagprops[TAG_LOADFROM_NUM].skipdb	= TAG_LOADFROM_SKIPDB;
	tagprops[TAG_OFFSET_NUM].skipdb		= TAG_OFFSET_SKIPDB;
	tagprops[TAG_SAMPLERATE_NUM].skipdb	= TAG_SAMPLERATE_SKIPDB;
	tagprops[TAG_SOURCE_NUM].skipdb		= TAG_SOURCE_SKIPDB;
	tagprops[TAG_TITLE_NUM].skipdb		= TAG_TITLE_SKIPDB;
	tagprops[TAG_TRACKNR_NUM].skipdb	= TAG_TRACKNR_SKIPDB;
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
}

/* check whether path is a symlink */
int
islink(char *path)
{
	struct stat	statbuf;

	return ((lstat(path, &statbuf) == 0) && (S_ISLNK(statbuf.st_mode)));
}

/* create subdirectory in fids tree */
void
mkfidsubdir(char *subdir)
{
	char fidsubdir[PATH_MAX];

	sprintf(fidsubdir, "%s/%s", progopts.fiddir, subdir);
	if ((mkdir(fidsubdir, 0777) < 0) && (errno != EEXIST))
	{
		fprintf(stderr, "%s: can not create directory '%s': %s\n",
				progopts.progname, fidsubdir, strerror(errno));
		exit(1);
	}
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

	fid = strtoul(stripdirfrompath(path), &endptr, 16);
	if (*endptr)
		return 0;	
	else
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

/* calculate fid number from inode number */
unsigned int
inodetofidnumber(ino_t inode)
{
	return (inode + GetFidNumber(FID_FIRSTNORMAL));
}

/* build full path to fid from fid number and type */
char *
fidnumbertofidpath(unsigned int fidnumber, unsigned int fidtype, int tagtype)
{
	char fidpath[PATH_MAX];

	sprintf(fidpath, "%s/drive%u/fids/%x",
		progopts.fiddir, fidnumbertodrive(fidnumber, tagtype),
		MakeFid(fidnumber, fidtype));
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

/* scan an mp3 file for its properties */
void
mp3scan(char *mp3path, struct stat *statbuf, unsigned int fidnumber)
{
	char		**tagvalues;
	struct fidinfo	*fidinfo;
	char		tagvalue[MAXTAGLEN+1];

	/* check whether this mp3 info is cached */
	if (searchfidinfo(fidnumber))
	{
		if (strchr(progopts.debugoptions, 'M'))
			printf("no need to scan %s\n", mp3path);
		return;
	}

	if (strchr(progopts.debugoptions, 'm'))
		printf("scanning %s\n", mp3path);

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
	tagvalues[TAG_CODEC_NUM]    = "mp3";
	tagvalues[TAG_LOADFROM_NUM] = mp3path;
	sprintf(tagvalue, "%lu", statbuf->st_ctime);
	tagvalues[TAG_CTIME_NUM]    = strdup(tagvalue);
	sprintf(tagvalue, "%lu", statbuf->st_size);
	tagvalues[TAG_LENGTH_NUM]   = strdup(tagvalue);

	fidinfo->fidnumber   = fidnumber;
	fidinfo->tagtype     = TAG_TYPE_TUNE;
	fidinfo->tagvalues   = tagvalues;
	fidinfo->ntagvalues  = ntagprops;
	fidinfo->sourcemtime = statbuf->st_mtime;

	getmp3info(fidinfo);
	getid3info(fidinfo);
}

/* check whether string ends with <end> */
int
endswith(const char *string, const char *end)
{
	return (strcmp(string + strlen(string) - strlen(end), end) == 0);
}

/* check whether string begins with <begin> */
int
beginswith(const char *string, const char *begin)
{
	return (strncmp(string, begin, strlen(begin)) == 0);
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

	if (progopts.ignorecase)
		return strcasecmp(name[0] + offset[0], name[1] + offset[1]);
	else
		return strcmp(name[0] + offset[0], name[1] + offset[1]);
}


/* recursively scan a directory for MP3's */
int
dirscan(char *dir, unsigned int parentfidnumber, char *title)
{
	int		i;
	unsigned int	n;
	int		addfid;
	FID		*fids;
	int		nfids = 0;
	char		*childpath;
	unsigned int	childfidnumber = 0;
	struct dirent	**namelist;
	struct stat	statbuf;
	struct fidinfo	*fidinfo;
	char		**tagvalues;
	char		tagvalue[MAXTAGLEN+1];

	/* show what's going on */
	if (strchr(progopts.debugoptions, 'd'))
		printf("scanning %s\n", dir);

	/* count and sort directory entries */
	if ((n = scandir(dir, &namelist, NULL, direntcompar)) < 0)
	{

		fprintf(stderr, "%s: cannot scandir '%s': %s\n",
				progopts.progname, dir, strerror(errno));
		exit(1);
	}

	/* allocate playlist */
	fids = (FID *) ecalloc(n, sizeof(FID));

	/* check each directory entry */
	for (i=0; i<n; i++)
	{
		if (strcmp(namelist[i]->d_name, ".")  == 0)
			continue;
		if (strcmp(namelist[i]->d_name, "..") == 0)
			continue;

		/* build full pathname to directory entry */
		childpath = (char *) emalloc(strlen(dir) + namelist[i]->d_reclen + 2);
		sprintf(childpath, "%s/%s", dir, namelist[i]->d_name);

		addfid = 0;
		if (stat(childpath, &statbuf) == 0)
		{
			childfidnumber = inodetofidnumber(statbuf.st_ino);

			/* if it's a symlink, just add it to the playlist */
			if (islink(childpath))
				addfid = checklink(childpath);
			else
			{
				/* recurse into directories */
				if (S_ISDIR(statbuf.st_mode))
				{
					if (dirscan(childpath, childfidnumber,
							stripdirfrompath(childpath)))
						addfid++;
				}
				/* everything else is an mp3 or ignored */
				else if (endswith(childpath, ".mp3"))
				{
					mp3scan(childpath, &statbuf, childfidnumber);
					if ((progopts.brokendrive2 == 0) ||
							(fidnumbertodrive(childfidnumber,
								TAG_TYPE_TUNE) == 0))
						addfid++;
				}
			}
		}
		else
			fprintf(stderr, "%s: can not stat '%s': %s\n",
						progopts.progname, childpath, strerror(errno));

		/* only add non-empty directories and mp3's */
		if (addfid)
			fids[nfids++] = MakeFid(childfidnumber, FIDTYPE_TUNE);

		free(namelist[i]);
	}
	free(namelist);

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
	tagvalues[TAG_TITLE_NUM]    = title;
	tagvalues[TAG_LOADFROM_NUM] = dir;
	stat(dir, &statbuf);
	sprintf(tagvalue, "%lu", statbuf.st_ctime);
	tagvalues[TAG_CTIME_NUM]    = strdup(tagvalue);
	sprintf(tagvalue, "%d", nfids * sizeof(FID));
	tagvalues[TAG_LENGTH_NUM]   = strdup(tagvalue);

	fidinfo->fidnumber   = parentfidnumber;
	fidinfo->tagtype     = TAG_TYPE_PLAYLIST;
	fidinfo->tagvalues   = tagvalues;
	fidinfo->ntagvalues  = ntagprops;
	fidinfo->playlist    = fids;
	fidinfo->pllength    = nfids;

	return (nfids);
}

/* get fidinfo from an existing fid */
int
fidpathtofidinfo(struct fidinfo *fidinfo, char *fidpath)
{
	int		i;
	int		reasonstoinvalidate = 0;
	FILE		*fp;
	struct stat	fidstatbuf;
	struct stat	mp3statbuf;
	char		*tok;
	char		*fidbuf;
	char		*fidname;
	FID		fid;
	size_t		fidsize;
	char		**tagvalues;
	int		ntagvalues;
	unsigned int	taglen;

	/* strip directory from path */
	fidname = stripdirfrompath(fidpath);

	/* convert fidname to FID */
	if ((fid = fidpathtofid(fidpath)) == 0)
		return 0;	

	/* only tag fids */
	if (GetFidType(fid) != FIDTYPE_TAGS)
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

	/* check lenghth stored in fid */
	if (mp3statbuf.st_size != atol(tagvalues[TAG_LENGTH_NUM]))
		reasonstoinvalidate++;

	/* check fid value */
	if (inodetofidnumber(mp3statbuf.st_ino) != GetFidNumber(fid))
		reasonstoinvalidate++;

	if (reasonstoinvalidate)
	{
		free(tagvalues);
		free(fidbuf);
		return 0;
	}

	/* prepare data for return */
	fidinfo->fidnumber   = GetFidNumber(fid);
	fidinfo->tagtype     = TAG_TYPE_TUNE;
	fidinfo->sourcemtime = mp3statbuf.st_mtime;
	fidinfo->tagvalues   = tagvalues;
	fidinfo->ntagvalues  = ntagvalues;

	return 1;
}

/* store valid tune tags into memory in order to prevent rescanning
 * the corresponding mp3's */
void
loadfids()
{
	int		i;
	char		dirpath[PATH_MAX];
	char		fidpath[PATH_MAX];
	DIR		*dirstream;
	struct dirent	*dirent;
	struct fidinfo	*fidinfo;

	if (strchr(progopts.debugoptions, 's'))
		puts("loading existings fids");

	for (i=0; i<2; i++)
	{
		sprintf(dirpath, "%s/drive%d/fids", progopts.fiddir, i);
		if ((dirstream = opendir(dirpath)) == NULL)
			continue;
		while ((dirent = readdir(dirstream)) != NULL)
		{
			sprintf(fidpath, "%s/%s", dirpath, dirent->d_name);

			fidinfo = fitail;
			if (fidinfo->ntagvalues)
			{
				fidinfo->next = allocfidinfo();
				fidinfo = fidinfo->next;
				fitail = fidinfo;
			}
			fidpathtofidinfo(fidinfo, fidpath);
		}
		closedir(dirstream);
	}
}

/* delete everything from the fids directories */
void
delfids()
{
	int		i;
	int		showremove = 0;
	char		dirpath[PATH_MAX];
	char		fidpath[PATH_MAX];
	DIR		*dirstream;
	struct dirent	*dirent;

	if (strchr(progopts.debugoptions, 's'))
		puts("deleting existing fids");

	if (strchr(progopts.debugoptions, 'r'))
		showremove++;

	for (i=0; i<2; i++)
	{
		sprintf(dirpath, "%s/drive%d/fids", progopts.fiddir, i);
		if ((dirstream = opendir(dirpath)) == NULL)
			continue;
		while ((dirent = readdir(dirstream)) != NULL)
		{
			sprintf(fidpath, "%s/%s", dirpath, dirent->d_name);
			if (showremove)
				printf("unlinking %s\n", fidpath);
			unlink(fidpath);
		}
		closedir(dirstream);
	}
}

/* print a usage message and quit */
void
usage(int exitstatus)
{
	FILE	*fp;

	fp = exitstatus ? stderr : stdout;

	fprintf(fp, "usage: %s [options] <mp3 base directory> <fid base directory>\n",
			progopts.progname);
	fprintf(fp, "\toptions:\n");
	fprintf(fp, "\t\t-v: show version\n");
	fprintf(fp, "\t\t-h: show this help\n");
	fprintf(fp, "\t\t-b: skips fids on drive 2 in playlists\n");
	fprintf(fp, "\t\t-i: ignore case in directory sorting\n");
	fprintf(fp, "\t\t-I: \"intelligent\" directory sorting\n");
	fprintf(fp, "\t\t-2: percentage of files on second drive\n");
	fprintf(fp, "\t\t-s: spread playlists over both drives\n");
	fprintf(fp, "\t\t-d: debug options\n");
	fprintf(fp, "\tdebug options:\n");
	fprintf(fp, "\t\ts: show stages of program\n");
	fprintf(fp, "\t\tr: show fids being removed\n");
	fprintf(fp, "\t\td: show directories being scanned\n");
	fprintf(fp, "\t\tm: show MP3's being scanned\n");
	fprintf(fp, "\t\tM: show MP3's being skipped\n");
	exit (exitstatus);
}

/* create the necessary subdirectories in fid tree */
void
mkfidsubdirs()
{
	if (strchr(progopts.debugoptions, 's'))
		puts("creating subdirectories");
	mkfidsubdir("drive0");
	mkfidsubdir("drive0/fids");
	mkfidsubdir("drive0/var");
	mkfidsubdir("drive1");
	mkfidsubdir("drive1/fids");
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
	{
		if (fidnumbera > fidnumberb)
			return 1;
		else
			return -1;
	}
}

void
savefids()
{
	int		i, j;
	int		lastfidnumber = 0;
	unsigned int	nfids = 0;
	struct fidinfo	*fidinfo;
	struct fidinfo	**fidinfoarray;
	struct utimbuf	utimbuf;
	FID		fid;
	FILE		*fp;
	FILE		*fpdatabase;
	FILE		*fpplaylists;
	char		path[PATH_MAX];
	char		*fidpath;

	/* show what's going on */
	if (strchr(progopts.debugoptions, 's'))
		puts("saving data");

	/* open database files */
	sprintf(path, "%s/drive0/var/database",  progopts.fiddir);
	fpdatabase  = efopen(path, "w");
	sprintf(path, "%s/drive0/var/playlists", progopts.fiddir);
	fpplaylists = efopen(path, "w");
	sprintf(path, "%s/drive0/var/tags",      progopts.fiddir);
	fp          = efopen(path, "w");

	/* write tags file */
	for (i=0; i<MAXTAGS; i++)
		fprintf(fp, "%s\n", tagprops[i].name ? tagprops[i].name : "");
	fclose(fp);

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

		if (fidinfo->tagtype == TAG_TYPE_PLAYLIST)
		{
			/* create the playlist "tune" fid */
			fp = efopen(fidpath, "w");
			for (j=0; j<fidinfo->pllength; j++)
			{
				fid = fidinfo->playlist[j];
				fwrite(&fid, sizeof(fid), 1, fp);
				fwrite(&fid, sizeof(fid), 1, fpplaylists);
			}
			fclose(fp);

		}
		else if (fidinfo->tagtype == TAG_TYPE_TUNE)
		{
			/* create symlink from mp3 file to fid */
			if (symlink(fidinfo->tagvalues[TAG_LOADFROM_NUM], fidpath) < 0)
				fprintf(stderr, "%s: cannot symlink '%s' to '%s': %s\n",
						progopts.progname, fidinfo->tagvalues[TAG_LOADFROM_NUM],
						fidpath, strerror(errno));
		}

		/* create the tag fid */
		if ((fidinfo->tagtype == TAG_TYPE_PLAYLIST) ||
				(fidinfo->tagtype == TAG_TYPE_TUNE))
		{
			fidpath = fidnumbertofidpath(fidinfo->fidnumber, FIDTYPE_TAGS,
					fidinfo->tagtype);
			fp = efopen(fidpath, "w");
			for (j=0; j<fidinfo->ntagvalues; j++)
				if (fidinfo->tagvalues[j])
					fprintf(fp, "%s=%s\n", tagprops[j].name,
							fidinfo->tagvalues[j]);
			fclose(fp);
		}

		/* set the tag fid's mtime; makes things easy for rsync and ourselves */
		if (fidinfo->sourcemtime)
		{
			utimbuf.actime  = fidinfo->sourcemtime;
			utimbuf.modtime = fidinfo->sourcemtime;
			utime(fidpath, &utimbuf);
		}

		/* fill up empty database entries */
		for (j=lastfidnumber; j<fidinfo->fidnumber; j++)
			fputc(0xff, fpdatabase);

		/* append entry to database */
		for (j=0; j<fidinfo->ntagvalues; j++)
			if ((fidinfo->tagvalues[j]) && !tagprops[j].skipdb)
				fprintf(fpdatabase, "%c%c%s",
						j,
						strlen(fidinfo->tagvalues[j]),
						fidinfo->tagvalues[j]);

		/* terminate entry */
		fputc(0xff, fpdatabase);

		lastfidnumber = fidinfo->fidnumber + 1;
	}

	/* close database files */
	fclose(fpdatabase);
	fclose(fpplaylists);
}

/* scan mp3 tree */
void
scanfids()
{
	if (strchr(progopts.debugoptions, 's'))
		puts("scanning MP3 directories");
	dirscan(progopts.mp3dir, GetFidNumber(FID_ROOTPLAYLIST) , "All Music");
}

/* main routine */
int
main(int argc, char **argv)
{
	int	c;

	if ((progopts.progname = strrchr(argv[0], '/')) == NULL)
		progopts.progname = argv[0];
	else
		progopts.progname++;

	while ((c = getopt(argc, argv, "2:bd:hiIsv")) != EOF)
	{
		switch (c)
		{
			case '2':
				progopts.drive2perc = atoi(optarg);
				if ((progopts.drive2perc > 100) || (progopts.drive2perc < 0))
					usage(1);
				break;
			case 'b':
				progopts.brokendrive2++;
				break;
			case 'd':
				progopts.debugoptions = optarg;
				break;
			case 'h':
				usage(0);
				break;
			case 'i':
				progopts.ignorecase++;
				break;
			case 'I':
				progopts.intellisort++;
				break;
			case 's':
				progopts.spreadplaylists++;
				break;
			case 'v':
				printf("%s version %s\n", progopts.progname, VERSION);
				exit(0);
			default:
				usage(1);
		}
	}

	if ((argc - optind) != 2)
		usage(1);

	progopts.mp3dir = argv[optind];
	progopts.fiddir = argv[optind+1];

	initdata();
	mkfidsubdirs();
	loadfids();
	scanfids();
	delfids();
	savefids();

	return 0;
}
