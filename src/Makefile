CFLAGS = -O2 -g

# Theses options are used by the author during development. Comment
# them if you're not using gcc.
CFLAGS += \
-Wall 			\
-Wpointer-arith 	\
-Wbad-function-cast 	\
-Wcast-align 		\
-Wnested-externs	\
#-Wconversion		\
#-pedantic		\
#-Wcast-qual 		\

PROGNAME = mp3tofid
PROGNAMESTATIC = $(PROGNAME).static
LDLIBS   = -lpopt -lgdbm -lcrypto -lmad -lid3tag -lvorbisfile -lvorbis -logg -lsndfile -lFLAC -lz -lm #-lefence
LDFLAGS  = -g 
LDFLAGSSTATIC  = $(LDFLAGS) -static
OBJECTS  = $(PROGNAME).o scanmp3.o scanwave.o scanwma.o scanvorbis.o scanflac.o iconv.o
BINDIR   = /usr/local/bin

all: $(PROGNAME) # $(PROGNAMESTATIC)

$(PROGNAME): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $(PROGNAME) $(OBJECTS) $(LDLIBS)

$(PROGNAMESTATIC): $(OBJECTS)
	$(CC) $(LDFLAGSSTATIC) -o $(PROGNAMESTATIC) $(OBJECTS) $(LDLIBS)

# dependencies
mp3tofid.o: version.h fids.h mp3tofid.h mp3tofid.c
scanmp3.o: fids.h mp3tofid.h scanmp3.c
scanvorbis.o: mp3tofid.h scanvorbis.c
scanwave.o: fids.h mp3tofid.h scanwave.c
scanwma.o: config.h asf.h bswap.h fids.h mp3tofid.h scanwma.c

clean:
	rm -f *~ $(PROGNAME) $(PROGNAMESTATIC) *.o core core.* *.exe *.stackdump
	chmod 644 *.c *.h

install: $(PROGNAME)
	cp $(PROGNAME) $(BINDIR)/$(PROGNAME)
