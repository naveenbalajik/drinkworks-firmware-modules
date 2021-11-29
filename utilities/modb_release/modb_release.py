#
# @file modb_release.py
#
# Script to generate an ESP32 firmware release package for Drinkworks Model-B Appliance
#
# Companion Utility: hex2aws.py
#    The hex2aws utility takes a PIC18F/PIC32MX firmware *.hex file and processes it to generate the *.aws
#    file, suitable for uploading to AWS.
#
#    Example metadata:
#    {
#      "HexFileName": "MODB_v1.01_b145.hex",
#      "SHA256": "ggfNPlwbGb7jbjjYFfG4IIEUoKdfMPg0CHzIoVD4p08=",
#      "CRC16_CCITT": 65182,
#      "LoadAddress": 4096,
#      "ImageSize": 126976,
#      "TimeStamp": "2021-07-30T07:45:01.0354828-04:00",
#      "Version_PIC": 1.01,
#      "PaddingBoundary": 512
#     }
#
import sys
import subprocess
import os
import time
import serial
import shutil, os
import datetime
import argparse
import filecmp

# Update this version number with subsequent releases
utilityVersion = "1.4"

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

#
#	Fixup the ESP32 Image File.
#	Pad to 16-byte boundary
#	Copy to ApplicationImage name in Release Folder
#
def fixup_image_file( ImageFile, ReleasePath, ApplicationImage, update ):
    # Path for temporary padded Image
    tempFile = f'{ReleasePath}\\temp.bin'

    # Path for Application Image
    appFile = f'{ReleasePath}\\{ApplicationImage}'

    # Create a temporary padded image file
    pad_file( ImageFile, tempFile, 16)
            
	# Copy to Application file (with version/build in name)
    compare_copy_file( tempFile, appFile, update)
	
    # Delete temp file
    os.remove( tempFile )
	
    return

#
#	Clear Folder
#
def clear_folder( name ):
    if os.path.isdir( name ):
        if not os.listdir( name ):
            print("folder " + name + " is already empty")
        else:
            print("Clear folder: " + name )
            shutil.rmtree( name )
    else:
        print("Folder: " + name + " does not exist")
    return

#
#	Create Folder
#
def create_folder( name ):
    if os.path.isdir(name):
        if not os.listdir(name):
            print( f'folder {name} is empty' )
        elif not args.update:                        # exit if update argument not set
            print( f'folder: {name} already exits and is not empty!' )
            sys.exit()
    else:
        print( f'create folder: {name}' )
        os.mkdir( name )
    return

#
#	Get PIC Path
#
#	Validate that the path exists
#
def get_pic_path( picDir, picVersion ):
    picPath = f'{picDir}\\v{picVersion:4.2f}'
	
    if not os.path.isdir(picPath):
        print( f'PIC release directory not found: {picPath}')
        sys.exit(9)
		
    return picPath

#
# locate PIC18/PIC32 Image, look for a sub-directory with the version number (e.g. Releases\v1.30)
#
#	PicPath will have already been validated
#
def get_pic_image( picPath, picVersion, picBuild, mcu ):
    count = 0

    mcuStr = f'_{mcu}_'
	
    # search for files matching version and (optionally) build number
    searchString = f'_v{picVersion:4.2f}_b'
    if picBuild != 0:
        searchString += f'{picBuild:d}'
    #print( f'Searching for file with: {searchString}')
	
    for file in os.listdir( picPath ):
        if file.endswith(".aws") and searchString in file:
            if mcuStr in file:
                print( f'Found {mcu} Image: {file}')
                picFile = file
                count += 1
	
    if count == 0:
        print( f'{mcu} Image file not found' )
        sys.exit(7)
    if count > 1:
        print( f'More than one {mcu} .aws file found! Try specifying PIC Build number' )
        sys.exit(8)
    #print( f'Found file: {picFile}')
		
    return picFile

