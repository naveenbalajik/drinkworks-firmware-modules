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
import libscrc
import re
import argparse
import functools
import shutil


# Update this version number with subsequent releases
utilityVersion = "1.0"

POLYNOMIAL = 0x1021
PRESET = 0

scanlines = list()

#
# CRC16 support
#

#
# Build CRC look-up table
#
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

#
# Add byte to CRC calculation, using lookup table
#
def _update_crc(crc, c):
    cc = 0xff & c

    tmp = (crc >> 8) ^ cc
    crc = (crc << 8) ^ _tab[tmp & 0xff]
    crc = crc & 0xffff
    return crc

#
# Calculate CRC on a string
#
def crc(str):
    crc = PRESET
    for c in str:
        crc = _update_crc(crc, ord(c))
    return crc

#
# Calculate a CRC on a byte array
#
def crcb(ba):
    crc = PRESET
    for c in ba:
        crc = _update_crc(crc, c)
    return crc

#
# Strip off type casting
#
# Strip off type casting:
#	(uint8_t)
#	(uint16_6)
#
def typeCast( string ):
    typecast = re.compile( r"(\(uint[0-9]+_t\))\s+(['a-f|A-F|0-9]+)" )
    n = typecast.match( string )
    if n:
        value = string_Value( n.group(2) )
#        print( f'value = {n.group(2)}')
    else:
        value = string_Value( string )
    return value

#
# Look for binary operation
#
# Binary operation Macro expansion
#  <v1> <op> <v2>
# Where
#    v1, V2:  ( .... ) or [a-f|A-F|0-9|_]
#    op: +,*, -
#
# If v1 and/or v2 of the form (... ) recurse
#
def binaryOperation( string ):	
    lrv = '(\(.+\)|[a-z|A-Z|0-9|_]+)'			# left or right value
    opr = '([\*\+-])'							# operator
    pattern = f'\s*{lrv}\s+{opr}\s+{lrv}'
    reSplit = re.compile( pattern )
    results = reSplit.match( string );
    if results:
#        print( f'v1:{results.group(1)} op:{results.group(2)} v2:{results.group(3)}' )
        v1 = strip_parentheses( results.group(1) )
        v2 = strip_parentheses( results.group(3) )
        op = results.group(2)
        if op == '+':
            calc = v1 + v2
        if op == '*':
            calc = v1 * v2
        if op == '-':
            calc = v1 - v2
#        print( f'{v1} {op} { v2} = {calc}' )
        value = calc
    else:
#        print( "No binary operation")
        value = typeCast( string )
    return value

#
# Strip off outer parantheses
#
def strip_parentheses( string ):
    # look for outer parentheses
    outer = re.compile("\((.+)\)")
    m = outer.search( string )
    if m:
#        print( f'inner: {m.group(1)}')
        value = binaryOperation( m.group(1) )
    else:
        value = binaryOperation( string )
    return value		


#
# Swap bytes of 16-bit word value
#
def byteswap(word):
    return( ( word >> 8 ) + ( ( word & 0x00ff ) << 8 ) )

#
# Strip out C-style comments
#
#	Basic functionality:
#		// .....
#		/* .... */
#
def stripcomments(text):
    return re.sub('//.*|/\*.*\*/', '', text)
	
#
# Find a defined constant from the lines array
#
# C-style constants are of the form
#  "#define   <tag>  <value>"
#
#	
def find_constant_definition( tag, lines):
    # Search string includes <tag>
    reTag = re.compile( f'^#define\s*{tag}\s*(.*)')
    value = 0
    tagFound = False
#    print( f'Looking for {tag}')
	
    # Find the tag line	
    for line in lines:
        z = reTag.match( line )
        if z:
            # Strip off any C-style comments
            const = stripcomments( z.group(1) )
            #print( f'{tag}:: {const}')
            value = strip_parentheses( const )
            # terminate search on first match
            tagFound = True
            break
#    print( f'{tag} = {value:#x}')

    if not tagFound:
        print( f'No value found for {tag}' )

    return value

#
# Extract a numeric value from input string
#
# string can be in the form of:
#    decimal:  1234
#    hexadecimal: 0x1234
#    C-stype character:  'E'
#    defined constant:  OOBE_STAGE1_DEFAULT
#
# For defined constants the source file (and included files) will be scanned for the first occurance of
# a valid C-style constant definition.
#	e.g.  "#define  OOBE_STAGE1_DEFAULT  0xffff"
#
# Returns numeric value

