#!/usr/bin/python
#
# VitalElement Build System - Pretty Processor.
# (FullFAT - OpenSource Edition).
#
# Written by James Walmsley <james@fullfat-fs.co.uk>
#

import sys
import prettyformat as pretty

command     = "??"
module      = "Unknown"
description = "Please fix this somebody!"
bCustom     = True

offset=0

if(len(sys.argv) >= 2):
    if(sys.argv[1] == "--vebuild"):
        offset = 1
        bCustom = False

if(len(sys.argv) >= 2+offset):
    command = sys.argv[1+offset]

if(len(sys.argv) >= 3+offset):
    module = sys.argv[2+offset]

if(len(sys.argv) >= 4+offset):
    description = sys.argv[3+offset]

pretty.prettyformat(command, module, description, bCustom)
