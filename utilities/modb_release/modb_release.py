import sys
import subprocess
import os
import time
import serial
import shutil, os
import datetime
import argparse
import filecmp

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
    print ("Reading Flash args from " + argFile)
    try:
        file =open(argFile, 'r')
    except OSError:
        print("Could not open/read file: " + argFile)
        sys.exit()

    with file:
        data = file.read().replace('\n', ' ')
    return data

#
# If Update is True compare source and destination files.  If they do not match
# only copy source to desitination after user confirmation.
#
# If destination is a directory the filename is extracted from the source.
# If destination is a file, then that path is uses, unmodified
# If destination is a file, but file is not present, source is copied
def compare_copy_file(source, destination, update):
    copyFile = False
    fileName = os.path.basename(source)
    if update:
        if os.path.isfile(destination):
            target = destination
        else:
            target = destination + "\\" + fileName
        if os.path.exists(target):
            result = filecmp.cmp(source, target, shallow=False)
            if not result:
                print( f'Compare {source} with {target}')
                print( f'{source} has changed')
                response = input("Over-write? (Y/N): ")
                if ('Y' or 'y') in response:
                    copyFile = True
        else:
            copyFile = True
    else:
        copyFile = True
		
    if copyFile:
        print("Copy: " + source)
        shutil.copy( source, destination )
    else:
        print("Skipping: " + source)

#
# Pad source file with 0xff bytes to be aligned on padBoundary, save as destination file
#
def pad_file( source, destination, padBoundary):
    # calculate pad buffer
    fileSize = os.path.getsize( source )
    padSize = padBoundary - (fileSize%padBoundary)
    padBuffer = bytearray(padSize)
    #print(f'Padding with {padSize:d} bytes')
    i = 0
    while i < padSize:
        padBuffer[i] = 255
        i += 1
    # Make copy of source file
    shutil.copy( source, destination )
	# Pad Destination file
    with open(destination, "ab") as file:
        file.write(padBuffer)
	
if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description="Drinkworks Model-B ESP32 Firmware Release")
    parser.add_argument("version", help="set the ESP Version number")
    parser.add_argument("build", help="set ESP Build number")
    parser.add_argument("picVersion", help="PIC18 Firmware Version")
    parser.add_argument('--source', '-s', help="set the source build directory. Default is \'build\'")
    parser.add_argument('--clear', '-c', action='store_true', help="Clear the release directory")
    parser.add_argument('--message', '-m', nargs='*', help="set Release Note Message. Quote each line separately")
    parser.add_argument('--update', '-u', action='store_true', help="Update an existing release")
    args = parser.parse_args()

    # Set default variables
    version = args.version
    buildNumber = args.build
    picVersion = args.picVersion
    sourceDir = "build"
    internalRelease = False
	

    # Overwrite values based on optional variables
    if args.source:
        sourceDir = args.source
        internalRelease = True
    clearRelease = args.clear
	
    # Project root take from CWD to the project root.  Everything else is relative to the project Root	
    projectRoot = "\\..\\..\\.."
    releaseDir = "\\releases\\"
    picDir = "\\..\\..\\Desktop\\FirmwareImages\BM1"
	
    SevenZip = "\"c:\\Program Files\\7-Zip\\7z\""
	
    # Release file list, from build directory
    buildFiles = [
        "dw_ModelB.bin",
        "bootloader\\bootloader.bin",
        "partition_table\\partition-table.bin",
        "ota_data_initial.bin"
    ]

    # Other release files
    otherFiles = [
        "releases\\common\\dw_MfgTest.bin",
		"modules\\utilities\\modb_program\modb_program.py",
		"modules\\docs\\Model-B ESP32 Firmware Programing.pdf"
    ]
    fileList = []
	
    cwd = os.getcwd()
	
    fullProjectRoot = os.path.realpath( cwd + projectRoot)
    fullReleasePath = fullProjectRoot + releaseDir + "v" + version
    fullLogPath = fullProjectRoot + releaseDir
    fullBuildPath = fullProjectRoot + "\\" + sourceDir

    # Clear Release Directory?
    if clearRelease:
        if os.path.isdir(fullReleasePath):
            if not os.listdir(fullReleasePath):
                print("folder " + fullReleasePath + " is already empty")
            else:
                print("Clear folder: " + fullReleasePath)
                shutil.rmtree(fullReleasePath)
        else:
            print("Folder: " + fullReleasePath + " does not exist")
        sys.exit(0)
		
    print("Releasing version: " + version)
    print( "Release path: " + fullReleasePath)
    print( "Build path: " + fullBuildPath)
    if os.path.isdir(fullReleasePath):
        if not os.listdir(fullReleasePath):
            print("folder " + fullReleasePath + " is empty")
        elif not args.update:                        # exit if update argument not set
            print("folder: " + fullReleasePath + " already exits and is not empty!")
            sys.exit()
    else:
        print("create folder: " + fullReleasePath)
        os.mkdir(fullReleasePath)

    # Path for temporary padded Image
    tempImagePath = fullReleasePath + "\\temp.bin"

    # Path for Application Image
    applicationImagePath = fullReleasePath + "\\dw_ModelB_v" + version + "b" + buildNumber + ".bin"

    # locate PIC Image
    fullPicPath = os.path.realpath( fullProjectRoot + picDir + "\\v" + picVersion + "\\")
    count = 0
    picFile = ""
	
    for file in os.listdir(fullPicPath):
        if file.endswith(".aws"):
            picFile = file
            count += 1
	
    if count == 0:
        print("PIC Image file not found")
        sys.exit(0)
    if count > 1:
        printf("More than one .aws file fount!")
        sys.exit(0)
   
    # Process the build files
    for file in buildFiles:
        fileList.append( fullBuildPath + "\\" + file)

    # Process other file list, make each entry a full path
    for file in otherFiles:
         fileList.append(fullProjectRoot + "\\" + file)

    # Add PIC file to list
    fileList.append(fullPicPath + "\\" + picFile)
	
    # Copy Build files to destination
    for file in fileList:
        compare_copy_file( file, fullReleasePath, args.update)
		# Copy application image to new file
        # If appliaction image file
        if "dw_ModelB.bin" in file:
            # Create a temporary padded image file
            pad_file( file, tempImagePath, 16)
            # Copy to file with version/build in name
            compare_copy_file( tempImagePath, applicationImagePath, args.update)
            # Delete temp file
            os.remove(tempImagePath)
	