def string_Value( string ):
    number = re.match(r'0x[0-9|A-F|a-f]+', string)
    if (number != None):
        value = int(string, 16)
    else:
        number = re.match(r'[0-9]+', string)
        if number != None:
            value = (int(string))
        else:
              # next look for C-style character, keep it simple
            patt = r"'([A-Z|a-z|0-9])'"
            if re.search( patt, string ) is not None:
                pattern = re.compile(r"'([A-Z|a-z|0-9])'")
                for match in pattern.finditer( string ):
                    ba = bytearray( match.group(1).encode() )
                    value = ba[0]
                    #print( f'value = {value:#x}')
            else:
                # lastly, look for defined constants
                #print( f'Defined constant: {string}')
                value = find_constant_definition( string, scanlines )
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
    pattern = re.compile(r"(\(uint[0-9]+_t\))(['_|a-z|A-Z|0-9]+)")
	
	# find data items in line, accumulate bytes in databytes
    for match in pattern.finditer(line):
        value = string_Value( match.group(2) )
        if '16' in match.group(1):
            value = byteswap( value )
            ba = bytearray( divmod(value, 256))
            databytes.append( ba[0] )
            databytes.append( ba[1] )
        if '8' in match.group(1):
            databytes.append( value )

    datastring = (' '.join(format(x, '02x') for x in databytes ) )
    crc = crcb( databytes)
    crcValueString = f'0x{byteswap(crc):04x},\t\t/* {datastring} -> 0x{crc:04x} */'
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

#
# Read Include file, if present, add lines to scanlines list
#
def readInclude( line ):
    pattern = re.compile( r'#include\s*"([\._|a-z|A-Z|0-9]+)"')
    z = pattern.match( line )
    if z:
        filename = sourcePath + "\\" + z.group( 1 )
	    # Check that include file exists
        if os.path.exists( filename ):
            try:
                file = open(filename, 'r')
                includeLines = file.readlines()
                for iline in includeLines:
                    scanlines.append( iline )	
            except OSError:
                print( f'Could not open/read file {filename}')
	
    return



# Scan file
def scanSourceFile( filename, verbose ):

    # Open file for Read and Writing
    try:
        file = open(filename, 'r+')
    except OSError:
        print( f'Could not open/read file {filename}')
        sys.exit(1)
	
    basename = os.path.basename( filename )
	
    global filelines
    filelines = file.readlines()	

    outlines = list()
	
    mode = 'Search'

    # Scanner has 4 modes:
    #  Search - Seaching for CRC_START
    #  Data - data between CRC_START and CRC_VALUE (one line)
    #  Value - Calculating/Replacing bewteen CRC_VALUE and CRC_END (one line)
	#
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
            elif line.startswith( r'#include' ):
#                print( f'Include file: {line}' )
                readInclude( line )
            else:
                # Add to scan lines
                scanlines.append( line )
				
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
                if verbose == True:
                    print( f'\t{field:15}\t\t = {calculatedCRC}' )
                outlines.append( f'\t{field:15}\t\t = {calculatedCRC}\n' )

    # compare filelines with outlines
    if outlines == filelines:
        print( f'No changes to {basename} required' )
    else:		
        # If changes were made, write outlines to file
        file.seek( 0 )
        file.writelines( outlines )
        print( f'Updated {basename}' )
    return

#
#	"main" entry point
#	
if __name__ == "__main__":

    global sourcePath
	
    # Set up the arguments
    parser = argparse.ArgumentParser(description= f'Drinkworks CRC Pre-processor Utility, v{utilityVersion}')
    parser.add_argument("sourcefile", help="Source Filename")
    parser.add_argument('--verbose', help="Verbose output", action='store_true')
    args = parser.parse_args()

	# Check that Source file exists
    if not os.path.exists( args.sourcefile ):
        print( f'ERROR: Input file \"{args.sourcefile}\" does not exist')
        sys.exit(1)
	
	# Extract directory name
    sourcePath = os.path.dirname( args.sourcefile )
	
    scanSourceFile( args.sourcefile, args.verbose )

    print("Exiting Program")
    sys.exit(0)
	