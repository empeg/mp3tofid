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


char	*progname;
char	*mp3dir;
char	*fiddir;
int	drive2perc = 50;
char	*debugoptions = "sm";
int	intellisort = 0;
int	ignorecase  = 0;

int
islink(char *path)
{
	struct stat	statbuf;

	return ((lstat(path, &statbuf) == 0) && (S_ISLNK(statbuf.st_mode)));
}

void
mkfidsubdir(char *subdir)
{
	char fidsubdir[PATH_MAX];

	sprintf(fidsubdir, "%s/%s", fiddir, subdir);
	if ((mkdir(fidsubdir, 0777) < 0) && (errno != EEXIST))
	{
		fprintf(stderr, "%s: can not create directory '%s': %s\n",
				progname, fidsubdir, strerror(errno));
		exit(1);
	}
}

int
fidbasetodrive(char *fidbase)
{
	return ((strtol(fidbase, NULL, 16) % 100) > (100 - drive2perc));
}

long
inodetoshiftfid(ino_t inode)
{
	return (inode + 256) << 4;
}

char *
inodetofidbase(ino_t inode)
{
	char	fidbase[8];

	sprintf(fidbase, "%x", inode + 256);
	return strdup(fidbase);
}

char *
fidbasetofidpath(char *fidbase, int fidtype)
{
	char fidpath[PATH_MAX];

	sprintf(fidpath, "%s/drive%d/fids/%s%d",
		fiddir, fidbasetodrive(fidbase), fidbase, fidtype);
	return strdup(fidpath);
}


void
mp3tofid(char *mp3path, struct stat *statbuf, char *fidbase)
{
	char		*fidpath;
	struct utimbuf	utimbuf;
	FILE		*fp;
	void		writeid3info(FILE *, char *);
	void		writemp3info(FILE *, char *);

	fidpath = fidbasetofidpath(fidbase, 0);
	if (symlink(mp3path, fidpath) < 0)
		fprintf(stderr, "%s: cannot symlink '%s' to '%s': %s\n",
				progname, mp3path, fidpath, strerror(errno));

	fidpath = fidbasetofidpath(fidbase, 1);

	if (access(fidpath, R_OK) == 0)
	{
		if (strchr(debugoptions, 'M'))
			printf("no need to scan %s\n", mp3path);
		return;
	}

	if (strchr(debugoptions, 'm'))
		printf("scanning %s\n", mp3path);

	if ((fp = fopen(fidpath, "w")) == NULL)
	{
		fprintf(stderr, "%s: cannot open '%s': %s\n",
				progname, fidpath, strerror(errno));
		exit(1);
	}

	fprintf(fp, "type=%s\n",     "tune");
	fprintf(fp, "codec=%s\n",    "mp3");
	fprintf(fp, "ctime=%d\n",    statbuf->st_ctime);
	fprintf(fp, "length=%d\n",   statbuf->st_size);
	fprintf(fp, "loadfrom=%s\n", mp3path);
	writemp3info(fp, mp3path);
	writeid3info(fp, mp3path);
	fclose(fp);
	utimbuf.actime  = statbuf->st_mtime;
	utimbuf.modtime = statbuf->st_mtime;
	utime(fidpath, &utimbuf);
}

int
endswith(const char *string, const char *end)
{
	return (strcmp(string + strlen(string) - strlen(end), end) == 0);
}

int
beginswith(const char *string, const char *begin)
{
	return (strncmp(string, begin, strlen(begin)) == 0);
}

int
checklink(char *linkpath)
{
	FILE *fp;
	char targetpath[PATH_MAX];

	memset(targetpath, 0, PATH_MAX / sizeof(int));

	if (readlink(linkpath, targetpath, PATH_MAX) < 0)
	{
		fprintf(stderr, "%s: can not read symbolic link '%s': %s\n",
				progname, linkpath, strerror(errno));
		return 0;
	}

	if ((fp = fopen(linkpath, "r")) == NULL)
	{
		fprintf(stderr,
			"%s: '%s' is a symbolic link to '%s' which can not be opened: %s\n",
				progname, linkpath, targetpath, strerror(errno));
		return 0;
	}
	else
		fclose(fp);

	return 1;
}

int
direntcompare(const struct dirent **a, const struct dirent **b)
{
	int			i;
	int			offset[2] = {0, 0};
	const char		*name[2];
	const struct dirent	*entarray[2];

	entarray[0] = *a;
	entarray[1] = *b;


	for (i=0; i<2; i++)
	{
		name[i] = entarray[i]->d_name;

		if (intellisort)
		{
			/* A Flock of Seagulls */
			if (beginswith(name[i], "A "))
				offset[i] = 2;
			else
			/* An Emotional Fish */
			if (beginswith(name[i], "An "))
				offset[i] = 3;
			else
			/* Uhm, The Beatles ? */
			if (beginswith(name[i], "The "))
				offset[i] = 4;
		}
	}

	if (ignorecase)
		return strcasecmp(name[0] + offset[0], name[1] + offset[1]);
	else
		return strcmp(name[0] + offset[0], name[1] + offset[1]);
}


