#!/bin/sh
set -eu

dell1320c refresh || echo "dell1320c: could not update the existing print queues; run 'sudo dell1320c refresh'" >&2
exit 0
