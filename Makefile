PROGRAM = rmate

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

INCLUDES = 
CPPFLAGS += -Wall -Wextra -Wno-missing-field-initializers $(INCLUDES)
#LDFLAGS = -L.
#LDLIBS +=

$(PROGRAM): $(OBJS)

$(PROGRAM).c: version.h

version.h:
	sh version.sh $(MSG_DEF) > $@

clean:
	$(RM) $(PROGRAM) $(OBJS) version.h

.PHONY: release clean
