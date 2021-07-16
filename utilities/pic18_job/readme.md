# PIC18 OTA Job Creation Utility

The pic18_job.py utility enables the creation of ota jobs for the PIC18F.

## Summary
This script creates an OTA job from an unsigned firmware file. The following command line arguments are used by the script to create the update:
1. `aws s3api list-object-versions`
2. `aws signer start-signing-job`
3. `aws iot create-stream`
4. `aws iot create-ota-update`

For items 2-4 above, a json file is used as an input parameter the the command. The json files are saved in the current directory for future reference.

### Usage
```
usage: pic18_job.py [-h] [--fileID FILEID] [--fileName FILENAME] [--signerRoleArn SIGNERROLEARN] [--streamJobRoleArn STREAMJOBROLEARN] jobName thingGroupTarget pic18ProgramS3Filepath

positional arguments:
  jobName               	set the OTA job name
  thingGroupTarget      	set target thing group. Example: MODB_PIC18_Firmware_Update
  pic18ProgramS3Filepath	Filepath of the PIC18 program in the afr-ota-drinkwork-firmware bucket. Example: 'PIC18F/MODB_v0.29.aws'

optional arguments:
  -h, --help            				show this help message and exit
  --fileID FILEID       				Set fileID. Default is 1
  --fileName FILENAME   				Set fileName. Default is 'pic_fw'
  --signerRoleArn SIGNERROLEARN         Set the role arn for the code signing. Default is 'profile_for_ESP32'
  --streamJobRoleArn STREAMJOBROLEARN   Set role arn for stream and job creation. Default is 'IoT_Update_Role'
```
#### Note
Must be signed into aws from the command line to use script. 
## Example
```
C:\Users\ian.whitehead\GitHub\dw_ModelB\modules\utilities\pic18_job>pic18_job.py ModelB_PIC18F_v0_29_Test1 ModelB_PIC_Firmware_Update_Test PIC18F/MODB_v0.29.aws
```
Output:
```
Current version of program on S3: cuZba.a_f.ytHYzX9GvJSnT67bWZEPy6

Signing code
{
    "jobId": "b572bdf0-c7e6-4856-ab32-88ac9d158c8d"
}

Creating Stream
{
    "streamId": "ModelB_PIC18F_v0_29_Test1_Stream",
    "streamArn": "arn:aws:iot:us-east-1:551671089203:stream/ModelB_PIC18F_v0_29_Test1_Stream",
    "description": "This is the stream for the signed update of b572bdf0-c7e6-4856-ab32-88ac9d158c8d",
    "streamVersion": 1
}

Creating OTA Job
{
    "otaUpdateId": "ModelB_PIC18F_v0_29_Test1",
    "otaUpdateArn": "arn:aws:iot:us-east-1:551671089203:otaupdate/ModelB_PIC18F_v0_29_Test1",
    "otaUpdateStatus": "CREATE_PENDING"
}

Successfully create OTA job: ModelB_PIC18F_v0_29_Test1
```
## Output
The script will save the json files used for the following commands
1. `aws signer start-signing-job`
2. `aws iot create-stream`
3. `aws iot create-ota-update`

The following items will be created on aws:
1. Signed firmware file in S3
2. File stream in IoT
3. OTA update job in IoT

The OTA update job should be viewable in the AWS console

#### Note
NOTE:  The target group should be created before running the script to create the job.
