#
# @file hex2aws.py
#
# Script to convert a PIC18F .hex Image file to a binary .aws image file.
#
# The binary .aws image file has a pre-pended JSON meta-data document of known length
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

# Format Meta Data, to be pre-pended to PIC binary image
def format_meta_data( hexfile, hash, crc, address, size, timestamp, picVersion, padding):
    metaData = "{\r\n"
    metaData += f'  "HexFileName": "{hexfile}",\r\n'
    metaData += f'  "SHA256": "{hash}",\r\n'
    metaData += f'  "CRC16_CCITT": {crc:d},\r\n'
    metaData += f'  "LoadAddress": {address:d},\r\n'
    metaData += f'  "ImageSize": {size:d},\r\n'
    metaData += f'  "TimeStamp": "{timestamp}",\r\n'
    metaData += f'  "Version_PIC": {picVersion:5.2f},\r\n'
    metaData += f'  "PaddingBoundary": {padding:d}\r\n'
    metaData += "}"
    return metaData

# Create PIC18F AWS Image file from .hex
# If image file already exists, compare SHA256 hash values
# Return values:
#	-1: Hex file does not exist
#	 0: AWS file created (may have over-written existing file)
#    1: Hash Value from existing AWS file matches that for processed HEX file
#    2: Hash Values do not match, not over-writing
def create_aws( hexfile, outpath, startAddr, binSize, picVersion, padBoundary = 512):
    # Split hexfile and form .aws output filename
    root_ext = os.path.splitext(os.path.basename(hexfile))
    awsFileName = outpath + "\\" + root_ext[0] + ".aws"
	
	# Check that Hex file exists
    if not os.path.exists( hexfile ):
        print( f'ERROR: Input file {hexfile} does not exist')
        return -1

	# Read in Hex file, convert specific address range as binary array, compute SHA256 hash
    ih = IntelHex( hexfile )
    contents = ih.tobinarray(start = startAddr, size = binSize)
    hash =  base64.b64encode(hashlib.sha256(contents).digest()).decode('ascii')
	
    # If AWS file exists, read it in and extract the SHA256 Hash value	
    if os.path.exists(awsFileName):
        with open(awsFileName, "r", errors="ignore") as file:
            data = file.read()
            m = re.search('\{(.+?)\}', data, flags=re.DOTALL)
            if m:
                dictionary = json.loads( m.group(0) )
                # If hash values match, don't recreate
                if hash == dictionary["SHA256"]:
                    print("AWS file already exists, Hash values match")
                    return 1
                else:
                    print("AWS file already exists, hash value does not match that for processed HEX file")
                    response = input("Over-write? (Y/N): ")
                    if not ('Y' or 'y') in response:
                        return 2

    # Time/Date stamp the release
	# TODO: Add TimeZone Offset to be compatible with C# App
    from datetime import datetime
    now = datetime.now()
    dateTimeFormat = now.strftime("%Y-%m-%dT%H:%M:%S.%f")
	
    # Byte reverse the data, and calculate CRC16_CCITT, to mimic what the PIC18F hardware CRC engine does
    rdata = bytearray(len(contents))
    rdata[0::2] = contents[1::2]
    rdata[1::2] = contents[0::2]
    crc = libscrc.xmodem(rdata)
	
    # Format Meta Data
    metaData = format_meta_data( os.path.basename(hexfile), hash, crc, startAddr, binSize, dateTimeFormat, picVersion, padBoundary )
    n = len(metaData)

    # Write out binary file with pre-pended meta data
    with open( awsFileName, "wb") as awsFile:
        awsFile.write( bytearray(metaData.encode()))
        padSize = padBoundary - len(metaData)
        padBuffer = bytearray( padSize )
        awsFile.write( padBuffer )
        awsFile.write( contents )
    return 0
	
if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description="Drinkworks Model-B HEX to AWS convertion utility")
    parser.add_argument("picVersion", help="PIC18 Firmware Version, e.g. \'1.01\'", type=float)
    parser.add_argument("picReleasePath", help="set PIC Release path")
    parser.add_argument('--destination', '-d', help="set the destination path. Default is the same as the PIC ReleasePath")
    parser.add_argument('--start', '-s', help="set the Image extraction starting address . Default is \'0x1000\'", default=0x1000, type=int)
    parser.add_argument('--length', '-l', help="set the Image extraction length. Dafault is \'0x1F000\'", nargs='?', const=0x1F000, default=0x1F000, type=int)
    parser.add_argument('--pad', '-p', help="set meta-data padding length. Default is \'512\'", nargs='?', const=512, default=512, type=int)
    parser.add_argument('--build', '-b', help="set PIC18 Firmware Build Number. e.g. \'145\'", nargs='?', const=0, default=0, type=int)
    args = parser.parse_args()

    #hexfile = "MODB_v1.01_b145.hex"
    #picVersion = 1.01
	
    if args.destination:
        outputPath = args.destination
    else:
        outputPath = args.picReleasePath
	
    # Try to find the Hex Image file in the PIC release path
    if not os.path.isdir(args.picReleasePath):
        print( "PIC Release Path: {picReleasePath} does not exist" )
        sys.exit(0)

    searchString = f'_v{args.picVersion:4.2f}_b'
    if args.build != 0:
        searchString += f'{args.build:d}'
    print( f'Searching for file with: {searchString}')
	
    count = 0
    picFile = ""
	
    for file in os.listdir(args.picReleasePath):
        if file.endswith(".hex") and searchString in file:
            hexfile = args.picReleasePath + "\\" +file
            count += 1
	
    if count == 0:
        print("PIC Hex Image file not found")
        sys.exit(0)
    if count > 1:
        printf("More than one matching .hex file found!")
        sys.exit(0)

    print( f'Found hex file: {hexfile}')
    status = create_aws( hexfile, outputPath, args.start, args.length, args.picVersion, args.pad)
    print( f'Status = {status:d}')
    sys.exit(0)