#
# Construct a programming arguments file
#   PIC Factory Image is at fixed address (0x70000)
#   Manufacturing Test Image has fixed name and fixed address (0x800000)
#
def construct_flash_args( ReleasePath, BuildPath, PicFile ):

    pgmFile = ReleasePath + "\\flash_project_args"
	
    with open(pgmFile, "w") as argsFile:
	
        #Copy, with edit, Bootloader args
        bootloader = BuildPath + "\\flash_bootloader_args"
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
        project = BuildPath + "\\flash_project_args"
        with open(project, "r") as projectArgs:
            for line in projectArgs:
                #Edit partition-table line
                if "bootloader/bootloader" in line:
                    #skip line if it has bootloader (added that above)
                    continue
                if "partition_table/" in line:
                    line = line.replace("partition_table/", "")
                if line != "\n" and line != " \n":
                    argsFile.write(line)
                if "ota_data_initial" in line:
                    #Add PIC Factory Image, after ota data initial
                    argsFile.write("0x70000 " + PicFile + "\n")
        #Add Manufacturing Test Image
        argsFile.write("0x800000 dw_MfgTest.bin\n")
    return

#
#	Copy release files to release destination
#
#	For "BuildFilename", fixup as "appFilename"
#
def copy_release_files( fileList, ReleasePath, buildFilename, appFilename, update ):	
    for file in fileList:
        compare_copy_file( file, ReleasePath, update)
        # If application image file
        if "dw_ModelB.bin" in file:
            fixup_image_file( file, ReleasePath, applicationFilename, update )
 
#
#	Create Readme file, local to MCU specific release
#
def create_readme( logLines, ReleasePath, version, build, timestamp, internal, ZipPath ):
    # Use 7-zip to create hash values for each file, except Readme.txt and previous ZIP file
    systemCommand = f'"{ZipPath}" h -scrcsha256 {ReleasePath}\\* -x!Readme.txt -x!*.zip'
    hashOutput = os.popen( systemCommand ).read()

    # create ReadMe.txt file
    readmeFile = ReleasePath + "\\Readme.txt"
    separator = "\n\n============================================================================================\n"
    skipNote = False

    with open(readmeFile, "w") as readme:
        readme.write("Drinkworks Model-B Appliance\n")
        readme.write("ESP32 Firmware\n")
        readme.write("Version: " + version + "\n")
        readme.write("Build: " + build + "\n")
        readme.write("Date: " + timestamp )
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
                        elif "(internal)" in line and not internal:
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
    return

#
# Create Release Archive
#
def create_release_archive( ReleasePath, mcu, version, build, ZipPath):
     # Use 7-zip to package everything update, exclude preious ZIP file (if present)
    archiveName = f'{ReleasePath}\\ModelB_ESP_{mcu}_v{version}b{build}.zip'
    print( archiveName )
    systemCommand = f'"{ZipPath}" a -x!*.zip {archiveName} {ReleasePath}\\*'
    os.system(systemCommand)
    return

