# solpkg Makefile for Solaris 7 SPARC
#
# Package manager for TGCware and GitHub release packages.
# Uses OpenSSL (TGCware) loaded at runtime via dlopen for HTTPS.
#
# Builds three targets:
#   solpkg       - CLI package manager
#   solpkg-gui   - Motif/CDE graphical package manager
#   solpkg-agent - Background update check daemon
#
# Requires: TGCware OpenSSL (loaded at runtime via dlopen)
#           Solaris socket/dl libs (-lsocket -lnsl -ldl)
#           Motif/CDE (for GUI: -lXm -lXt -lX11)

# Use tgcware GCC if available, fall back to system cc
CC = /usr/tgcware/gcc47/bin/gcc
#CC = cc

# TGCware paths for OpenSSL headers and libs
TGCWARE = /usr/tgcware

# Motif / X11 / CDE paths
MOTIFDIR = /usr/dt
OPENWINDIR = /usr/openwin

CFLAGS = -O2 -Wall -I$(TGCWARE)/include
LDFLAGS = -s -L$(TGCWARE)/lib -R$(TGCWARE)/lib
LIBS = -lsocket -lnsl -ldl

# Motif/X11 link flags (GUI only)
MOTIF_CFLAGS = -I$(MOTIFDIR)/include -I$(OPENWINDIR)/include
MOTIF_LDFLAGS = -L$(MOTIFDIR)/lib -R$(MOTIFDIR)/lib \
                -L$(OPENWINDIR)/lib -R$(OPENWINDIR)/lib
MOTIF_LIBS = -lXm -lXt -lX11 -lm

# Shared source objects (used by all three targets)
SHARED_SRCS = http.c json.c html.c config.c pkgdb.c
SHARED_OBJS = $(SHARED_SRCS:.c=.o)

# CLI
CLI_TARGET = solpkg
CLI_OBJS = solpkg.o $(SHARED_OBJS)

# GUI
GUI_TARGET = solpkg-gui
GUI_OBJS = solpkg-gui.o $(SHARED_OBJS)

# Agent
AGENT_TARGET = solpkg-agent
AGENT_OBJS = solpkg-agent.o $(SHARED_OBJS)

PREFIX = /opt/solpkg

all: $(CLI_TARGET) $(GUI_TARGET) $(AGENT_TARGET)

cli: $(CLI_TARGET)

gui: $(GUI_TARGET)

agent: $(AGENT_TARGET)

$(CLI_TARGET): $(CLI_OBJS)
	$(CC) $(LDFLAGS) -o $(CLI_TARGET) $(CLI_OBJS) $(LIBS)

$(GUI_TARGET): $(GUI_OBJS)
	$(CC) $(LDFLAGS) $(MOTIF_LDFLAGS) -o $(GUI_TARGET) $(GUI_OBJS) $(LIBS) $(MOTIF_LIBS)

$(AGENT_TARGET): $(AGENT_OBJS)
	$(CC) $(LDFLAGS) -o $(AGENT_TARGET) $(AGENT_OBJS) $(LIBS)

# Default .c.o rule for shared sources
.c.o:
	$(CC) $(CFLAGS) -c $<

# GUI source needs Motif includes
solpkg-gui.o: solpkg-gui.c config.h http.h html.h json.h pkgdb.h
	$(CC) $(CFLAGS) $(MOTIF_CFLAGS) -c solpkg-gui.c

# Dependencies
solpkg.o: solpkg.c config.h http.h html.h json.h pkgdb.h
solpkg-agent.o: solpkg-agent.c config.h http.h json.h html.h pkgdb.h
http.o: http.c http.h config.h
json.o: json.c json.h
html.o: html.c html.h
config.o: config.c config.h
pkgdb.o: pkgdb.c pkgdb.h http.h json.h html.h config.h

clean:
	rm -f $(CLI_TARGET) $(GUI_TARGET) $(AGENT_TARGET) \
	      solpkg.o solpkg-gui.o solpkg-agent.o $(SHARED_OBJS)

install: all
	@echo "Installing solpkg..."
	mkdir -p $(PREFIX)/bin 2>/dev/null || true
	mkdir -p $(PREFIX)/etc 2>/dev/null || true
	mkdir -p $(PREFIX)/var/cache 2>/dev/null || true
	mkdir -p $(PREFIX)/var/index 2>/dev/null || true
	mkdir -p $(PREFIX)/var/rollback 2>/dev/null || true
	cp $(CLI_TARGET) $(PREFIX)/bin/solpkg
	cp $(GUI_TARGET) $(PREFIX)/bin/solpkg-gui
	cp $(AGENT_TARGET) $(PREFIX)/bin/solpkg-agent
	chmod 755 $(PREFIX)/bin/solpkg
	chmod 755 $(PREFIX)/bin/solpkg-gui
	chmod 755 $(PREFIX)/bin/solpkg-agent
	@if [ ! -f $(PREFIX)/etc/repos.conf ]; then \
		echo "Generating default config..."; \
		$(PREFIX)/bin/solpkg --version >/dev/null 2>&1; \
	fi
	@echo ""
	@echo "Installing init.d script..."
	cp solpkg-agent.init /etc/init.d/solpkg-agent 2>/dev/null || true
	chmod 755 /etc/init.d/solpkg-agent 2>/dev/null || true
	@echo ""
	@echo "Done! Add /opt/solpkg/bin to your PATH:"
	@echo "  PATH=/opt/solpkg/bin:\$$PATH; export PATH"
	@echo ""
	@echo "Start the update agent:"
	@echo "  /etc/init.d/solpkg-agent start"
	@echo ""
	@echo "Launch the GUI:"
	@echo "  solpkg-gui"
	@echo ""
	@echo "Or use the CLI: solpkg update"

.PHONY: all cli gui agent clean install
