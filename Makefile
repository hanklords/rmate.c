PROGRAM = rmate

SRCS = $(PROGRAM).c
OBJS = $(SRCS:.c=.o)
RM ?= rm -f

CFLAGS += -O2 -Wall -Wextra -Wno-missing-field-initializers

PREFIX ?= /usr/bin
DESTDIR ?= /

.SUFFIXES:.o
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $(CFLAGS) $? -o $@

rmate.c: version.h

version.h: version.sh
	sh version.sh $(MSG_DEF) > $@

clean:
	$(RM) $(PROGRAM) version.h $(OBJS)

install: $(PROGRAM)
	install -d $(DESTDIR)/$(PREFIX)
	install $(PROGRAM) $(DESTDIR)/$(PREFIX)/$(PROGRAM)

.PHONY: clean all
