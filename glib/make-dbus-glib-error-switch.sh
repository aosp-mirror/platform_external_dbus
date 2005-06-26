#!/bin/sh

SRC=$1
DEST=$2

die()
{
    echo $1 1>&2
    /bin/rm $DEST.tmp
    exit 1
}

echo 'static gint' > $DEST.tmp
echo 'dbus_error_to_gerror_code (const char *derr)' >> $DEST.tmp
echo '{' >> $DEST.tmp
echo '  if (0) ; ' >> $DEST.tmp

cat $SRC | grep '#define DBUS_ERROR' | sed -e 's/#define //g' | \
  sed -e 's/".*//g' | \
   (while read line; do \
     echo '  else if (!strcmp (derr, ' "$line" ' )) '; \
     echo '    return ' `echo $line | sed -e 's/DBUS_ERROR/DBUS_GERROR/g'` ';'; \
    done; \
   ) >> $DEST.tmp
echo '  else' >> $DEST.tmp
echo '    return DBUS_GERROR_REMOTE_EXCEPTION;' >> $DEST.tmp
echo '}' >> $DEST.tmp

mv $DEST.tmp $DEST || die "could not move $DEST.tmp to $DEST"
