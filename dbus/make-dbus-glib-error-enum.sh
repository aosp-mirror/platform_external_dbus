#!/bin/sh

SRC=$1
DEST=$2

die()
{
    echo $1 1>&2
    /bin/rm $DEST.tmp
    exit 1
}

cat $SRC | grep '#define DBUS_ERROR' | sed -e 's/#define //g' | \
  sed -e 's/".*//g' | sed -e 's/DBUS_ERROR/DBUS_GERROR/g' | sed -e 's/ *$/,/g' > $DEST.tmp

if ! test -s $DEST.tmp ; then
    die "$DEST.tmp is empty, something went wrong, see any preceding error message"
fi
echo "DBUS_GERROR_REMOTE_EXCEPTION" >> $DEST.tmp

echo "#ifndef DBUS_INSIDE_DBUS_GLIB_H" >> $DEST.tmp
echo '#error "' "$DEST" 'may only be included by dbus-glib.h"' >> $DEST.tmp
echo "#endif" >> $DEST.tmp

mv $DEST.tmp $DEST || die "could not move $DEST.tmp to $DEST"
