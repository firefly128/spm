#!/bin/sh
# build-pkg.sh - Build solpkg SVR4 package on Solaris
# Run this on the SPARCstation (or any Solaris 7 SPARC system)
#
# Usage: sh build-pkg.sh
#
# Produces: solpkg-VERSION-sparc.pkg

PKGNAME="JWsolpkg"
VERSION="0.1.0"
BASEDIR="/opt/solpkg"
SRCDIR=`pwd`
BUILDDIR="/tmp/solpkg-build"
STAGEDIR="/tmp/solpkg-stage"
PKGDIR="/tmp/solpkg-pkg"

echo "=== solpkg SVR4 Package Builder ==="
echo "Source: ${SRCDIR}"
echo ""

# Clean previous builds
rm -rf ${BUILDDIR} ${STAGEDIR} ${PKGDIR} /tmp/solpkg-spool
mkdir -p ${BUILDDIR}
mkdir -p ${STAGEDIR}${BASEDIR}/bin
mkdir -p ${STAGEDIR}${BASEDIR}/etc
mkdir -p ${STAGEDIR}${BASEDIR}/var/cache
mkdir -p ${STAGEDIR}${BASEDIR}/var/index
mkdir -p ${STAGEDIR}${BASEDIR}/var/rollback
mkdir -p ${STAGEDIR}${BASEDIR}/man/man1m
mkdir -p ${PKGDIR}
mkdir -p /tmp/solpkg-spool

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
cp ${BUILDDIR}/solpkg ${STAGEDIR}${BASEDIR}/bin/
cp ${BUILDDIR}/solpkg-gui ${STAGEDIR}${BASEDIR}/bin/
cp ${BUILDDIR}/solpkg-agent ${STAGEDIR}${BASEDIR}/bin/
cp ${SRCDIR}/README.md ${STAGEDIR}${BASEDIR}/ 2>/dev/null
cp ${SRCDIR}/solpkg.1m ${STAGEDIR}${BASEDIR}/man/man1m/ 2>/dev/null

# Stage init.d script
mkdir -p ${STAGEDIR}/etc/init.d
cp ${SRCDIR}/solpkg-agent.init ${STAGEDIR}/etc/init.d/solpkg-agent
chmod 755 ${STAGEDIR}/etc/init.d/solpkg-agent 2>/dev/null

# Generate default config if not present
if [ ! -f ${STAGEDIR}${BASEDIR}/etc/repos.conf ]; then
    cat > ${STAGEDIR}${BASEDIR}/etc/repos.conf <<CONFEOF
# solpkg repository configuration
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
NAME=solpkg - Solaris SPARC Package Manager
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
f none bin/solpkg 0755 root bin
f none bin/solpkg-gui 0755 root bin
f none bin/solpkg-agent 0755 root bin
d none etc 0755 root bin
f none etc/repos.conf 0644 root bin
d none var 0755 root bin
d none var/cache 0755 root bin
d none var/index 0755 root bin
d none var/rollback 0755 root bin
d none man 0755 root bin
d none man/man1m 0755 root bin
f none man/man1m/solpkg.1m 0644 root bin
PROTO

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
pkgmk -o -r ${STAGEDIR}${BASEDIR} -d /tmp/solpkg-spool -f ${PKGDIR}/prototype

# Convert to datastream
echo "--- Creating datastream package ---"
OUTPKG="${SRCDIR}/solpkg-${VERSION}-sparc.pkg"
pkgtrans -s /tmp/solpkg-spool ${OUTPKG} ${PKGNAME}

echo ""
echo "=== Package built successfully ==="
echo "Output: ${OUTPKG}"
ls -la ${OUTPKG}
echo ""
echo "Install with: pkgadd -d ${OUTPKG}"
echo ""
echo "After install, add to PATH:"
echo "  PATH=/opt/solpkg/bin:\$PATH; export PATH"
echo "  solpkg update"
