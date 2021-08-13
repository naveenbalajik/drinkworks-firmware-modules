import sys
import subprocess
import os
import time
import serial
import shutil, os
import datetime
import argparse
import string
import glob

# Update this version number with subsequent releases
utilityVersion = "1.0"

def get_val_from_key(key, haystack):
    keyLoc = haystack.find(key)
    endOfKeyLoc = keyLoc + len(key)
    # Check for the first = after the key
    startofValIndx = haystack[endOfKeyLoc:].find("=") + endOfKeyLoc + 1
    # ensure the = is close to the key
    numNewLines = haystack[endOfKeyLoc:startofValIndx].count("\n")
    if(startofValIndx - endOfKeyLoc > 40 or numNewLines > 1):
        print ("Error: Value for " + key + " not in expected location")
        return
    else:
        # determine end of string
        endOfValIndex = haystack[startofValIndx:].find("\n") + startofValIndx
        return haystack[startofValIndx:endOfValIndex]

def checkForQuestionMarks(checkString):
    if '?' in checkString:
        print ("ERROR: Found \"?\" in " + flashKeyString)
        return True

def checkForAllQuestionMarks(checkString):
    secure = " ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? -/-"
    if secure in checkString:
        return True

# Close equivalent of UNiX strings utility
def strings(filename, min=4):
    with open(filename, errors="ignore") as file:
        result = ""
        for c in file.read():
            if c in string.ascii_letters + string.digits + string.punctuation + ' ':
                result += c
                continue
            if len(result) >= min:
                yield result
            result = ""
        if len(result) >= min:
            yield result
        
def get_flash_arg(argFile):
    print ("Reading Flash args from " + argFile)
    try:
        file =open(argFile, 'r')
    except OSError:
        print("Could not open/read file: " + argFile)
        sys.exit()

    with file:
        data = file.read().replace('\n', ' ')
    return data

# Read out a partition and look for (and print) strings
def check_partition( port, name, address, length, minMatch):
    outName = name + ".out"
    print("\nChecking: " + name + ", address: " + f'{address:08X}' + ", length: " + f'{length:08X}')
    # Read out partition
    command = "esptool.py --port " + port + " --baud 460800 read_flash " + str(address) + " " + str(length) + " " + outName
    output = str(subprocess.check_output(command, shell=True, timeout=60), 'utf-8')
	# Look for strings
    patternList = list(strings(outName,minMatch))
    n = len(patternList)
    print( "  Found " + str(n) + " strings of at least " + str(minMatch) + " bytes:")
    for s in patternList:
        print("    " + s)

#
# Model-B Appliance ESP Encription Verification
#
# This program should be run on both an encypted appliance and an unencrypted appliance and the data compared
#		
if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description=("Drinkworks Model-B ESP32 Firmware Verifier, v" + utilityVersion), allow_abbrev=True)
    parser.add_argument("port", help="set ESP Programming Port, e.g. COM17")
    args = parser.parse_args()

    print("Deleting temporary image files")
    tempFiles = glob.glob('*.out')
    for f in tempFiles:
        try:
            print("  Deleting: " + f)
            os.remove(f)
        except OSError as e:
            print("error: %s: %s" % (f, e.strerror))
    print("Checking espfuse to see if keys are secure")
    systemCommand = "espefuse.py --port " + args.port + " summary"
    output = str(subprocess.check_output(systemCommand, shell=True, timeout=10), 'utf-8')

    # Check if block 1 is secure
    flashKeyString = "Flash encryption key"
    flashKeyVal = get_val_from_key(flashKeyString, output)
    if checkForAllQuestionMarks(flashKeyVal):
        print("  " + flashKeyString + ": SECURE")
    else:
        print("  " + flashKeyString + ": NOT SECURE !!!")
	
    # Check if block 2 is secure
    secureBootKeyString = "Secure boot key"
    secureBootKeyVal = get_val_from_key(secureBootKeyString, output)
    if checkForAllQuestionMarks(secureBootKeyVal):
        print("  " + secureBootKeyString + ": SECURE")
    else:
        print("  " + secureBootKeyString + ": NOT SECURE")

    
    check_partition( args.port, "Bootloader", 0x1000, 0xD000, 8)         # Check Bootloader is encrypted
    check_partition( args.port, "PartitionTable", 0xE000, 0x1000, 8)     # Check Partition Table is encrypted
    check_partition( args.port, "storage", 0x1A000, 0x10000, 8)          # Check Storage Partition is encrypted
    check_partition( args.port, "picFactory", 0x70000, 0x30000, 10)      # Check PIC Factory Image is encrypted
    check_partition( args.port, "picOTA", 0xA0000, 0x30000, 10)          # Check PIC OTA Image is encrypted
    check_partition( args.port, "espFactory", 0x200000, 0x100000, 10)    # Check ESP Factory Image is encrypted
    print("Exiting Program")
    sys.exit(0)
	
	
