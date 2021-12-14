#
#  The pic18_job.py utility enables the creation of ota jobs for the PIC18F and PIC32MX.
#
import argparse
import json
import subprocess

# Update this version number with subsequent releases
utilityVersion = "1.1"

awsEndpoint = '551671089203'
region = 'us-east-1'

if __name__ == "__main__":

    # Set up the arguments
    parser = argparse.ArgumentParser(description= f'Drinkworks AWS OTA Job Creation Utility for PIC processors, v{utilityVersion}')
    parser = argparse.ArgumentParser()
    parser.add_argument("jobName", help="set the OTA job name")
    parser.add_argument("thingGroupTarget", help="set target thing group. Example: MODB_PIC18_Firmware_Update")
    parser.add_argument("pic18ProgramS3Filepath", help="Filepath of the PIC18 program in the afr-ota-drinkwork-firmware bucket. Example: \'PIC18F/MODB_v0.29.aws\'")
    parser.add_argument("--fileID", help="Set fileID. Default is 1", type=int)
    parser.add_argument("--fileName", help="Set fileName. Default is \'pic_ota0\'")
    parser.add_argument("--signerRoleArn", help="Set the role arn for the code signing. Default is \'profile_for_ESP32\'")
    parser.add_argument("--streamJobRoleArn", help="Set role arn for stream and job creation. Default is \'IoT_Update_Role\'")
    parser.add_argument("--jobTimeout", help="Set job timeout in minutes. Default is \'10\' minutes", default=10, type=int )
    args = parser.parse_args()

    # Set default variables
    jobName = args.jobName
    thingGroupTarget = args.thingGroupTarget
    pic18ProgramS3Filepath = args.pic18ProgramS3Filepath
    fileID = 1
    streamName = jobName + "_Stream"
    fileName = 'pic_ota0'
    signerRoleArn = 'profile_for_ESP32'
    streamJobRoleArn = 'IoT_Update_Role'
    # Overwrite values based on optional variables
    if args.fileID:
        fileID = args.fileID
    if args.fileName:
        fileName = args.fileName
    if args.signerRoleArn:
        signerRoleArn = args.signerRoleArn
    if args.streamJobRoleArn:
        streamJobRoleArn = args.streamJobRoleArn

    # Find the current version of the specified file
    systemCommand = "aws s3api list-object-versions --bucket afr-ota-drinkworks-firmware --prefix " + pic18ProgramS3Filepath + " --query Versions[?IsLatest].[VersionId] --output text"
    try:
        systemOutput = str(subprocess.check_output(systemCommand, stderr=subprocess.STDOUT, shell=True, timeout=10),
                           'utf-8')
    except subprocess.CalledProcessError as e:
        print(e.output.decode("utf-8"))
        raise

    print("Current version of program on S3: " + systemOutput)
    version = systemOutput

    # Code sign the PIC18 program
    codeSignJSON = {}
    codeSignJSON['source'] = {
        "s3": {
            "bucketName": "afr-ota-drinkworks-firmware",
            "key": pic18ProgramS3Filepath,
            "version": version
        }
    }
    codeSignJSON['destination'] = {
        "s3": {
            "bucketName": "afr-ota-drinkworks-firmware",
            "prefix": "PIC18_SignedImages/"
        }
    }
    codeSignJSON['profileName'] = signerRoleArn

    # Store the create JSON stream document
    codeSignJSONfilename = jobName + '_codeSign.json'
    with open(codeSignJSONfilename, 'w') as outfile:
        json.dump(codeSignJSON, outfile, indent=2)

    # send the request to AWS
    print("Signing code")
    systemCommand = "aws signer start-signing-job --cli-input-json file://" + codeSignJSONfilename
    try:
        systemOutput = str(subprocess.check_output(systemCommand, stderr=subprocess.STDOUT, shell=True, timeout=10), 'utf-8')
    except subprocess.CalledProcessError as e:
        print(e.output.decode("utf-8"))
        raise

    print(systemOutput)

    # Save output of signed job
    try:
        signedFirmware = json.loads(systemOutput)['jobId']
    except:
        print("Error: jobID not found in return from start-signing-job")
        raise



    # Create JSON document for stream
    streamJSON = {}
    streamJSON['streamId'] = streamName
    streamJSON['description'] = "This is the stream for the signed update of " + signedFirmware
    streamJSON['files'] = []
    streamJSON['files'].append({
        "fileId": fileID,
        "s3Location": {"bucket": "afr-ota-drinkworks-firmware", "key": "PIC18_SignedImages/" + signedFirmware}
    })
    streamJSON['roleArn'] = "arn:aws:iam::" + awsEndpoint + ":role/" + streamJobRoleArn

    # Store the create JSON stream document
    streamJSONfilename = streamName+'.json'
    with open(streamJSONfilename, 'w') as outfile:
        json.dump(streamJSON, outfile, indent=2)

    # send the request to AWS
    print("Creating Stream")
    systemCommand = "aws iot create-stream --cli-input-json file://" + streamJSONfilename
    try:
        systemOutput = str(subprocess.check_output(systemCommand, stderr=subprocess.STDOUT, shell=True, timeout=10), 'utf-8')
    except subprocess.CalledProcessError as e:
        print(e.output.decode("utf-8"))
        raise

    print(systemOutput)

    # Create JSON document for job file
    otaJobJSON = {}
    otaJobJSON['otaUpdateId'] = jobName
    otaJobJSON['description'] = "PIC18 OTA Update Job for " + pic18ProgramS3Filepath
    otaJobJSON['targets'] = []
    otaJobJSON['targets'].append("arn:aws:iot:" + region + ":" + awsEndpoint + ":thinggroup/" + thingGroupTarget)
    otaJobJSON['protocols'] = []
    otaJobJSON['protocols'].append("MQTT")
    otaJobJSON['targetSelection'] = "CONTINUOUS"
    #otaJobJSON['awsJobExecutionsRolloutConfig'] = {"maximumPerMinute": 1000}
    otaJobJSON['awsJobPresignedUrlConfig'] = {}
    #otaJobJSON['awsJobAbortConfig']
    otaJobJSON['awsJobTimeoutConfig'] = {
        "inProgressTimeoutInMinutes": args.jobTimeout
    }
    otaJobJSON['files'] = []
    otaJobJSON['files'].append({
        'fileName':fileName,
        'fileLocation': {
            "stream": {
                "streamId": streamName,
                "fileId": fileID
            }
        },
        'codeSigning':{
            "awsSignerJobId": signedFirmware
        }
    })
    otaJobJSON['roleArn'] = "arn:aws:iam::" + awsEndpoint + ":role/" + streamJobRoleArn

    # Store the create JSON stream document
    otJobJSONfilename = jobName+'.json'
    with open(otJobJSONfilename, 'w') as outfile:
        json.dump(otaJobJSON, outfile, indent=2)

    # send the request to AWS
    print("Creating OTA Job")
    systemCommand = "aws iot create-ota-update --cli-input-json file://" + otJobJSONfilename
    try:
        systemOutput = str(subprocess.check_output(systemCommand, stderr=subprocess.STDOUT, shell=True, timeout=10), 'utf-8')
    except subprocess.CalledProcessError as e:
        print(e.output.decode("utf-8"))
        raise

    print(systemOutput)

    print("Successfully create OTA job: " + json.loads(systemOutput)['otaUpdateId'])
