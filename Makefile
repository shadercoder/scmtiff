CC = cc
CP = cp

TARG = scmtiff
OPTS = -g -Wall
LIBS = -ljpeg -ltiff -lpng -lz
OBJS = error.o scm.o main.o

ifneq ($(wildcard /opt/local),)
	OPTS += -I/opt/local/include
	LIBS += -L/opt/local/lib
endif

%.o : %.c
	$(CC) $(OPTS) -c $<

$(TARG) : $(OBJS)
	$(CC) $(OPTS) -o $(TARG) $(OBJS) $(LIBS)

install : $(TARG)
	$(CP) $(TARG) $(HOME)/bin

clean :
	$(RM) $(TARG) $(OBJS)
