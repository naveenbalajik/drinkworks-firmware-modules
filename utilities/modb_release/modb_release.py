import sys
import subprocess
import os
import time
import serial
import shutil, os
import datetime

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
	
if __name__ == "__main__":

    if len(sys.argv) != 4:
        print("Usage: modb_release.py <version> <build> <PIC>\r\n")
        print(" where <version> = release version, e.g. 1.020\r\n")
        print("       <build>   = build number, e.g. 35\n\r")
        print("       <PIC>     = PIC Version, e.g. 1.01\n\r")
        sys.exit(0)

    version = "v" + sys.argv[1]
    buildNumber = sys.argv[2]
    picVersion = sys.argv[3]
	
    picPath = "\\..\\..\\..\\..\\..\\Desktop\\FirmwareImages\BM1"
    releasePath = "\\..\\..\\..\\releases\\"
    buildPath = "\\..\\..\\.."
	
    SevenZip = "\"c:\\Program Files\\7-Zip\\7z\""
	
    # Release file list, from build directory
    files = [
        "build\\dw_ModelB.bin",
        "build\\bootloader\\bootloader.bin",
        "build\\partition_table\\partition-table.bin",
        "build\\ota_data_initial.bin",
        "releases\\common\\dw_MfgTest.bin",
		"modules\\utilities\\modb_program\modb_program.py"
    ]
    fileList = []
	
    cwd = os.getcwd()
	
    fullReleasePath = os.path.realpath( cwd + releasePath + version)
    fullBuildPath = os.path.realpath( cwd + buildPath)

    print("Releasing version: " + version)
    print( "Release path: " + fullReleasePath)
    print( "Build path: " + fullBuildPath)
    if os.path.isdir(fullReleasePath):
        if not os.listdir(fullReleasePath):
            print("folder " + fullReleasePath + " is empty")
        else:
            print("folder: " + fullReleasePath + " already exits and is not empty!")
            sys.exit()
    else:
        print("create folder: " + fullReleasePath)
        os.mkdir(fullReleasePath)

    # locate PIC Image
    fullPicPath = os.path.realpath( cwd + picPath + "\\v" + picVersion + "\\")
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
   
    # Process file list, make each entry a full path
    for file in files:
         fileList.append(fullBuildPath + "\\" + file)

    #Add PIC file to list
    fileList.append(fullPicPath + "\\" + picFile)
	
    # Copy Build file to destination
    for file in fileList:
        print("Copy: " + file)
        shutil.copy( file, fullReleasePath )


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
        bootloader = fullBuildPath + "\\build\\flash_bootloader_args"
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
        project = fullBuildPath + "\\build\\flash_project_args"
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

    # Use 7-zip to create hash values for each file, except Readme.txt
    systemCommand = "\"c:\\Program Files\\7-Zip\\7z\" h -scrcsha256 " + fullReleasePath +"\\* -x!Readme.txt"
    hashOutput = os.popen( systemCommand ).read()

    # create ReadMe.txt file
    readmeFile = fullReleasePath + "\\Readme.txt"
    separator = "\n========================================================================\n"
    from datetime import datetime
    now = datetime.now()
    dateTimeFormat = now.strftime("%m/%d/%Y %H:%M:%S")
	
    with open(readmeFile, "w") as readme:
        readme.write("Drinkworks Model-B Appliance\n")
        readme.write("ESP32 Firmware\n")
        readme.write("Version: " + sys.argv[1] + "\n")
        readme.write("Build: " + buildNumber + "\n")
        readme.write("Date: " + dateTimeFormat )
        readme.write(separator)
        readme.write(hashOutput)

    # Use 7-zip to package everything update
    archiveName = fullReleasePath + "\\ModelB_ESP_" + version + "b" + buildNumber + ".zip"
    print( archiveName )
    systemCommand = SevenZip + " a " + archiveName + " " + fullReleasePath + "\\*"
    os.system(systemCommand)
	
    print("Exiting Program")
