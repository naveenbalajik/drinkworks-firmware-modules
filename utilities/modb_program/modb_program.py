import sys
import subprocess
import os
import time
import serial
import argparse

# Update this version number with subsequent releases
utilityVersion = "1.3"

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

def get_flash_arg(argFile):
    try:
        file =open(argFile, 'r')
    except OSError:
        print("Could not open/read file: " + argFile)
        sys.exit(1)

    with file:
        data = file.read().replace('\n', ' ')
    return data

def check_serial_port(port):
    try:
        s = serial.Serial(port)
        s.close()
        return True
    except (OSError, serial.SerialException):
        return False

#
# Model-B Appliance ESP Programming Script
#
#	Exit codes:
#		0 = Success
#		1 = Could not open programming argument file
#		2 = Command line usage error
#		3 = Board already programmed (encryption/secure boot key present)
#		4 = ESP Erase failure
#       5 = Can't access COM port

if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description=("Drinkworks Model-B ESP32 Firmware Programming Script, v" + utilityVersion), allow_abbrev=True)
    parser.add_argument("port", help="set ESP Programming Port, e.g. COM17")
    parser.add_argument('--flashArgs', '-f', help="set flash programing arguments file. Default is \'flash_project_args\'", default="flash_project_args")
    args = parser.parse_args()

    COMport = args.port
    print ("COM port: " + COMport)

    if( not check_serial_port( args.port)):
        print("Can't access port: " + args.port)
        sys.exit(5)
		
    flashArgsString = get_flash_arg(args.flashArgs)

    print("Checking espfuse to see if board already programmed")
    systemCommand = "espefuse.py --port " + COMport + " summary"
    output = str(subprocess.check_output(systemCommand, shell=True, timeout=10), 'utf-8')

    flashKeyString = "Flash encryption key"
    flashKeyVal = get_val_from_key(flashKeyString, output)

    secureBootKeyString = "Secure boot key"
    secureBootKeyVal = get_val_from_key(secureBootKeyString, output)

    VB3KeyString = "Variable Block 3"
    VB3Val = get_val_from_key(VB3KeyString, output)

    # Check to see if there are any question marks
    if checkForQuestionMarks(flashKeyVal) or checkForQuestionMarks(secureBootKeyVal) or checkForQuestionMarks(VB3Val):
        print("BOARD ALREADY PROGRAMMED")
        print("Aborting...")
        sys.exit(3)
    else:
        print("Board not encrypted. Proceed with programming.")

    print("-----Programming Board-----")
    print("Erasing Flash...")
    systemCommand = "Esptool.py -p " + COMport + " -b 460800 erase_flash"
    output = str(subprocess.check_output(systemCommand, shell=True, timeout=100), 'utf-8')
    print( output )
	
    # Verify that erase was succesful before continuing
    if not "Chip erase completed successfully in" in output:
        print( "Erase failed - aborting programming" )
        sys.exit(4)
	
    print("Programming Flash...")
    systemCommand = "Esptool.py -p " + COMport + " -b 460800 --after no_reset write_flash " + flashArgsString
    print(systemCommand)
    os.system(systemCommand)

    ser = serial.Serial(COMport, 115200, exclusive=True, rtscts=0, dsrdtr=0, timeout=10)
    # rts and dtr are set to 1 by the OS. Need to hard set them to 0 after opening the serial port
    ser.dtr = 1
    ser.rts = 1
    ser.dtr = 0
    time.sleep(1)
    ser.rts = 0
    startTime = time.time()
    secondTimeout = 120
    completeOutput = ''

    while True:
        timePassed = time.time() - startTime
        if timePassed < secondTimeout:
            data = ser.read(ser.in_waiting or 1)
            if data:
                try:
                    decodedData = data.decode("utf-8")
                    sys.stdout.write(decodedData)
                    completeOutput += decodedData

                    if 'Drinkworks' in completeOutput:
                        sys.stdout.write("Drinkworks String Found\r\nBootloader Encryption Completed\r\n")
                        break
                except:
                    pass

        else:
            sys.stdout.write("Drinkworks not found in " + str(secondTimeout) + "s\r\nEncryption Timeout\r\n")
            break

    print("Exiting Program")
    sys.exit(0)

