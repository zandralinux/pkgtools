VERSION = 0.2

PREFIX = /usr/local

CC = gcc
LD = $(CC)
CPPFLAGS = -D_BSD_SOURCE -D_GNU_SOURCE -DVERSION=\"${VERSION}\"
CFLAGS   = -std=c99 -Wall -Wextra -pedantic $(CPPFLAGS)
LDFLAGS  = -s -larchive
