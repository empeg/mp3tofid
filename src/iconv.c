#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <langinfo.h>
#include <iconv.h>
#include <inttypes.h>
#include <ctype.h>

#include "fids.h"
#include "mp3tofid.h"

#define	OUTBUFLEN	256

extern struct progopts	progopts;
extern struct cds	cds;


/* call iconv_open and check the outcome */
iconv_t
eiconv_open(const char *tocode, const char *fromcode)
{
	iconv_t	cd;

	if ((cd = iconv_open(tocode, fromcode)) == (iconv_t) -1)
	{
		fprintf(stderr, "%s: cannot convert from \"%s\" to \"%s\": %s\n",
			progopts.progname, fromcode, tocode, strerror(errno));
		exit(1);
	}

	return cd;
}
        
/* initialize character encoding conversions */
void
init_iconv()
{
	/* get the current locale */
	if (setlocale(LC_ALL, "") == NULL)
	{
		fprintf(stderr, "%s: setlocale() failed\n",
			progopts.progname);
		exit(1);
	}
			
	/* determin the filesystem codepage from the locale */
	if (progopts.fscodeset == NULL)
		progopts.fscodeset = nl_langinfo(CODESET);

	/* database codeset is always the same as the internal codeset
	   unless you've hacked the player */
	if (progopts.dbcodeset == NULL)
		progopts.dbcodeset = progopts.internalcodeset;
	else
		fprintf(stderr,
			"%s: warning! a non standard database encoding requires that you hack your player\n",
			progopts.progname);

	/* tell user what the internal codeset is */
	if (progopts.showstages)
	{
		printf("internal codeset   = %s\n", progopts.internalcodeset);
		printf("filesystem codeset = %s\n", progopts.fscodeset);
		printf("database codeset   = %s\n", progopts.dbcodeset);
	}

	cds.fstointernal    = eiconv_open(progopts.internalcodeset, progopts.fscodeset);
	cds.fstodb          = eiconv_open(progopts.dbcodeset, progopts.fscodeset);
	cds.utf16tointernal = eiconv_open(progopts.internalcodeset, "UTF-16");
	cds.utf8tointernal  = eiconv_open(progopts.internalcodeset, "UTF-8");
	cds.ucs4tointernal  = eiconv_open(progopts.internalcodeset, "UTF-32");
}

/* convert from a possibly multi-byte encoded string to a null-terminated native string */
char *
codesetconv(iconv_t cd, char *instring, size_t inlen)
{
	int	i;
	char	outstring[OUTBUFLEN];
	char	*frombuf;
	char	*tobuf;
	size_t	fromlen;
	size_t	tolen;
	size_t	nconv;

	/* initialize variables */
	frombuf = instring;
	tobuf   = outstring;
	fromlen = inlen < OUTBUFLEN ? inlen : OUTBUFLEN;
	tolen   = OUTBUFLEN - 1;
	memset(outstring, 0, OUTBUFLEN);

	/* reset iconv to initial state */
	iconv(cd, NULL, NULL, NULL, NULL);

	/* do the conversion */
	if ((nconv = iconv(cd, &frombuf, &fromlen, &tobuf, &tolen)) == (size_t) -1)
	{
		for (i=0; i<inlen; i++)
			if (!isprint(instring[i]))
				instring[i] = '.';
		fprintf(stderr, "%s: cannot convert \"%s\", length %ld: %s\n",
			progopts.progname, instring, inlen, strerror(errno));
		return strdup(instring);
	}
	else
		return strdup(outstring);
}

char *
fstointernal(char *string, size_t length)
{
	return codesetconv(cds.fstointernal, string, length);
}

char *
fstodb(char *string, size_t length)
{
	return codesetconv(cds.fstodb, string, length);
}

char *
utf16tointernal(char *string, size_t length)
{
	return codesetconv(cds.utf16tointernal, string, length);
}

char *
utf8tointernal(char *string)
{
	return codesetconv(cds.utf8tointernal, string, strlen(string));
}

char *
ucs4tointernal(char *string, size_t length)
{
	return codesetconv(cds.ucs4tointernal, string, length);
}

size_t
utf16len(char *string)
{
	uint16_t	*utf16char;
	size_t		i = 0;

	if (string == NULL)
		i = 0;
	else
		for (utf16char = (uint16_t *) string; *utf16char; utf16char++)
			i++;
	return i;
}