#
#	Main entry point
#	
if __name__ == "__main__":

    # Some default paths, may be overiden with script arguments
    defaultPicDir = r'c:\Users\ian.whitehead\Desktop\ModelB_DispenseEngine\Releases'
    defaultSevenZip = r'C:\Program Files\7-Zip\7z.exe'
    defaultRootDir = r'c:\Users\ian.whitehead\GitHub\dw_ModelB'
    releaseDir = "\\releases\\"
	
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
	
    # Set up the arguments
    parser = argparse.ArgumentParser(description= f'Drinkworks Model-B ESP32 Firmware Release Utility, v{utilityVersion}')
    parser.add_argument("version", help="set the ESP Version number")
    parser.add_argument("build", help="set ESP Build number")
    parser.add_argument("picVersion", help="PIC18 Firmware Version, e.g. \'1.01\'", type=float)
    parser.add_argument('--source', '-s', help="set the source build directory. Default is \'build\'", default="build")
    parser.add_argument('--clear', '-c', action='store_true', help="Clear the release directory")
    parser.add_argument('--message', '-m', nargs='*', help="set Release Note Message. Quote each line separately")
    parser.add_argument('--update', '-u', action='store_true', help="Update an existing release")
    parser.add_argument('--internal', '-i', action='store_true', help="Internal release")
    parser.add_argument('--picDir', '-p', help="set PIC18F/PIC32MX release directory", default=defaultPicDir)
    parser.add_argument('--rootDir', '-r', help="set ESP32/PIC32MX Project roor directory", default=defaultRootDir)
    parser.add_argument('--picBuild', '-b', help="set PIC18F Firmware Build Number. e.g. \'145\'", default=0, type=int)
    parser.add_argument('--zip', '-z', help="set 7-zip path", default=defaultSevenZip)
	
    args = parser.parse_args()

    # Validate Project root directory
    if not os.path.isdir(args.rootDir):
        print( f'Invalid location for Project Root: {args.rootDir}')
        sys.exit(-1)
	
    # Validate Project release directory
    releasePath = args.rootDir + releaseDir
    if not os.path.isdir(releasePath):
        print( f'Invalid location for Project Release: {releasePath}')
        sys.exit(-1)
	
	# Validate 7-Zip location
    if not os.path.isfile(args.zip):
        print( f'Invalid location for 7-zip: {args.zip}')
        sys.exit(-1)
		
    p18FileList = []
    p32FileList = []
	
    fullP18ReleasePath = f'{releasePath}v{args.version}\\P18'
    fullP32ReleasePath = f'{releasePath}v{args.version}\\P32'
    fullBuildPath = args.rootDir + "\\" + args.source

    # Clear P18 and P32 Release Directory?
    if args.clear:
        clear_folder( fullP18ReleasePath )
        clear_folder( fullP32ReleasePath )
#        if os.path.isdir( fullP18ReleasePath ):
#            if not os.listdir( fullP18ReleasePath ):
#                print("folder " + fullP18ReleasePath + " is already empty")
#            else:
#                print("Clear folder: " + fullP18ReleasePath)
#                shutil.rmtree(fullP18ReleasePath)
#        else:
#            print("Folder: " + fullP18ReleasePath + " does not exist")
        sys.exit(0)
		
    # Clear P32 Release Directory?
#    if args.clear:
#        if os.path.isdir( fullP32ReleasePath ):
#            if not os.listdir( fullP32ReleasePath ):
#                print("folder " + fullP32ReleasePath + " is already empty")
#            else:
#                print("Clear folder: " + fullP32ReleasePath )
#                shutil.rmtree( fullP18ReleasePath )
#        else:
#            print("Folder: " + fullP32ReleasePath + " does not exist")
#        sys.exit(0)
		
    print( "Releasing version: " + args.version)
    print( "P18 Release path: " + fullP18ReleasePath)
    print( "P32 Release path: " + fullP32ReleasePath)
    print( "Build path: " + fullBuildPath)
	
    create_folder( fullP18ReleasePath )
    create_folder( fullP32ReleasePath )
#    if os.path.isdir(fullReleasePath):
#        if not os.listdir(fullReleasePath):
#            print("folder " + fullReleasePath + " is empty")
#        elif not args.update:                        # exit if update argument not set
#            print("folder: " + fullReleasePath + " already exits and is not empty!")
#            sys.exit()
#    else:
#        print("create folder: " + fullReleasePath)
#        os.mkdir(fullReleasePath)

    # locate PIC18/PIC32 Image, look for a sub-directory with the version number (e.g. Releases\v1.30)
    picPath = get_pic_path( args.picDir, args.picVersion )
    p18File = get_pic_image( picPath, args.picVersion, args.picBuild, 'P18' )
    p32File = get_pic_image( picPath, args.picVersion, args.picBuild, 'P32' )

#    # locate PIC18/PIC32 Image, look for a sub-directory with the version number (e.g. Releases\v1.30)
#    p18Count = 0
#    p32Count = 0
#    picFiles = list()

