VERSION = 0.1

PREFIX = /usr/local

CC = gcc
LD = $(CC)
CPPFLAGS = -D_BSD_SOURCE -D_GNU_SOURCE
CFLAGS   = -std=c99 -Wall -Wextra -pedantic $(CPPFLAGS)
LDFLAGS  = -s -larchive
