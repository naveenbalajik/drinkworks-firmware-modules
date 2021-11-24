#
# CRC Pre-processor
#
#	• Phase 1 (no macros)3
#		○ Scan file, extract data arrays
#		○ Convert 16-bit values to 8-bit pairs
#		○ Calculate CRC
#		○ Insert in file
#		○ Add valid tag with byte array
#		○ Error if START but no END
#		○ Error if END but no VALUE
#	• Phase 2
#		○ Add macro substitution for constants
#		○ Scan include files
#			§ Look for required items, rather than scan everything
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
import shutil


# Update this version number with subsequent releases
utilityVersion = "1.0"

#filelines = []
constants = []

POLYNOMIAL = 0x1021
PRESET = 0

def _initial(c):
    crc = 0
    c = c << 8
    for j in range(8):
        if (crc ^ c) & 0x8000:
            crc = (crc << 1) ^ POLYNOMIAL
        else:
            crc = crc << 1
        c = c << 1
    return crc

_tab = [ _initial(i) for i in range(256) ]

def _update_crc(crc, c):
    cc = 0xff & c

    tmp = (crc >> 8) ^ cc
    crc = (crc << 8) ^ _tab[tmp & 0xff]
    crc = crc & 0xffff
#    print (crc)

    return crc

def crc(str):
    crc = PRESET
    for c in str:
        crc = _update_crc(crc, ord(c))
    return crc

def crcb(ba):
    crc = PRESET
    for c in ba:
        crc = _update_crc(crc, c)
    return crc
	
	
def lines_that_equal(line_to_match, fp):
    return [line for line in fp if line == line_to_match]

def lines_that_contain(string, fp):
    print( f'Looking for {string}')
    return [line for line in fp if string in line]

def lines_that_start_with(string, fp):
    return [line for line in fp if line.startswith(string)]

def lines_that_end_with(string, fp):
    return [line for line in fp if line.endswith(string)]

def string_Value( string ):
    number = re.match(r'0x[0-9|A-F|a-f]+', string)
    if (number != None):
        value = int(string, 16)
    else:
        number = re.match(r'[0-9]+', string)
        if number != None:
            value = (int(string))
        else:
            print( f'Defined constant: {string}')
            value = 0
    return value

def byteswap(word):
    return( ( word >> 8 ) + ( ( word & 0x00ff ) << 8 ) )
	
def find_constant_definition( string, lines):
    ss = f'^#define\s*{string}\s*([0-9|A-F|a-f|x|X]+)'
    value = 0
    pattern = re.compile(ss)
    for line in lines:
        z = pattern.match(line)
        if z:
#            print( f'{string} = {z.group(1)}')
            value = string_Value( z.group( 1 ) )
#            print( f'{string} = {string_Value(z.group(1))}')
#    print( f'{string} = {value:#x}')
    return value
	
def word_value( string ):
    number = re.match(r'[0-9]+', string)
    if (number != None):
        value = (int(string))
        #print( f'word_value: {value:#x}')
    else:
        number = re.match(r'0x[0-9|A-F|a-f]+', string)
        if number != None:
            print( f'Hex number: {number}')
        else:
#            print( f'Defined constant: {string}')
			# Add defined constant to list
#            constants.append( string )
            value = find_constant_definition(string, filelines )
			
    return bytearray( divmod(value, 256))
	
def byte_value( string ):
    number = re.match(r'0x[0-9|A-F|a-f]+', string)
    if number != None:
        value = int(string, 16)
        #print( f'byte_value: {value:#x}')
        #print( f'Hex number: {number}')
    else:
        number = re.match(r'[0-9]+', string)
        if number != None:
            value = (int(string))
            #print( f'byte_value: {value:#x}')
        else:
#            print( f'Defined constant: {string}')
            value = find_constant_definition( string, filelines )
    return value

#
# Parse line containing data items to be CRC'd
#
# Data items must be prefixed with:
#    (uint8_t) for 8-bit values
#    (uint16_t) for 16-bit values
#
# Returns formated string containing the CRC value (byte reversed)
# with appended C-style comment containing the data bytes and CRC value.
#
# Example output:
#    "0x8951,         /* 68 01 b4 00 68 01 -> 0x5189 */"
#
def parse_data( line ):
    databytes = bytearray()
	#print( f'Data: {line}')
    #print(re.split('^\s*\.[_A-Z|a-z0-9|\s]+=\s*', line))
    #print(re.split('\s*\.[_A-Z|a-z0-9|\s=]+', line))
    #print(re.split('^[^=]*=\s*', line))
    #data = re.search(r'(\b[a-z|A-Z|0-9]+\b)\s*\{?(.*),$', line)
