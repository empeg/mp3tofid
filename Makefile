CFLAGS = -O2 -g

PROGNAME = mp3tofid
LDLIBS   = -lmad -lid3 -lz
OBJECTS  = $(PROGNAME).o scanid3.o scanmp3.o

all: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	g++ -o $(PROGNAME) $(OBJECTS) $(LDLIBS)

mp3tofid.o: mp3tofid.cpp version.h
scanid3.o: scanid3.cpp id3genre.h

clean:
	rm -f *~ $(PROGNAME) *.o core
