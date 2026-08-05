#**************************************************************
#* File:: Makefile
#*
#* Description:: Builds the Codex Micro user-space programs. The default target
#* builds the dashboard control surface, the ioctl test, and the key bridge.
#*
#**************************************************************

CC = gcc
CFLAGS = -Wall -g -I.

# The main application: the agent-control dashboard.
DASHBOARD_OBJ = dashboard.o mock_agent.o uinput_bridge.o

all: dashboard ioctl_test key_bridge

dashboard: $(DASHBOARD_OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

ioctl_test: ioctl_test.c
	$(CC) -o $@ $< $(CFLAGS)

key_bridge: key_bridge.c uinput_bridge.c
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

run: dashboard
	sudo ./dashboard

clean:
	rm -f *.o dashboard ioctl_test key_bridge
