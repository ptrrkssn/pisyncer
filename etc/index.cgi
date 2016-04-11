#!/bin/sh

ADMINS="@sysadmin-users"
#ADMINS="peter,kristina"

HEADER=header.html
FOOTER=footer.html
export HEADER FOOTER ADMINS

exec ./pisyncer.cgi "$@"
