# solpkg - Solaris SPARC Package Manager

A package manager for Solaris 7 SPARC that aggregates packages from multiple
sources: [TGCware](https://jupiterrise.com/tgcware/sunos5.7_sparc/stable/)
binary packages and GitHub release assets.

Designed for vintage SPARC hardware running Solaris 7 (SunOS 5.7). Uses native
OpenSSL from TGCware for direct HTTPS access to package repositories — no proxy
or bridge server required.

## Features

- **Multi-source repositories** — Index TGCware packages and GitHub releases
- **Dependency resolution** — Recursive dependency tree tracking for TGCware packages
- **SVR4 integration** — Wraps native `pkgadd`/`pkgrm` for install/remove
- **Rollback** — Roll back to previous package versions from cache
- **Search** — Case-insensitive search across name, description, and package code
- **Source correlation** — Links TGCware binaries to their GitHub source trees
- **Native HTTPS** — Direct SSL/TLS via TGCware OpenSSL (no proxy needed)
- **Motif/CDE GUI** — Graphical package manager with search, install, remove, and upgrade
- **Background agent** — Daemon that periodically checks for updates, started on boot

## Requirements

- Solaris 7 (SunOS 5.7) on SPARC
- TGCware GCC 4.7+ (`/usr/tgcware/gcc47/bin/gcc`) or system `cc`
- TGCware OpenSSL (`/usr/tgcware/lib/libssl.so`)
- TGCware curl CA bundle (`/usr/tgcware/etc/curl-ca-bundle.pem`) — or any CA bundle
- CDE/Motif (for GUI: `/usr/dt/lib/libXm.so`, `/usr/openwin/lib/libX11.so`)
- Network access to the internet (DNS + HTTPS)

## Building

```sh
# On the SPARCstation
make          # builds all three: solpkg, solpkg-gui, solpkg-agent
make install

# Build individual targets
make cli      # CLI only
make gui      # GUI only
make agent    # Agent only

# Or build an SVR4 package
sh build-pkg.sh
pkgadd -d solpkg-0.1.0-sparc.pkg
```

## Usage

### CLI

```sh
# Add to PATH
PATH=/opt/solpkg/bin:$PATH; export PATH

# Refresh package indices from all repos
solpkg update

# Search for packages
solpkg search curl
solpkg search discord

# Show package details
solpkg info curl

# Show dependency tree
solpkg deps curl

# Install a package (resolves dependencies automatically)
solpkg install curl

# Install multiple packages
solpkg install bash vim emacs

# Remove a package
solpkg remove curl

# Roll back to previous version
solpkg rollback curl

# Upgrade all packages
solpkg upgrade

# Upgrade specific package
solpkg upgrade curl

# List all available packages
solpkg list

# List installed packages
solpkg list --installed

# Show configured repositories
solpkg repo list

# Clear download cache
solpkg cache clean
```

### GUI (Motif/CDE)

```sh
# Launch the graphical package manager
DISPLAY=:0 solpkg-gui
```

The GUI provides a 2-pane layout: package list with search/filter on the left,
and package info/log on the right. Action buttons for Install, Remove, Upgrade,
and Deps. The status bar shows agent update notifications.

### Background Agent

```sh
# Start the update agent (daemonizes by default)
solpkg-agent

# Run in foreground (for debugging)
solpkg-agent -f

# Set custom check interval (seconds)
solpkg-agent -i 3600

# Check agent status
solpkg-agent -s

# Stop the agent
solpkg-agent -k

# Init.d control
/etc/init.d/solpkg-agent start
/etc/init.d/solpkg-agent stop
/etc/init.d/solpkg-agent status
/etc/init.d/solpkg-agent reload    # SIGHUP: reload config + re-check
```

The agent checks for updates at a configurable interval (default: 6 hours)
and writes status to `/opt/solpkg/var/update.status`. The GUI reads this
file to show update notifications in the status bar.

Send `SIGUSR1` to force an immediate check: `kill -USR1 $(cat /opt/solpkg/var/agent.pid)`

## Configuration

Configuration is stored in `/opt/solpkg/etc/repos.conf`:

```ini
# SSL settings
[ssl]
ca_bundle = /usr/tgcware/etc/curl-ca-bundle.pem

# Background agent settings
[agent]
interval = 21600
notify = yes

# TGCware Solaris 7 SPARC binary repository
[repo:tgcware]
type = tgcware
name = TGCware Solaris 7 SPARC
url = https://jupiterrise.com/tgcware/sunos5.7_sparc/stable/
source = https://github.com/AstroVPK/tgcwarev2-for-solaris
enabled = yes

# GitHub releases (SVR4 .pkg files)
[repo:firefly128]
type = github
name = firefly128 SPARC Apps
owner = firefly128
repos = pizzafool,sparccord,wesnoth-sparc
enabled = yes
```

### Repository Types

**TGCware** (`type = tgcware`): Indexes the Apache directory listing at
jupiterrise.com. Parses filenames like `curl-8.18.0-1.tgc-sunos5.7-sparc-tgcware.gz`
and descriptions containing package codes, dependencies, and MD5 checksums.

**GitHub** (`type = github`): Fetches releases from the GitHub API for each
configured repo. Indexes `.pkg` and `.gz` assets as installable packages.

## Directory Layout

```
/opt/solpkg/
├── bin/
│   ├── solpkg              # CLI package manager
│   ├── solpkg-gui          # Motif/CDE graphical UI
│   └── solpkg-agent        # Background update daemon
├── etc/
│   └── repos.conf          # Repository + agent configuration
└── var/
    ├── cache/              # Downloaded package files
    ├── index/
    │   └── available.idx   # Cached package index
    ├── installed.db        # Installed package database
    ├── rollback/           # Backed-up packages for rollback
    ├── agent.pid           # Agent PID file (runtime)
    ├── agent.log           # Agent log file (runtime)
    └── update.status       # Agent update status (runtime)

/etc/init.d/solpkg-agent    # Init script for boot start
/etc/rc2.d/S99solpkg-agent  # Run-level 2 start link
/etc/rc0.d/K01solpkg-agent  # Run-level 0 stop link
```

## Architecture

```
                              HTTPS (TLS)
┌─────────────┐  native OpenSSL  ┌────────────────┐
│  SPARCstation│ ──────────────── │ jupiterrise.com│
│             │                  │ api.github.com │
│  ┌─────────┐│                  │ github.com     │
│  │ solpkg  ││                  └────────────────┘
│  │ (CLI)   ││
│  ├─────────┤│
│  │ solpkg  ││  Motif/X11
│  │  -gui   │├──────────── CDE Desktop
│  ├─────────┤│
│  │ solpkg  ││  sleep loop
│  │ -agent  │├──────────── /opt/solpkg/var/update.status
│  └─────────┘│
│      │      │
│  pkgadd/rm  │
│      ▼      │
│  ┌─────────┐│
│  │  SVR4   ││
│  │  pkgsys ││
│  └─────────┘│
└─────────────┘
```

solpkg talks HTTPS directly using the TGCware OpenSSL 1.0.2 library.
No proxy or bridge server is needed.  CA certificates come from the
TGCware curl package's CA bundle.

## How Dependencies Work

TGCware packages embed their dependencies in the HTML directory listing
description field as comma-separated package codes (e.g., `TGCgtext,TGClgcc1,TGCzlib`).

When you run `solpkg install curl`, solpkg:
1. Looks up `curl` in the available package index
2. Reads its dependency list: `TGCgtext,TGClgcc1,TGClicnv,...`
3. Checks each dependency against `pkginfo` (system SVR4 database)
4. Recursively resolves sub-dependencies
5. Installs all missing dependencies in order
6. Installs the requested package

## Source Correlation

For TGCware packages, solpkg maps each binary package to its build recipe in
the [tgcwarev2-for-solaris](https://github.com/AstroVPK/tgcwarev2-for-solaris)
repository. Use `solpkg info <pkg>` to see the source URL.

## SVR4 Package

solpkg itself is distributed as an SVR4 package (`JWsolpkg`). Build it with
`sh build-pkg.sh` on any Solaris 7 SPARC system with TGCware GCC installed.

## License

MIT
