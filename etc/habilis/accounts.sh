#!/bin/sh
# Keep /etc/passwd, /etc/group and /etc/shadow on the home disk (see
# accounts.service), seeding them from the shipped ones in /etc/defaults
# the first time. Without a usable home disk, plain copies of the shipped
# ones go back in /etc -- a link to a missing disk would lock everyone out.
D=/etc/defaults
if [ -d /home ] && mkdir -p /home/.accounts 2>/dev/null; then
    chmod 755 /home/.accounts
    for f in passwd group shadow; do
        [ -e /home/.accounts/$f ] || cp $D/$f /home/.accounts/$f || exit 1
        ln -sf /home/.accounts/$f /etc/$f || exit 1
    done
    chmod 644 /home/.accounts/passwd /home/.accounts/group
    chmod 600 /home/.accounts/shadow
else
    for f in passwd group shadow; do
        if [ -L /etc/$f ] || [ ! -e /etc/$f ]; then
            rm -f /etc/$f && cp $D/$f /etc/$f || exit 1
        fi
    done
    chmod 600 /etc/shadow
fi