# Other files: MODBxxxx.aws (run utility)
#              Readme.txt (release notes)

    #
    # Construct a programming arguments file
    #   PIC Factory Image is at fixed address (0x70000)
    #   Manufacturing Test Image has fixed name and fixed address (0x800000)
	#
    pgmFile = fullReleasePath + "\\flash_project_args"
    with open(pgmFile, "w") as argsFile:
        #Copy, with edit, Bootloader args
        bootloader = fullBuildPath + "\\flash_bootloader_args"
        with open(bootloader, "r") as bootloaderArgs:
            for line in bootloaderArgs:
                #Edit bootloader line
                if "bootloader/bootloader" in line:
                    line = line.replace("bootloader/bootloader", "bootloader")
                # Replace flash_size detect with 16M
                if "flash_size" in line:
                    line = line.replace("detect", "16MB")
                argsFile.write(line)
        #Copy, with edit, Project args
        project = fullBuildPath + "\\flash_project_args"
        with open(project, "r") as projectArgs:
            for line in projectArgs:
                #Edit partition-table line
                if "partition_table/" in line:
                    line = line.replace("partition_table/", "")
                if line != "\n" and line != " \n":
                    argsFile.write(line)
                if "ota_data_initial" in line:
                    #Add PIC Factory Image, after ota data initial
                    argsFile.write("0x70000 " + picFile + "\n")
        #Add Manufacturing Test Image
        argsFile.write("0x800000 dw_MfgTest.bin\n")

    #
    # Update release.log file
    #
	
    versionBuild = "v" + version + " b" + buildNumber
	
    # Time/Date stamp the release
    from datetime import datetime
    now = datetime.now()
    dateTimeFormat = now.strftime("%m/%d/%Y %H:%M:%S")

    # Append record to the log file
    logFile = fullLogPath + "\\release.log"
    startOfNote = False
    logLines = []
	
	# Read in existing file
    with open(logFile, "r") as log:
        logLines = log.readlines()
	
    # Look for note(s) for existing vesrion/build, tag as "deleted"
    n = 0
    while n < len(logLines):
        if not logLines[n].startswith( "#"):
            if logLines[n].isspace():
                startOfNote = True
            elif startOfNote:
                startOfNote = False
                if (versionBuild in logLines[n]) and not ("(deleted)" in logLines[n]):
                    print("Log already has note for version: " + versionBuild + " - tag as deleted")
                    logLines[n] = logLines[n].replace("\n", " (deleted)\n")
            else:
                startOfNote = False
        n += 1
    
    # Construct new release note
    newNote = versionBuild + " " + dateTimeFormat
    if internalRelease:
        newNote += " (internal)\n"
    else:
        newNote += "\n"
    logLines.append("\n")
    logLines.append(newNote)
    logLines.append("  PIC firmware: " + picFile + "\n")
	
    # if a message was specified, it can be multi-line, append each line
    if args.message:
        for line in args.message:
            logLines.append("  " + line + "\n")
    # Write out ammended log file
    with open(logFile, "w") as log:
        log.writelines(logLines)

    # Use 7-zip to create hash values for each file, except Readme.txt and previous ZIP file
    systemCommand = SevenZip + " h -scrcsha256 " + fullReleasePath +"\\* -x!Readme.txt -x!*.zip"
    hashOutput = os.popen( systemCommand ).read()

    # create ReadMe.txt file
    readmeFile = fullReleasePath + "\\Readme.txt"
    separator = "\n\n============================================================================================\n"
    skipNote = False

    with open(readmeFile, "w") as readme:
        readme.write("Drinkworks Model-B Appliance\n")
        readme.write("ESP32 Firmware\n")
        readme.write("Version: " + version + "\n")
        readme.write("Build: " + buildNumber + "\n")
        readme.write("Date: " + dateTimeFormat )
        readme.write(separator)
		
        # Copy release notes from log file
        #   Always skip lines starting with '#'
        #   Always skip deleted notes
        #   skip internal notes for external release
        for line in logLines:
            if not line.startswith("#"):
                if line.isspace():
                    startOfNote = True
                else:
                    if startOfNote:
                        if "(deleted)" in line:
                            skipNote = True
                        elif "(internal)" in line and not internalRelease:
                            skipNote = True
                        else:
                            skipNote = False
                            readme.write("\n")
                        startOfNote = False
						
                    # Unless skipping this note, write line to Readme file
                    if not skipNote:
                        readme.write(line)
                   
        # Add Hash values
        readme.write(separator)
        readme.write(hashOutput)

     # Use 7-zip to package everything update, exclude preious ZIP file (if present)
    archiveName = fullReleasePath + "\\ModelB_ESP_" + version + "b" + buildNumber + ".zip"
    print( archiveName )
    systemCommand = SevenZip + " a -x!*.zip " + archiveName + " " + fullReleasePath + "\\*"
    os.system(systemCommand)
	
    print("Exiting Program")
