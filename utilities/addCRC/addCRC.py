#
# @file addCRC.py
#
# Script to normalize a hex image file, add a CCITT-CRC16 and
# save the result as the same file name.
#
# This script makes use of the Microchip HexMate utility.
# For XC32 projects HexMate cannot be automatically invoked
# with linker options, as it can for XC8 projects.  It can be
# invoked as a Post Build process, but only a single command is
# allowed, which prevents saving the resulting output as the
# same filename as the input (important for development).
#
# The intent is that this script is called from the MPLAB-X
# Post Build hook, with the necessary arguments.
#
import sys
import os
import hashlib
import base64
from intelhex import IntelHex
import datetime
import libscrc
import json
import re
import argparse
import functools

# Update this version number with subsequent releases
utilityVersion = "1.0"
defaultHexmate = '\\Program Files\Microchip\\xc8\\v2.31\\pic\\bin\\hexmate.exe'

if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description= f'Drinkworks Add CRC Utility, v{utilityVersion}')
    parser.add_argument("hexfile", help="Hex Image Filename")
    parser.add_argument('--ProgStart', '-ps', help="Program Flash starting address. Default is \'0x1d000000\'", default=0x1d000000, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--ProgEnd', '-pe', help="Program Flash ending address. Default is \'0x1d01ffff\'", default=0x1d01ffff, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--BootStart', '-bs', help="Boot Flash starting address. Default is \'0x1fc00000\'", default=0x1fc00000, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--BootEnd', '-be', help="Boot Flash ending address. Default is \'0x1fc00bff\'", default=0x1fc00bff, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--CrcStart', '-cs', help="CRC starting address. Default is \'0x1d003000\'", default=0x1d003000, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--CrcEnd', '-ce', help="CRC ending address. Default is \'0x1d01fff7\'", default=0x1d01fff7, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--CrcLocation', '-cl', help="CRC placement address. Default is \'0x1d01fff8\'", default=0x1d01fff8, type=functools.wraps(int)(lambda x: int(x,0)))
    parser.add_argument('--hexmate', '-hex', help=f'Hexmate utility loation. Default is \'{defaultHexmate}\'', default=defaultHexmate)
    args = parser.parse_args()

	# Check that Hex file exists
    if not os.path.exists( args.hexfile ):
        print( f'ERROR: Input file \"{args.hexfile}\" does not exist')
        sys.exit(1)

    path = os.path.dirname( args.hexfile )

#   print( f'ProgStart:   {args.ProgStart:#x}')
#   print( f'ProgEnd:     {args.ProgEnd:#x}')
#   print( f'BootStart:   {args.BootStart:#x}')
#   print( f'BootEnd:     {args.BootEnd:#x}')
#   print( f'CrcStart:    {args.CrcStart:#x}')
#   print( f'CrcEnd:      {args.CrcEnd:#x}')
#   print( f'CrcLocation: {args.CrcLocation:#x}')

    # Perform some basic address checking
    if( args.ProgStart > args.ProgEnd ):
        print( f'ERROR: Invalid Program Flash address range')
        sys.exit(2)
    if( args.BootStart > args.BootEnd ):
        print( f'ERROR: Invalid Boot Flash address range')
        sys.exit(3)
    if( args.CrcStart > args.CrcEnd ):
        print( f'ERROR: Invalid CRC address range')
        sys.exit(4)
    if( args.CrcLocation >= args.CrcStart and args.CrcLocation <= args.CrcEnd):
        print( f'ERROR: Inavlid CRC Location address')
        sys.exit(5)

    # Prepare HexMate command to normalize file and add CRC
    fill = '-FILL=w4:0xffffffff'
    check = '+-CK='
    flags = '+0000w2g5p1021'
    # Temporary output file in same directory as input
    tempfile = f'{path}\\temp.hex'
	
    hexmateCommand = f'"{args.hexmate}" {args.hexfile} {fill}@{args.ProgStart:#x}:{args.ProgEnd:#x} {fill}@{args.BootStart:#x}:{args.BootEnd:#x} {check}{args.CrcStart:#x}-{args.CrcEnd:#x}@{args.CrcLocation:#x}{flags} -o{tempfile}'
    print( f'{hexmateCommand}')
	
    # Invoke HexMate command, check exit code before deleting input file
    # At command prompt "echo %errorlevel%" shows exit code
    # Hexmate return 0 upon success
    if( os.system(hexmateCommand) == 0):

        # Delete original file
        os.remove(args.hexfile)
	
        # Rename temp file to original filename
        os.rename( tempfile, args.hexfile )

        # Success
        sys.exit(0)
    else:
        print( f'ERROR: hexmate failed')
        sys.exit(6)
	
	