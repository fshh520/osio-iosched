#!/bin/sh
# shell script for the osio io-scheduler kernel modules
#
# Copyright (C) 2015 Octagram Sun <octagram@qq.com>
#
# This file is part of osio io-scheduler, as available from 
# https://gitcafe.com/octagram/osio. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "LICENSE" file of the
# osio distribution. osio is distributed in the hope that 
# it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

NAME=osio-ioscheduler
VERSION=$(cat version)
REVISION=1
ARCH=all
PKGNAME=${NAME}_${VERSION}-${REVISION}_${ARCH}

function clean()
{
	if [ -d ${PKGNAME} ] ; then
		rm -rf ${PKGNAME}
	fi

	if [ -f ${PKGNAME}.deb ] ; then
		rm -f ${PKGNAME}.deb
	fi
}

function mkdeb()
{
	mkdir ${PKGNAME}
	cp -a DEBIAN ${PKGNAME}
	sed -i "s/\(.*\)##VERSION##/\1${VERSION}/g" ${PKGNAME}/DEBIAN/control
	sed -i "s/\(.*\)##VERSION##/\1${VERSION}/g" ${PKGNAME}/DEBIAN/postinst
	sed -i "s/\(.*\)##VERSION##/\1${VERSION}/g" ${PKGNAME}/DEBIAN/prerm

	mkdir -p ${PKGNAME}/usr/src/${NAME}-${VERSION}
	cp Makefile ${PKGNAME}/usr/src/${NAME}-${VERSION}
	cp osio-iosched.c ${PKGNAME}/usr/src/${NAME}-${VERSION}
	cp dkms.conf ${PKGNAME}/usr/src/${NAME}-${VERSION}

	mkdir -p ${PKGNAME}/usr/share/doc/${NAME}
	cp LICENSE ${PKGNAME}/usr/share/doc/${NAME}
	cp README.zh_CN ${PKGNAME}/usr/share/doc/${NAME}
	cp README.en_US ${PKGNAME}/usr/share/doc/${NAME}
	cp README.md ${PKGNAME}/usr/share/doc/${NAME}
	cp version ${PKGNAME}/usr/share/doc/${NAME}
	cp DEBIAN/copyright ${PKGNAME}/usr/share/doc/${NAME}

	cd ${NAME}_${VERSION}-${REVISION}_${ARCH}
	find usr -type f | xargs md5sum > DEBIAN/md5sums
	cd ..

	dpkg-deb -b ${NAME}_${VERSION}-${REVISION}_${ARCH}
}

case "$1" in
clean)
	clean
	;;
*)
	clean
	mkdeb
	;;
esac

exit 0