#    picPath = f'{args.picDir}\\v{args.picVersion:4.2f}'
#
#    if os.path.isdir(picPath):
#        # search for files matching version and (optionally) build number
#        searchString = f'_v{args.picVersion:4.2f}_b'
#        if args.picBuild != 0:
#            searchString += f'{args.picBuild:d}'
#        print( f'Searching for file with: {searchString}')
#    	
#        for file in os.listdir( picPath ):
#            if file.endswith(".aws") and searchString in file:
#                if '_P18_' in file:
#                    print( f'Found PIC18 Image: {file}')
#                    p18File = f'{picPath}\\{file}'
#                    p18Count += 1
#                elif '_P32_' in file:
#                    p32File = f'{picPath}\\{file}'
#                    p32Count += 1
#    	
#        if p18Count == 0:
#            print("PIC18 Image file not found")
#            sys.exit(0)
#        if p18Count >= 1:
#            print("More than one PIC18 .aws file found!")
#            sys.exit(0)
#        if p32Count == 0:
#            print("PIC32 Image file not found")
#            sys.exit(0)
#        if p32Count >= 1:
#            print("More than one PIC32 .aws file found!")
#            sys.exit(0)
#       
#        print( f'Found file: {p18File}')
#        print( f'Found file: {p32File}')
#    else:
#        print( f'PIC release directory not found: {picPath}')
#        sys.exit(9)
		
    # Process the build files
    for file in buildFiles:
        p18FileList.append( fullBuildPath + "\\" + file)
        p32FileList.append( fullBuildPath + "\\" + file)

    # Process other file list, make each entry a full path
    for file in otherFiles:
         p18FileList.append( args.rootDir + "\\" + file)
         p32FileList.append( args.rootDir + "\\" + file)

    # Add PIC18/32 file to appropriate list
    p18FileList.append( f'{picPath}\\{p18File}' )
    p32FileList.append( f'{picPath}\\{p32File}' )
	
    # Path for temporary padded Image
    #tempImagePath = fullReleasePath + "\\temp.bin"

    # Filename for Application Image
    applicationFilename = f'dw_ModelB_v{args.version}b{args.build}.bin'

    # Copy Build files to destination
    copy_release_files( p18FileList, fullP18ReleasePath, 'dw_ModelB.bin', applicationFilename, args.update )
    copy_release_files( p32FileList, fullP32ReleasePath, 'dw_ModelB.bin', applicationFilename, args.update )

#    for file in fileList:
#        compare_copy_file( file, fullP18ReleasePath, args.update)
#        compare_copy_file( file, fullP32ReleasePath, args.update)
#		# Copy application image to new file
#        # If application image file
#        if "dw_ModelB.bin" in file:
#            fixup_image_file( file, fullP18ReleasePath, applicationFilename, args.update )
#            fixup_image_file( file, fullP32ReleasePath, applicationFilename, args.update )
#            # Create a temporary padded image file
#            pad_file( file, tempImagePath, 16)
#            # Copy to file with version/build in name
#            compare_copy_file( tempImagePath, applicationImagePath, args.update)
#            # Delete temp file
#            os.remove(tempImagePath)
	
    #
    # Construct a programming arguments file
    #   PIC Factory Image is at fixed address (0x70000)
    #   Manufacturing Test Image has fixed name and fixed address (0x800000)
	#
    construct_flash_args( fullP18ReleasePath, fullBuildPath, p18File )
    construct_flash_args( fullP32ReleasePath, fullBuildPath, p32File )