int
dirtofid(char *dir, char *parentfidbase, char *title)
{
	int		i, n;
	int		hasitems = 0;
	int		additem;
	int		shiftfid;
	char		*childpath;
	char		*childfidbase;
	char		*fidpath;
	FILE		*fp;
	struct dirent	**namelist;
	struct stat	statbuf;
	struct utimbuf	utimbuf;
	int		(*compar)(const struct dirent **, const struct dirent **) =
				direntcompare;

	if (strchr(debugoptions, 'd'))
		printf("scanning %s\n", dir);

	if ((n = scandir(dir, &namelist, NULL,
		(int (*) (const void *, const void *))compar)) < 0)
	{
		fprintf(stderr, "%s: cannot scandir '%s': %s\n",
				progname, dir, strerror(errno));
		exit(1);
	}

	fidpath = fidbasetofidpath(parentfidbase, 0);
	if ((fp = fopen(fidpath, "w")) == NULL)
	{
		fprintf(stderr, "%s: cannot open '%s': %s\n",
				progname, fidpath, strerror(errno));
		exit(1);
	}

	for (i=0; i<n; i++)
	{
		if (strcmp(namelist[i]->d_name, ".")  == 0)
			continue;
		if (strcmp(namelist[i]->d_name, "..") == 0)
			continue;

		childpath = (char *) malloc(strlen(dir) + namelist[i]->d_reclen + 2);
		sprintf(childpath, "%s/%s", dir, namelist[i]->d_name);

		additem = 0;


		if (stat(childpath, &statbuf) == 0)
		{
			if (islink(childpath))
				additem = checklink(childpath);
			else
			{
				childfidbase = inodetofidbase(statbuf.st_ino);
				if (S_ISDIR(statbuf.st_mode))
				{
					if (dirtofid(childpath, childfidbase,
								namelist[i]->d_name))
						additem++;
				}
				else if (endswith(childpath, ".mp3"))
				{
					mp3tofid(childpath, &statbuf, childfidbase);
					additem++;
				}
			}
		}
		else
			fprintf(stderr, "%s: can not stat '%s': %s\n",
						progname, childpath, strerror(errno));


		if (additem)
		{
			shiftfid = inodetoshiftfid(statbuf.st_ino);
			fwrite(&shiftfid, sizeof(shiftfid), 1, fp);
			hasitems++;
		}

		free(childpath);
		free(namelist[i]);
	}
	free(namelist);
	fclose(fp);

	if (stat(fidpath, &statbuf) < 0)
	{
		fprintf(stderr, "%s: cannot stat '%s': %s\n",
				progname, dir, strerror(errno));
		exit(1);
	}
	utimbuf.actime  = statbuf.st_mtime;
	utimbuf.modtime = statbuf.st_mtime;

	if (hasitems == 0)
	{
		fprintf(stderr, "%s: unlinking empty playlist %s: %s\n",
				progname, fidpath, dir);
		unlink(fidpath);
	}
	else
	{
		fidpath = fidbasetofidpath(parentfidbase, 1);
		if ((fp = fopen(fidpath, "w")) == NULL)
		{
			fprintf(stderr, "cannot open '%s': %s\n",
					fidpath, strerror(errno));
			exit(1);
		}
		fprintf(fp, "type=playlist\n");
		fprintf(fp, "ctime=%d\n",  statbuf.st_ctime);
		fprintf(fp, "length=%d\n", statbuf.st_size);
		fprintf(fp, "title=%s\n",  title);
		fclose(fp);
		utime(fidpath, &utimbuf);
	}

	return (hasitems);
}

