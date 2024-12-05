#!/bin/bash -eux

/sbin/m5 readfile > /tmp/runscript
chmod 755 /tmp/runscript
exec /tmp/runscript
