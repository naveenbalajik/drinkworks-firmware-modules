# PICMZ OTA Job Creation Utility

The mz_job.py utility enables the creation of ota jobs for the PICMZ.

## Summary
This script creates an OTA job from an unsigned firmware file. The following command line arguments are used by the script to create the update:
1. `aws s3api list-object-versions`
2. `aws signer start-signing-job`
3. `aws iot create-stream`
4. `aws iot create-ota-update`

For items 2-4 above, a json file is used as an input parameter the the command. The json files are saved in the current directory for future reference.

### Usage
```sh
usage: mz_job.py [-h] [--fileID FILEID] [--fileName FILENAME] [--signerRoleArn SIGNERROLEARN] [--streamJobRoleArn STREAMJOBROLEARN] jobName thingGroupTarget mzProgramS3Filepath

positional arguments:
  jobName               set the OTA job name
  thingGroupTarget      set target thing group. Example: ModelA_MZ_Firmware_Update
  mzProgramS3Filepath   Filepath of the MZ program in the afr-ota-drinkwork-firmware bucket. Example: 'PIC32MZ/GEN1_SV_v9.70.aws'

optional arguments:
  -h, --help                            show this help message and exit
  --fileID FILEID                       Set fileID. Default is 1
  --fileName FILENAME                   Set fileName. Default is 'pic_fw'
  --signerRoleArn SIGNERROLEARN         Set the role arn for the code signing. Default is 'profile_for_ESP32'
  --streamJobRoleArn STREAMJOBROLEARN   Set role arn for stream and job creation. Default is 'IoT_Update_Role'
```
#### Note
Must be signed into aws from the command line to use script. 
## Example
```
C:\espDevelopment\drinkworks-freertos\modules\utilities\mz_job>mz_job.py testJob6 OTA_Test_Group_T aws_demos_v2.bin
```
Output:
```
Current version of program on S3: EaAKTdqSZXQ5LHZCqCltQ9rYa.s9mXa1

Signing code
{
    "jobId": "b81775d9-366d-4546-bf63-b685055ef1c5"
}

Creating Stream
{
    "streamId": "testJob6_Stream",
    "streamArn": "arn:aws:iot:us-east-1:551671089203:stream/testJob6_Stream",
    "description": "This is the stream for the signed update of b81775d9-366d-4546-bf63-b685055ef1c5",
    "streamVersion": 1
}

Creating OTA Job
{
    "otaUpdateId": "testJob6",
    "otaUpdateArn": "arn:aws:iot:us-east-1:551671089203:otaupdate/testJob6",
    "otaUpdateStatus": "CREATE_PENDING"
}

Successfully create OTA job: testJob6
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

