CFLAGS = -O2

# Theses options are used by the author during development. Comment
# them if you're not using gcc.
CFLAGS += \
-Wall 			\
-Wpointer-arith 	\
-Wbad-function-cast 	\
-Wcast-align 		\
-Wconversion		\
-Wnested-externs
#-pedantic		\
#-Wcast-qual 		\

PROGNAME = mp3tofid
LDLIBS   = -lmad -lid3tag -lz
LDFLAGS  = -s
OBJECTS  = $(PROGNAME).o scanid3.o scanmp3.o
BINDIR   = /usr/local/bin

all: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $(PROGNAME) $(OBJECTS) $(LDLIBS)

mp3tofid.o: mp3tofid.c mp3tofid.h fids.h version.h
scanid3.o: scanid3.c mp3tofid.h fids.h
scanmp3.o: scanmp3.c mp3tofid.h fids.h

clean:
	rm -f *~ $(PROGNAME) *.o core
	chmod 644 *.c *.h

install: $(PROGNAME)
	cp $(PROGNAME) $(BINDIR)/$(PROGNAME)
