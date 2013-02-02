#! /bin/sh

set -e

qmake
make
cd tests
for i in ./tst_*; do ./$i; done
