#**************************************************************
#* File:: Makefile
#*
#* Description:: Builds the Codex Micro bridge: the daemon that drives a device
#* from agent status, and the notify client that adapters use to report status.
#*
#**************************************************************

CC = gcc
CFLAGS = -Wall -Wextra -g -I.

all: codexmicrod codexmicro-notify

codexmicrod: codexmicrod.c codexmicro_wire.h
	$(CC) $(CFLAGS) -o $@ codexmicrod.c

codexmicro-notify: codexmicro_notify.c codexmicro_wire.h
	$(CC) $(CFLAGS) -o $@ codexmicro_notify.c

clean:
	rm -f codexmicrod codexmicro-notify
