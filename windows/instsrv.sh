#!/bin/sh
# install rsync as a service

svcname=rsync
dispname=Rsync
description="Rsync is an open source utility that provides fast incremental file transfer"
programfile=/bin/rsync.exe
args="--daemon --no-detach"

cygrunsrv \
	--install ${svcname} \
	--disp ${dispname} \
	--desc "${description}" \
	--path ${programfile} \
	--args "${args}"
