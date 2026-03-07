# spm - Sunstorm Package Manager

A package manager for Solaris 7 SPARC that aggregates packages from multiple
sources: [TGCware](https://jupiterrise.com/tgcware/sunos5.7_sparc/stable/)
binary packages, [Sunstorm](https://github.com/firefly128/sunstorm) distribution
packages, and GitHub release assets.

Designed for vintage SPARC hardware running Solaris 7 (SunOS 5.7). Loads OpenSSL
at runtime via `dlopen` (from Sunstorm or TGCware) for direct HTTPS access to
package repositories — no proxy or bridge server required.

Part of the [Sunstorm](https://github.com/firefly128/sunstorm) distribution.

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

## Installation

### Bootstrap package (recommended)

The bootstrap package is a single self-contained SVR4 package that includes
spm and all its dependencies (OpenSSL, zlib, libsolcompat, prngd, wget).
No prerequisites needed — just a base Solaris 7 SPARC install.

Download `SSTspm-0.1.0-bootstrap-sparc.pkg.Z` from the
[Releases](https://github.com/firefly128/spm/releases) page, transfer it
to the Solaris system, then:

```sh
uncompress SSTspm-0.1.0-bootstrap-sparc.pkg.Z
pkgadd -d SSTspm-0.1.0-bootstrap-sparc.pkg
```

The postinstall script starts `prngd` (for SSL entropy), sets up init scripts,
and adds `/opt/sst/bin` to your system PATH via `/etc/default/login`.
Log out and back in for the new PATH to take effect, or in an existing shell:

```sh
PATH=/opt/sst/bin:$PATH; export PATH

spm update
spm install bash gcc curl   # install anything you need
```

### From the Sunstorm distribution

If you already have the [Sunstorm](https://github.com/firefly128/sunstorm)
packages installed, spm is included as `SSTspm` along with its dependencies.
See the Sunstorm repo for the full package list.

## Requirements

- Solaris 7 (SunOS 5.7) on SPARC
- CDE/Motif (for GUI: standard Solaris install includes these)
- Network access to the internet (DNS + HTTPS)

The bootstrap package bundles all other dependencies. If installing the
regular (non-bootstrap) `SSTspm`, you also need:
- OpenSSL — Sunstorm (`/opt/sst/lib/libssl.so`) or TGCware (`/usr/tgcware/lib/libssl.so`)
- libsolcompat, libgcc_s, zlib, prngd (all provided by Sunstorm)

For building from source: Sunstorm GCC 11 (`/opt/sst/bin/gcc`) or TGCware GCC 4.7+.

## Building from source

```sh
# On the SPARCstation (requires GCC and libsolcompat)
CC=/opt/sst/bin/gcc make    # builds spm, spm-gui, spm-agent

# Build individual targets
make cli      # CLI only
make gui      # GUI only
make agent    # Agent only

# Build an SVR4 package
CC=/opt/sst/bin/gcc sh build-pkg.sh
# Produces: SSTspm-0.1.0-sparc.pkg

# Build the self-contained bootstrap package
CC=/opt/sst/bin/gcc sh build-bootstrap-pkg.sh
# Produces: SSTspm-0.1.0-bootstrap-sparc.pkg.Z
```

## Usage

### CLI

```sh
# Add to PATH
PATH=/opt/sst/bin:$PATH; export PATH

# Refresh package indices from all repos
spm update

# Search for packages
spm search curl
spm search discord

# Show package details
spm info curl

# Show dependency tree
spm deps curl

# Install a package (resolves dependencies automatically)
spm install curl

# Install multiple packages
spm install bash vim emacs

# Remove a package
spm remove curl

# Roll back to previous version
spm rollback curl

# Upgrade all packages
spm upgrade

# Upgrade specific package
spm upgrade curl

# List all available packages
spm list

# List installed packages
spm list --installed

# Show configured repositories
spm repo list

# Clear download cache
spm cache clean
```

### GUI (Motif/CDE)

```sh
# Launch the graphical package manager
DISPLAY=:0 spm-gui
```

The GUI provides a 2-pane layout: package list with search/filter on the left,
and package info/log on the right. Action buttons for Install, Remove, Upgrade,
and Deps. The status bar shows agent update notifications.

### Background Agent

```sh
# Start the update agent (daemonizes by default)
spm-agent

# Run in foreground (for debugging)
spm-agent -f

# Set custom check interval (seconds)
spm-agent -i 3600

# Check agent status
spm-agent -s

# Stop the agent
spm-agent -k

# Init.d control
/etc/init.d/spm-agent start
/etc/init.d/spm-agent stop
/etc/init.d/spm-agent status
/etc/init.d/spm-agent reload    # SIGHUP: reload config + re-check
```

The agent checks for updates at a configurable interval (default: 6 hours)
and writes status to `/opt/sst/var/update.status`. The GUI reads this
file to show update notifications in the status bar.

Send `SIGUSR1` to force an immediate check: `kill -USR1 $(cat /opt/sst/var/agent.pid)`

## Configuration

Configuration is stored in `/opt/sst/etc/repos.conf`:

```ini
# SSL settings
[ssl]
ca_bundle = /opt/sst/etc/ssl/certs/ca-bundle.crt

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

# Sunstorm distribution packages
[repo:sunstorm]
type = github
name = Sunstorm Distribution
owner = firefly128
repos = sunstorm
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
/opt/sst/
├── bin/
│   ├── spm              # CLI package manager
│   ├── spm-gui          # Motif/CDE graphical UI
│   └── spm-agent        # Background update daemon
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

/etc/init.d/spm-agent    # Init script for boot start
/etc/rc2.d/S99spm-agent  # Run-level 2 start link
/etc/rc0.d/K01spm-agent  # Run-level 0 stop link
```

## Architecture

```
                              HTTPS (TLS)
┌─────────────┐  native OpenSSL  ┌────────────────┐
│  SPARCstation│ ──────────────── │ jupiterrise.com│
│             │                  │ api.github.com │
│  ┌─────────┐│                  │ github.com     │
│  │ spm  ││                  └────────────────┘
│  │ (CLI)   ││
│  ├─────────┤│
│  │ spm  ││  Motif/X11
│  │  -gui   │├──────────── CDE Desktop
│  ├─────────┤│
│  │ spm  ││  sleep loop
│  │ -agent  │├──────────── /opt/sst/var/update.status
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

spm talks HTTPS directly using OpenSSL loaded at runtime via `dlopen`.
It searches for `libssl.so` and `libcrypto.so` in `/opt/sst/lib` (Sunstorm),
then `/usr/tgcware/lib` (TGCware), then the system library path.
No proxy or bridge server is needed.  CA certificates come from
`/opt/sst/etc/ssl/certs/ca-bundle.crt` with fallbacks to TGCware paths.

## How Dependencies Work

TGCware packages embed their dependencies in the HTML directory listing
description field as comma-separated package codes (e.g., `TGCgtext,TGClgcc1,TGCzlib`).

When you run `spm install curl`, spm:
1. Looks up `curl` in the available package index
2. Reads its dependency list: `TGCgtext,TGClgcc1,TGClicnv,...`
3. Checks each dependency against `pkginfo` (system SVR4 database)
4. Recursively resolves sub-dependencies
5. Installs all missing dependencies in order
6. Installs the requested package

## Source Correlation

For TGCware packages, spm maps each binary package to its build recipe in
the [tgcwarev2-for-solaris](https://github.com/AstroVPK/tgcwarev2-for-solaris)
repository. Use `spm info <pkg>` to see the source URL.

## SVR4 Package

spm is distributed as an SVR4 package (`SSTspm`). Two variants exist:

- **Bootstrap** (`SSTspm-0.1.0-bootstrap-sparc.pkg.Z`) — Self-contained,
  bundles all dependencies. For fresh Solaris 7 installs.
- **Regular** (`SSTspm-0.1.0-sparc.pkg.Z`) — Minimal, depends on separately
  installed SSTossl, SSTzlib, SSTlsolc, SSTprngd. Included in the Sunstorm distro.

Build either with `sh build-pkg.sh` or `sh build-bootstrap-pkg.sh` on a
Solaris 7 SPARC system with Sunstorm GCC installed.

## License

MIT
