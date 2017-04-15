#!/bin/sh -e

for rc in /etc/NeoMuttrc.d/*.rc; do
    test -r "$rc" && echo "source \"$rc\""
done

# vi: ft=sh
