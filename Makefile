
EXES= scmtiff scmview

#-------------------------------------------------------------------------------

#CC = cc -std=c99 -m64 -g
CC = gcc-mp-4.4 -std=c99 -m64 -fopenmp -O3
CP = cp
RM = rm -f

CFLAGS = -g -Wall

ifneq ($(wildcard /usr/local),)
	CFLAGS += -I/usr/local/include
	LFLAGS += -L/usr/local/lib
endif

ifneq ($(wildcard /opt/local),)
	CFLAGS += -I/opt/local/include
	LFLAGS += -L/opt/local/lib
endif

%.o : %.c Makefile
	$(CC) $(CFLAGS) -c $<

#-------------------------------------------------------------------------------

all : $(EXES);

install : $(EXES)
	$(CP) $(EXES) $(HOME)/bin

clean :
	$(RM) $(EXES) *.o

#-------------------------------------------------------------------------------

scmtiff : err.o util.o scmdat.o scmio.o scm.o img.o jpg.o png.o tif.o pds.o convert.o combine.o mipmap.o border.o normal.o scmtiff.o
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^ -ljpeg -ltiff -lpng -lz

scmview : err.o util.o scmdat.o scmio.o scm.o img.o scmview.o
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^ -framework OpenGL -framework GLUT -lz
