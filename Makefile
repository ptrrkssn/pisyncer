# Makefile for pisyncer

CC=gcc
CFLAGS=-g -O -Wall
LDFLAGS=-lnsl


DEST=/export/ifm/mail/etc/mail-sync
PGMS=pisyncer.cgi
OBJS=pisyncer.o form.o html.o


all:		$(PGMS)

pisyncer.cgi:	$(OBJS)
	$(CC) -o pisyncer.cgi $(OBJS) $(LDFLAGS)

clean distclean:
	-rm -f $(PGMS) core *.o *~ \#*

install:	all
	cp pisyncer.cgi $(DEST)

form.o: 	form.c form.h
html.o: 	html.c html.h
pisyncer.o: 	pisyncer.c form.h html.h