#    pgmFile = fullReleasePath + "\\flash_project_args"
#    with open(pgmFile, "w") as argsFile:
#        #Copy, with edit, Bootloader args
#        bootloader = fullBuildPath + "\\flash_bootloader_args"
#        with open(bootloader, "r") as bootloaderArgs:
#            for line in bootloaderArgs:
#                #Edit bootloader line
#                if "bootloader/bootloader" in line:
#                    line = line.replace("bootloader/bootloader", "bootloader")
#                # Replace flash_size detect with 16M
#                if "flash_size" in line:
#                    line = line.replace("detect", "16MB")
#                argsFile.write(line)
#        #Copy, with edit, Project args
#        project = fullBuildPath + "\\flash_project_args"
#        with open(project, "r") as projectArgs:
#            for line in projectArgs:
#                #Edit partition-table line
#                if "bootloader/bootloader" in line:
#                    #skip line if it has bootloader (added that above)
#                    continue
#                if "partition_table/" in line:
#                    line = line.replace("partition_table/", "")
#                if line != "\n" and line != " \n":
#                    argsFile.write(line)
#                if "ota_data_initial" in line:
#                    #Add PIC Factory Image, after ota data initial
#                    argsFile.write("0x70000 " + picFile + "\n")
#        #Add Manufacturing Test Image
#        argsFile.write("0x800000 dw_MfgTest.bin\n")

    #
    # Update release.log file
    #
	
    versionBuild = f'v{args.version} b{args.build}'
	
    # Time/Date stamp the release
    from datetime import datetime
    now = datetime.now()
    dateTimeFormat = now.strftime("%m/%d/%Y %H:%M:%S")

    # Append record to the log file
    logFile = releasePath + "\\release.log"
    startOfNote = False
    logLines = []
	
	# Read in existing file
    with open(logFile, "r") as log:
        logLines = log.readlines()
	
    # Look for note(s) for existing version/build, tag as "deleted"
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
    if args.internal:
        newNote += " (internal)\n"
    else:
        newNote += "\n"
    logLines.append("\n")
    logLines.append(newNote)
    logLines.append("  P18 firmware: " + p18File + "\n")
    logLines.append("  P32 firmware: " + p32File + "\n")
	
    # if a message was specified, it can be multi-line, append each line
    if args.message:
        for line in args.message:
            logLines.append("  " + line + "\n")
    # Write out ammended log file
    with open(logFile, "w") as log:
        log.writelines(logLines)

    # Create local readme files
    create_readme( logLines, fullP18ReleasePath, args.version, args.build, dateTimeFormat, args.internal, args.zip )
    create_readme( logLines, fullP32ReleasePath, args.version, args.build, dateTimeFormat, args.internal, args.zip )
	
#    # Use 7-zip to create hash values for each file, except Readme.txt and previous ZIP file
#    systemCommand = f'"{args.zip}" h -scrcsha256 {fullReleasePath}\\* -x!Readme.txt -x!*.zip'
#    hashOutput = os.popen( systemCommand ).read()
#
#
#    # create ReadMe.txt file
#    readmeFile = fullReleasePath + "\\Readme.txt"
#    separator = "\n\n============================================================================================\n"
#    skipNote = False
#
#    with open(readmeFile, "w") as readme:
#        readme.write("Drinkworks Model-B Appliance\n")
#        readme.write("ESP32 Firmware\n")
#        readme.write("Version: " + args.version + "\n")
#        readme.write("Build: " + args.build + "\n")
#        readme.write("Date: " + dateTimeFormat )
#        readme.write(separator)
#		
#        # Copy release notes from log file
#        #   Always skip lines starting with '#'
#        #   Always skip deleted notes
#        #   skip internal notes for external release
#        for line in logLines:
#            if not line.startswith("#"):
#                if line.isspace():
#                    startOfNote = True
#                else:
#                    if startOfNote:
#                        if "(deleted)" in line:
#                            skipNote = True
#                        elif "(internal)" in line and not args.internal:
#                            skipNote = True
#                        else:
#                            skipNote = False
#                            readme.write("\n")
#                        startOfNote = False
#						
#                    # Unless skipping this note, write line to Readme file
#                    if not skipNote:
#                        readme.write(line)
#                   
#        # Add Hash values
#        readme.write(separator)
#        readme.write(hashOutput)

    # Use 7-zip to package everything update, exclude previous ZIP file (if present)
    create_release_archive( fullP18ReleasePath, 'P18', args.version, args.build, args.zip)
    create_release_archive( fullP32ReleasePath, 'P32', args.version, args.build, args.zip)
#    archiveName = fullReleasePath + "\\ModelB_ESP_" + "v" + args.version + "b" + args.build + ".zip"
#    print( archiveName )
#    systemCommand = f'"{args.zip}" a -x!*.zip {archiveName} {fullReleasePath}\\*'
#    os.system(systemCommand)
	
    print("Exiting Program")