int
shoulddelete(char *fidpath)
{
	int		reasonstodelete = 0;
	FILE		*fp;
	struct stat	fidstatbuf;
	struct stat	mp3statbuf;
	char		*tok;
	char		*fidbuf;
	off_t		fidsize;
	off_t		toklength = 0;
	time_t		tokctime = 0;
	char		*tokloadfrom = NULL;
	char		*toktype = NULL;

	if (!endswith(fidpath, "1"))
		return 1;

	if (islink(fidpath))
		return 1;

	if (stat(fidpath, &fidstatbuf) < 0)
		return 1;

	fidsize = fidstatbuf.st_size;

	if ((fp = fopen(fidpath, "r")) == NULL)
		return 1;

	if ((fidbuf = (char *) malloc(fidsize + 1)) == NULL)
		return 1;

	if (fread(fidbuf, sizeof(char), fidsize, fp) != fidsize)
	{
		free(fidbuf);
		return 1;
	}
	fclose(fp);

	fidbuf[fidsize] = '\0';

	for (tok = strtok(fidbuf, "\n"); tok; tok = strtok(NULL, "\n"))
	{
		if (beginswith(tok, "type="))
			toktype     = tok + 5 ;
		if (beginswith(tok, "loadfrom="))
			tokloadfrom = tok + 9;
		if (beginswith(tok, "ctime="))
			tokctime    = atol(tok + 6);
		if (beginswith(tok, "length="))
			toklength   = atol(tok + 7);
	}
	if (!beginswith(toktype, "tune"))
		reasonstodelete++;
	if (tokctime == 0)
		reasonstodelete++;
	if (toklength == 0)
		reasonstodelete++;
	if (tokloadfrom == NULL)
		reasonstodelete++;

	if (reasonstodelete)
	{
		free(fidbuf);
		return reasonstodelete;
	}

	if (stat(tokloadfrom, &mp3statbuf) < 0)
	{
		free(fidbuf);
		return 1;
	}

	if (mp3statbuf.st_mtime != fidstatbuf.st_mtime)
		reasonstodelete++;
	if (mp3statbuf.st_ctime != tokctime)
		reasonstodelete++;
	if (mp3statbuf.st_size  != toklength)
		reasonstodelete++;
	free(fidbuf);

	if (strcmp(fidbasetofidpath(inodetofidbase(mp3statbuf.st_ino), 1), fidpath) != 0)
		reasonstodelete++;

	return reasonstodelete;
}

void
cleanfids()
{
	int		i;
	char		dirpath[PATH_MAX];
	char		fidpath[PATH_MAX];
	DIR		*dirstream;
	struct dirent	*dirent;

	if (strchr(debugoptions, 's'))
		puts("deleting old fid files");

	for (i=0; i<2; i++)
	{
		sprintf(dirpath, "%s/drive%d/fids", fiddir, i);
		if ((dirstream = opendir(dirpath)) == NULL)
			continue;
		while ((dirent = readdir(dirstream)) != NULL)
		{
			sprintf(fidpath, "%s/%s", dirpath, dirent->d_name);
			if (shoulddelete(fidpath))
			{
				if (strchr(debugoptions, 'r'))
					printf("removing %s\n", fidpath);
				unlink(fidpath);
			}
		}
		closedir(dirstream);
	}
}

void
usage(int exitstatus)
{
	FILE	*fp;

	fp = exitstatus ? stderr : stdout;

	fprintf(fp, "usage: %s [options] <mp3 base directory> <fid base directory>\n",
			progname);
	fprintf(fp, "\toptions:\n");
	fprintf(fp, "\t\t-v: show version\n");
	fprintf(fp, "\t\t-h: show this help\n");
	fprintf(fp, "\t\t-i: ignore case in directory sorting\n");
	fprintf(fp, "\t\t-I: \"intelligent\" directory sorting\n");
	fprintf(fp, "\t\t-2: percentage of files on second drive\n");
	fprintf(fp, "\t\t-d: debug options\n");
	fprintf(fp, "\tdebug options:\n");
	fprintf(fp, "\t\ts: show stages of program\n");
	fprintf(fp, "\t\tr: show fids being removed\n");
	fprintf(fp, "\t\td: show directories being scanned\n");
	fprintf(fp, "\t\tm: show MP3's being scanned\n");
	fprintf(fp, "\t\tM: show MP3's being skipped\n");
	exit (exitstatus);
}

void
mkfidsubdirs()
{
	if (strchr(debugoptions, 's'))
		puts("creating subdirectories");
	mkfidsubdir("drive0");
	mkfidsubdir("drive0/fids");
	mkfidsubdir("drive1");
	mkfidsubdir("drive1/fids");
}

void
checkfids()
{
	if (strchr(debugoptions, 's'))
		puts("checking fid consistency");
	/* TODO */
}

int
main(int argc, char **argv)
{
	int	c;

	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

	while ((c = getopt(argc, argv, "2:d:hiIv")) != EOF)
	{
		switch (c)
		{
			case '2':
				drive2perc = atoi(optarg);
				if ((drive2perc > 100) || (drive2perc < 0))
					usage(1);
				break;
			case 'd':
				debugoptions = optarg;
				break;
			case 'h':
				usage(0);
				break;
			case 'i':
				ignorecase++;
				break;
			case 'I':
				intellisort++;
				break;
			case 'v':
				printf("%s version %s\n", progname, VERSION);
				exit(0);
			default:
				usage(1);
		}
	}

	if ((argc - optind) != 2)
		usage(1);

	mp3dir = argv[optind];
	fiddir = argv[optind+1];

	mkfidsubdirs();
	cleanfids();
	if (strchr(debugoptions, 's'))
		puts("scanning MP3 directories");
	dirtofid(mp3dir, "10", "All Music");
	checkfids();

	return 0;
}