#    data = re.search(r'(\b[a-z|A-Z|0-9]+\b)\s*\{?(.*),$', line)
#    print( data.groups() )
#    pattern = re.compile(r'(\b\(uint[0-9]+_t\)[a-z|A-Z|0-9]+,\b)')
    pattern = re.compile(r'(\(uint[0-9]+_t\))([_|a-z|A-Z|0-9]+)')
#    pattern = re.compile(r'(\b"(uint8_t)"[a-z|A-Z|0-9]+,\b)')
    for match in pattern.finditer(line):
        if '16' in match.group(1):
            #print( f'Word: {match.group(2)}')
            ba = word_value( match.group(2) )
            #print( f'{ba[0]:#x},{ba[1]:#x}')
            databytes.append( ba[1] )
            databytes.append( ba[0] )
        if '8' in match.group(1):
            #print( f'Byte: {match.group(2)}')
            
            databytes.append( byte_value( match.group(2)) )
    #print( 'databytes: ' + (' '.join(format(x, '02x') for x in databytes ) ) )
    datastring = (' '.join(format(x, '02x') for x in databytes ) )
    crc = crcb( databytes)
    #print( f'CRC: {crc:#x}')
    crcValueString = f'0x{byteswap(crc):04x},\t\t/* {datastring} -> 0x{crc:04x} */'
    #print( f'CRC: 0x{byteswap(crc):04x},\t\t/* {datastring} -> 0x{crc:04x} */')
    #print( f'CRC: {crcValueString}')
    return crcValueString
	
#
# Parse line containing CRC value
#
# Returns crc field name
#
# Example output:
#    ".ventCRC"
#
def parse_value( line ):
    pattern = re.compile(r'\s*(\.[|a-z|A-Z|0-9]+)')
    z = pattern.match( line )
    if z:
        field = z.group( 1 )
    return field




# Scan file
def scanSourceFile(filename):
    try:
        file = open(filename, 'r')
    except OSError:
        print( f'Could not open/read file {filename}')
        sys.exit(1)
	
    global filelines
    filelines = file.readlines()	

    outlines = list()
	
    mode = 'Search'

    # Scanner has 4 modes:
    #  Search - Seaching for CRC_START
    #  Data - data between CRC_START and CRC_VALUE (one line)
    #  Value - Calculating/Replacing bewteen CRC_VALUE and CRC_END (one line)
	#
#    for line in file:
    for line in filelines:
        if mode == 'Search':
            if "/*#* _CRC_START_ *#*/" in line:
                mode = 'Data'
            elif "/*#* _CRC_VALUE_ *#*/" in line:
                print( 'Error: CRC_VALUE before CRC_START')
                sys.exit(2)
            elif "/*#* _CRC_END_ *#*/" in line:
                print( 'Error: CRC_END before CRC_START')
                sys.exit(3)
            # copy line to output
            outlines.append( line )
				
        elif mode == 'Data':
            if "/*#* _CRC_VALUE_ *#*/" in line:
                mode = 'Value'
            elif "/*#* _CRC_START_ *#*/" in line:
                print( 'Error: CRC_START after CRC_START')
                sys.exit(4)
            elif "/*#* _CRC_END_ *#*/" in line:
                print( 'Error: CRC_END after CRC_START')
                sys.exit(5)
            else:
                calculatedCRC = parse_data( line )
            # copy line to output
            outlines.append( line )
				
        elif mode == 'Value':
            if "/*#* _CRC_END_ *#*/" in line:
                mode = 'Search'
                # copy line to output
                outlines.append( line )
            elif "/*#* _CRC_START_ *#*/" in line:
                print( 'Error: CRC_START after CRC_VALUE')
                sys.exit(4)
            elif "/*#* _CRC_VALUE_ *#*/" in line:
                print( 'Error: CRC_VALUE before CRC_VALUE')
                sys.exit(2)
            else:
                #print( f'Value: {line}')
                field = parse_value( line )
                print( f'\t{field:15}\t\t = {calculatedCRC}' )
                outlines.append( f'\t{field:15}\t\t = {calculatedCRC}\n' )
				
    # write outlines to file
    try:
        outfile = open("output.c", 'w')
        outfile.writelines( outlines)
    except OSError:
        print( f'Could not open/ write file')
        sys.exit(9)		
    return
	
if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description= f'Drinkworks CRC Pre-processor Utility, v{utilityVersion}')
    parser.add_argument("sourcefile", help="Source Filename")
    args = parser.parse_args()

	# Check that Source file exists
    if not os.path.exists( args.sourcefile ):
        print( f'ERROR: Input file \"{args.sourcefile}\" does not exist')
        sys.exit(1)

    scanSourceFile(args.sourcefile)

    print("Exiting Program")
    sys.exit(0)
	