#!/bin/sh
# build-pkg.sh - Build spm SVR4 package on Solaris
# Run this on the SPARCstation (or any Solaris 7 SPARC system)
#
# Usage: sh build-pkg.sh
#
# Produces: spm-VERSION-sparc.pkg

PKGNAME="SSTspm"
VERSION="0.1.0"
BASEDIR="/opt/sst"
SRCDIR=`pwd`
BUILDDIR="/tmp/spm-build"
STAGEDIR="/tmp/spm-stage"
PKGDIR="/tmp/spm-pkg"

echo "=== spm SVR4 Package Builder ==="
echo "Source: ${SRCDIR}"
echo ""

# Clean previous builds
rm -rf ${BUILDDIR} ${STAGEDIR} ${PKGDIR} /tmp/spm-spool
mkdir -p ${BUILDDIR}
mkdir -p ${STAGEDIR}${BASEDIR}/bin
mkdir -p ${STAGEDIR}${BASEDIR}/etc
mkdir -p ${STAGEDIR}${BASEDIR}/var/cache
mkdir -p ${STAGEDIR}${BASEDIR}/var/index
mkdir -p ${STAGEDIR}${BASEDIR}/var/rollback
mkdir -p ${STAGEDIR}${BASEDIR}/man/man1m
mkdir -p ${PKGDIR}
mkdir -p /tmp/spm-spool

# Copy source and build
echo "--- Compiling ---"
cp ${SRCDIR}/*.c ${SRCDIR}/*.h ${SRCDIR}/Makefile ${BUILDDIR}/
cd ${BUILDDIR}
make clean 2>/dev/null
make
if [ $? -ne 0 ]; then
    echo "BUILD FAILED"
    exit 1
fi
echo "Build OK"

# Stage files
echo "--- Staging ---"
cp ${BUILDDIR}/spm ${STAGEDIR}${BASEDIR}/bin/
cp ${BUILDDIR}/spm-gui ${STAGEDIR}${BASEDIR}/bin/
cp ${BUILDDIR}/spm-agent ${STAGEDIR}${BASEDIR}/bin/
cp ${SRCDIR}/README.md ${STAGEDIR}${BASEDIR}/ 2>/dev/null
cp ${SRCDIR}/spm.1m ${STAGEDIR}${BASEDIR}/man/man1m/ 2>/dev/null

# Stage init.d script
mkdir -p ${STAGEDIR}/etc/init.d
cp ${SRCDIR}/spm-agent.init ${STAGEDIR}/etc/init.d/spm-agent
chmod 755 ${STAGEDIR}/etc/init.d/spm-agent 2>/dev/null

# Stage CDE desktop integration files
mkdir -p ${STAGEDIR}/usr/dt/appconfig/types/C
mkdir -p ${STAGEDIR}/usr/dt/appconfig/icons/C
cp ${SRCDIR}/dt/types/spm.dt ${STAGEDIR}/usr/dt/appconfig/types/C/
cp ${SRCDIR}/dt/icons/Spm.l.pm ${STAGEDIR}/usr/dt/appconfig/icons/C/
cp ${SRCDIR}/dt/icons/Spm.m.pm ${STAGEDIR}/usr/dt/appconfig/icons/C/
cp ${SRCDIR}/dt/icons/Spm.l.bm ${STAGEDIR}/usr/dt/appconfig/icons/C/

# Generate default config if not present
if [ ! -f ${STAGEDIR}${BASEDIR}/etc/repos.conf ]; then
    cat > ${STAGEDIR}${BASEDIR}/etc/repos.conf <<CONFEOF
# spm repository configuration
#
# SSL settings (CA certificate bundle for HTTPS)
[ssl]
ca_bundle = /usr/tgcware/etc/curl-ca-bundle.pem

[agent]
interval = 21600
notify = yes

# TGCware Solaris 7 SPARC repository
[repo:tgcware]
type = tgcware
name = TGCware Solaris 7 SPARC
url = https://jupiterrise.com/tgcware/sunos5.7_sparc/stable/
source = https://github.com/AstroVPK/tgcwarev2-for-solaris
enabled = yes

# Julian's GitHub releases
[repo:firefly128]
type = github
name = firefly128 SPARC Apps
owner = firefly128
repos = pizzafool,sparccord,wesnoth-sparc
enabled = yes
CONFEOF
fi

echo "Staged to ${STAGEDIR}"

# Generate prototype file
echo "--- Generating prototype ---"
PSTAMP=`date '+%Y%m%d%H%M%S'`
cat > ${PKGDIR}/pkginfo <<EOF
PKG=${PKGNAME}
NAME=spm - Sunstorm Package Manager
ARCH=sparc
VERSION=${VERSION}
CATEGORY=system
VENDOR=Julian Wolfe
EMAIL=
PSTAMP=${PSTAMP}
BASEDIR=${BASEDIR}
CLASSES=none
EOF

cp ${SRCDIR}/pkg/depend ${PKGDIR}/ 2>/dev/null
cp ${SRCDIR}/pkg/postinstall ${PKGDIR}/
cp ${SRCDIR}/pkg/preremove ${PKGDIR}/

# Build prototype
cat > ${PKGDIR}/prototype <<PROTO
i pkginfo
i postinstall
i preremove
d none bin 0755 root bin
f none bin/spm 0755 root bin
f none bin/spm-gui 0755 root bin
f none bin/spm-agent 0755 root bin
d none etc 0755 root bin
f none etc/repos.conf 0644 root bin
d none var 0755 root bin
d none var/cache 0755 root bin
d none var/index 0755 root bin
d none var/rollback 0755 root bin
d none man 0755 root bin
d none man/man1m 0755 root bin
f none man/man1m/spm.1m 0644 root bin
PROTO

# Add CDE integration files (absolute paths, outside BASEDIR)
cat >> ${PKGDIR}/prototype <<DTPROTO
!search ${STAGEDIR}
d none /usr/dt/appconfig/types/C 0755 root bin
f none /usr/dt/appconfig/types/C/spm.dt 0644 root bin
d none /usr/dt/appconfig/icons/C 0755 root bin
f none /usr/dt/appconfig/icons/C/Spm.l.pm 0644 root bin
f none /usr/dt/appconfig/icons/C/Spm.m.pm 0644 root bin
f none /usr/dt/appconfig/icons/C/Spm.l.bm 0644 root bin
DTPROTO

# Add depend if present
if [ -f ${PKGDIR}/depend ]; then
    echo "i depend" >> ${PKGDIR}/prototype
fi

# Add README if staged
if [ -f ${STAGEDIR}${BASEDIR}/README.md ]; then
    echo "f none README.md 0644 root bin" >> ${PKGDIR}/prototype
fi

echo "Prototype:"
cat ${PKGDIR}/prototype
echo ""

# Build package
echo "--- Building package ---"
pkgmk -o -r ${STAGEDIR}${BASEDIR} -d /tmp/spm-spool -f ${PKGDIR}/prototype

# Convert to datastream
echo "--- Creating datastream package ---"
OUTPKG="${SRCDIR}/spm-${VERSION}-sparc.pkg"
pkgtrans -s /tmp/spm-spool ${OUTPKG} ${PKGNAME}

echo ""
echo "=== Package built successfully ==="
echo "Output: ${OUTPKG}"
ls -la ${OUTPKG}
echo ""
echo "Install with: pkgadd -d ${OUTPKG}"
echo ""
echo "After install, add to PATH:"
echo "  PATH=/opt/sst/bin:\$PATH; export PATH"
echo "  spm update"
