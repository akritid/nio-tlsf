#!/bin/bash
# This script builds both the tlsf lib as well as the
# example supplied with it.


autoreconf --install && ./configure && make clean && make all