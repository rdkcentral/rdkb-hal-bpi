/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2021 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/**********************************************************************
   Copyright [2017] [Technicolor, Inc.]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include "cJSON.h"
#include <unistd.h>
#define CMD_BUF 512
#define PATH_LEN 128
#define MAX_RETRIES 5
#define RETRY_DELAY 2
#define MAX_LINE 512
#define RETURN_OK 0
#define RETURN_ERR -1
//#include "fwupgrade_hal.h"
#define HTTP_DWNLD_CONFIG_FILE      "/tmp/httpDwnld.conf"
#define HTTP_DWNLD_IF_FILE          "/tmp/httpDwnldIf.conf"
typedef int INT;
typedef unsigned long ULONG;

#define STAGING_PARTITION "/dev/mmcblk0p14"
typedef int INT;
typedef unsigned long ULONG;
#define MOUNT_POINT "/opt/root_new"
char g_server_ip[64] = {0};
int g_xconf_upgrade_flag = 0;
int port_num = 1;
int g_xconf_flag = 0;
#define MOUNT_POINT_1 "/mnt/bootpart"
#define MOUNT_POINT_2 "/mnt/passiveroot"
#define REBOOT_REASON_SW_UPGRADE    "Software_upgrade"
#define MAX_BLOCK_SIZE 4096
char g_root_partition[PATH_LEN] = {0};
char g_downloaded_file_name[128] = {0};	
char extractedVersion[256] = {0};
char passiveVersion[256] = {0};
int protocol = 0;
static int gDwdInProgressFlag = 0; /* flag to set dload inprogress */
typedef enum {
    PROTOCOL_UNKNOWN = 0,
    PROTOCOL_HTTP,
    PROTOCOL_HTTPS,
    PROTOCOL_TFTP
} protocol_t;
protocol_t g_protocol = PROTOCOL_UNKNOWN;
#define TMP_JSON_FILE "/tmp/xconf_response.json"
#define FILENAME_MAX_LENGTH 128
#define URL_MAX_LENGTH 128
char g_firmwareFilename[256];
char g_firmwareLocation[512];
char g_firmwareVersion[128];
char g_firmwareProtocol[64];

int run_command(const char *cmd, char *output, unsigned int size) {
        char buf[256];
        FILE *fp = popen(cmd, "r");
        int status, n=0;

        if (!fp) {
                perror("popen");
                fprintf(stderr, "'%s' command failed\n", cmd);
                return -1;
        }

        while (fgets(buf, sizeof(buf), fp)) {
                fprintf(stderr, "[CMD OUTPUT] %s", buf);
                if (output && size > 0) {
                       strncpy(output + n, buf, strlen(buf));
                       n += strlen(buf);
                }
        }


        if((status = pclose(fp))==-1) {
                perror("pclose failed");
                return -1;
        }

        return WEXITSTATUS(status);
}

//static INT fwupgrade_hal_util_get_syscmd_output( char *pCmd, char *pOutput, int iOutputSize );

/* * fwupgrade_hal_util_get_syscmd_output() */
static INT fwupgrade_hal_util_get_syscmd_output( char *pCmd, char *pOutput, int iOutputSize )
{
	FILE   *FilePtr            = NULL;
	char   bufContent[ 256 ]  = { 0 };

	if ( ( NULL == pCmd ) || ( NULL == pOutput ) || ( 0 == iOutputSize ) )
	{
		return RETURN_ERR;
	}

	FilePtr = popen( pCmd, "r" );

	if ( FilePtr )
	if ( FilePtr )
	{
		char *pos;

		fgets( bufContent, 256, FilePtr );
		fclose( FilePtr );
		FilePtr = NULL;

		// Remove line \n charecter from string
		if ( ( pos = strchr( bufContent, '\n' ) ) != NULL )
			*pos = '\0';

		snprintf( pOutput, iOutputSize, "%s", bufContent );
	}

	return RETURN_ERR;
}


/*
    download the image from httpserver and store in /tmp
 */
static INT download_image_from_server(char *httpUrl, char* fileName)
{
	// TBD should have been dynamically allocated
	//
        char cmd[1400] = {0};
        char res[16] = {0};
        FILE* fp = NULL;
        INT ret = 0;
        char output[512] = {0};
        static int partition_created = 0;

	const char *partition_path = STAGING_PARTITION;
        struct stat st;

        // Check if partition exists
        if (access(partition_path, F_OK) == 0) {
                fprintf(stderr,"Partition %s already exists. Skipping creation.\n", partition_path);
        } else {
		ret = run_command("echo -e \"n\n\n\n+3G\nw\" | fdisk /dev/mmcblk0", NULL, 0);
        	if (ret != 0) {
                	fprintf(stderr, "Failed to create new partition\n");
                	return -1;
        	}
	
	snprintf(cmd, sizeof(cmd), "sgdisk -c 14:\"staging\" /dev/mmcblk0");
	run_command(cmd, NULL, 0);
	snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F -L staging %s", partition_path);
        ret = run_command(cmd, NULL, 0);
        if (ret != 0) {
                fprintf(stderr, "mkfs failed for %s\n", partition_path);
                return RETURN_ERR;
	}
	}
        // Step 6: Mount it to /mnt/bootpart
	if (access(MOUNT_POINT_1, F_OK) != 0) {
		if (mkdir(MOUNT_POINT_1, 0755) != 0) {
                	perror("mkdir failed");
                        return EXIT_FAILURE;
               }
        }
        snprintf(cmd, sizeof(cmd), "mount %s /mnt/bootpart", partition_path);
        memset(output, 0, sizeof(output));
        ret = run_command(cmd, output, sizeof(output));

	if (ret != 0) {
        fprintf(stderr, "Mount failed: %s\n", output);
        return -1;
	}
	strncpy(g_downloaded_file_name, fileName, sizeof(g_downloaded_file_name) - 1);
        g_downloaded_file_name[sizeof(g_downloaded_file_name) - 1] = '\0';

	if (g_protocol == PROTOCOL_HTTP || g_protocol == PROTOCOL_HTTPS) {
    		fprintf(stderr, "%s: Using CURL: curl -fgLo /mnt/bootpart/%s %s\n", __func__, fileName, httpUrl);
    		snprintf(cmd, sizeof(cmd),
                "rm -f dload_status && "
                "(curl -fgLo /mnt/bootpart/%s %s); "
                "echo $? > /mnt/bootpart/dload_status",
                fileName, httpUrl);
	}
 	else if (g_protocol == PROTOCOL_TFTP) {
    		fprintf(stderr, "%s: Using TFTP: tftp -g -r %s %s\n", __func__, fileName, g_server_ip);
    		snprintf(cmd, sizeof(cmd),
                "cd /mnt/bootpart && "
                "rm -f dload_status && "
                "(/usr/bin/tftp -g -r %s %s); "
                "echo $? > dload_status",
                fileName, g_server_ip);
	}
	else
	{
		fprintf(stderr, "%s: Unknown protocol in URL: %s\n", __func__, httpUrl);
    		return RETURN_ERR;
	}
        run_command(cmd, NULL, 0);

	fprintf(stderr," the value of flag in /tmp is ",g_xconf_flag);
	g_xconf_flag = get_xconf_flag();
	if (g_xconf_flag == 1) {

	fprintf(stderr," the value of flag in /tmp is ",g_xconf_flag);
        char md5_file[256] = "/tmp/local_md5.txt";
        snprintf(cmd, sizeof(cmd),
                "cd /mnt/bootpart &&"
		"md5sum %s > %s",
                fileName, md5_file);
        memset(output, 0, sizeof(output));
        ret= run_command(cmd, output, sizeof(output));
        if (ret !=0){
	        fprintf(stderr,"md5sum failed for downloaded image");
        }

        snprintf(cmd, sizeof(cmd),
		"(diff -q %s /tmp/%s.txt > /dev/null; ret=$?; echo $ret > /mnt/bootpart/dload_status; exit $ret)", md5_file, fileName);
        memset(output, 0, sizeof(output));
        ret = run_command(cmd, output, sizeof(output));

        if (ret != 0) {
                fprintf(stderr, "MD5 mismatch! See %s vs /tmp/%s\n", md5_file, g_firmwareVersion);
                return -1; 
        }
	}
        fprintf(stderr, "MD5 verified successfully.\n");
        fprintf(stderr, "Download successful. File: %s and %d\n", g_downloaded_file_name, g_xconf_flag);
	fp = fopen("/mnt/bootpart/dload_status", "r");
        if (NULL == fp) {
                printf("dload_status : file open error!");
                return RETURN_ERR;
        }
        fgets(res, sizeof(res) - 1, fp);
        fclose(fp);
        ret = atoi(res);
        if (0 != ret) {
                printf("Download from remote server failed!\n");
                return RETURN_ERR;
        }
	return 0;
}

int get_xconf_flag() {
    int val = 0;
    FILE *fp = fopen("/tmp/xconf_flag", "r");
    if (fp) {
        fscanf(fp, "%d", &val);
        fclose(fp);
    }
    fprintf(stderr, "value of xconf from /tmp = %d\n",val);
    return val;
}

/* FW Download HAL API Prototype */
/* fwupgrade_hal_set_download_url  - 1 */
/* Description: Set Download Settings
Parameters : char* pUrl;
Parameters : char* pfilename;

@return the status of the operation
@retval RETURN_OK if successful.
@retval RETURN_ERR if any Downloading is in process or Url string is invalided.
*/
INT fwupgrade_hal_set_download_url (char* pUrl, char* pfilename)
{
	fprintf(stderr,"Entering %s \n",__FUNCTION__);
//	if ((pUrl == NULL) || (pfilename==NULL))
	if ((pUrl == NULL) || (pfilename==NULL))
	{
		fprintf(stderr,"%s: NULL Url or filename has been passed as the input -... \n", __func__);
		return RETURN_ERR;
	}
	else if ( (strlen(pfilename) >= FILENAME_MAX_LENGTH) || (strlen(pUrl) >= URL_MAX_LENGTH) )
	{
                fprintf(stderr,"%s: Buffer length was exceeding for HTTP URL - %d or filename - %d... \n", __func__, strlen(pUrl), strlen(pfilename));
		return RETURN_ERR;
             
        }
	else
	{
		FILE* fp;
		char httpUrl[1024] = {0};
		char fileName[256] = {0};
		int ret_status = 0;

		/* To Get the previous URL if any and compare with new one */
		ret_status = fwupgrade_hal_get_download_url(httpUrl, fileName);

		if(ret_status == RETURN_OK)
		{
			if ((strcmp(httpUrl, pUrl) == 0) && (strcmp(fileName, pfilename) == 0))
			{
				fprintf(stderr,"HTTP URL and file name is same as previous! \n");
			}
			else
			{
				fprintf(stderr,"HTTP URL or filename Changed! \n");
				run_command("rm /mnt/bootpart/dload_status", NULL, 0);
			}
		}
		else
		{
			run_command("rm /mnt/bootpart/dload_status", NULL, 0);
		}
		fp = fopen(HTTP_DWNLD_CONFIG_FILE, "w");
		if(fp == NULL)
		{
			return RETURN_ERR;
		}
		fprintf(fp, "%s\n%s\n", pUrl, pfilename);
		fclose(fp);
		fprintf(stderr,"%s Stored HTTP download URL and filename to %s file\n", __func__, HTTP_DWNLD_CONFIG_FILE);

		return RETURN_OK;
	}
}


/* fwupgrade_hal_get_download_Url: */
/* Description: Get FW Download Url
Parameters : char* pUrl
Parameters : char* pfilename;
@return the status of the operation.
@retval RETURN_OK if successful.
@retval RETURN_ERR if http url string is empty.
*/
INT fwupgrade_hal_get_download_url (char *pUrl, char* pfilename)
{
//if ((pUrl == NULL) || (pfilename==NULL))
	if ((pUrl == NULL) || (pfilename==NULL))
	{
                fprintf(stderr,"%s: NULL Url or filename has been passed as the input -... \n", __func__);
                return RETURN_ERR;
        }
        else if ( (strlen(pfilename) >= FILENAME_MAX_LENGTH) || (strlen(pUrl) >= URL_MAX_LENGTH) )
        {
                fprintf(stderr,"%s: Buffer length was exceeding for HTTP URL - %d or filename - %d... \n", __func__, strlen(pUrl), strlen(pfilename));
                return RETURN_ERR;

        }		
	else
	{
		FILE* fp;
		char* fc;

		fprintf(stderr,"%s: Checking Buffer length for HTTP URL - %d or filename - %d... \n", __func__, strlen(pUrl), strlen(pfilename));
		fprintf(stderr,"Entering %s\n", __func__);

		fp = fopen(HTTP_DWNLD_CONFIG_FILE, "r");
		if(fp == NULL)
		{
			return RETURN_ERR;
		}
		fc = pUrl;
		while((*fc = (char)fgetc(fp)) != '\n')
		{
			++fc;
		}
		*fc = '\0';
		fc = pfilename;
		while((*fc = (char)fgetc(fp)) != '\n')
		{
			++fc;
		}
		*fc = '\0';
		fprintf(stderr,"%s pfilename: %s\n", __func__, pfilename);
		fclose(fp);

		return RETURN_OK;
	}
}

/* interface=0 for wan0, interface=1 for erouter0 */
INT fwupgrade_hal_set_download_interface(unsigned int interface)
{
	fprintf(stderr,"Entering %s\n", __func__);
	FILE *fp = NULL;	
	if( interface > 1 )
	{
		return RETURN_ERR;
	}
	// Save the interface numerical value to the config file
	fp = fopen(HTTP_DWNLD_IF_FILE, "w");
	if(fp == NULL)
	{
		return RETURN_ERR;
	}
	fprintf(fp, "%d\n", interface);
	fclose(fp);
	return RETURN_OK;
}


/* interface=0 for wan0, interface=1 for erouter0 */
INT fwupgrade_hal_get_download_interface(unsigned int* pinterface)
{
	if (pinterface == NULL)
	{
		return RETURN_ERR;
	}
	else
	{
		FILE *fp = NULL;
		char ifNum;

		fp = fopen(HTTP_DWNLD_IF_FILE, "r");
		if(fp == NULL)
		{
			return RETURN_ERR;
		}
		ifNum = fgetc(fp);
		*pinterface = atoi(&ifNum);
		fprintf(stderr,"%s Download interface numerical value: %d\n", __func__, *pinterface);
		fclose(fp);
		return RETURN_OK;
	}
}


/* fwupgrade_hal_download */
/**
Description: Start FW Download
Parameters: <None>
@return the status of the operation.
@retval RETURN_OK if successful.
@retval RETURN_ERR if any Downloading is in process.

*/
INT fwupgrade_hal_download ()
{
    fprintf(stderr,"Entering %s\n", __func__);
    struct hostent *host;
    struct in_addr **addr_list;
    char hostname[1024] = {0};
    char fullhostname[256] = {0};
    char *pstr;
    int i = 0;
    char dlHttpUrl[1024] = {0};
    char dlFilename[256] = {0};
    unsigned char ipAddrInt[4] = {0};
    char cmd[512] = {0};
    g_protocol = PROTOCOL_UNKNOWN;
    if( fwupgrade_hal_get_download_url(dlHttpUrl, dlFilename) != RETURN_OK)
    {
        return RETURN_ERR;
    }
    if( strstr(dlHttpUrl, "http://") == NULL && strstr(dlHttpUrl, "https://") == NULL && strstr(dlHttpUrl, "www.") == NULL  && strstr(dlHttpUrl, "tftp://") == NULL)

    {
        return 400;
    }

    if((pstr = strstr(dlHttpUrl, "http://")))
    {
	g_protocol = PROTOCOL_HTTP;
        pstr += strlen("http://");
        strcpy(fullhostname, "http://");
    }
    else if((pstr = strstr(dlHttpUrl, "https://")))
    {
	g_protocol = PROTOCOL_HTTPS;
        pstr += strlen("https://");
        strcpy(fullhostname, "https://");
    }
    else if((pstr = strstr(dlHttpUrl, "www.")))
    {
        pstr += strlen("www.");
        strcpy(fullhostname, "www.");
    }
    else if((pstr = strstr(dlHttpUrl, "tftp://")))
    {
	    g_protocol = PROTOCOL_TFTP;
        pstr += strlen("tftp://");
        strcpy(fullhostname, "tftp://");
    }

    while( *pstr != '/' && *pstr != '\0' && *pstr != ':' )
    {
        hostname[i++] = *pstr;
        ++pstr;
    }
    hostname[i] = '\0';
    strcat(fullhostname, hostname);

    if ((host = gethostbyname(dlHttpUrl)) == NULL)
    {
        if ((host = gethostbyname(fullhostname)) == NULL)
        {
            if ((host = gethostbyname(hostname)) == NULL)
            {
                fprintf(stderr,"Failed on gethostbyname() call. hostname: %s\n", hostname);
                return 400;
            }
        }
    }

    fprintf(stderr,"host->h_addrtype = %d, %s\n", host->h_addrtype,
            host->h_addrtype == AF_INET ? "AF_INET" : host->h_addrtype == AF_INET6 ? "AF_INET6" : "Unknown");
    addr_list = (struct in_addr **) host->h_addr_list;
    for(i = 0; addr_list[i] != NULL; i++)
    {
        printf("addr_list[%d] = %s\n", i, inet_ntoa(*addr_list[i]));
    }

    // Convert the dot-text format IP address to an array of numbers, a strange format used by the s/w download module
    memset(hostname, 0, sizeof(hostname));
    snprintf(hostname, sizeof(hostname), "%s", inet_ntoa(*(struct in_addr*)host->h_addr_list[0]));
    snprintf(g_server_ip, sizeof(g_server_ip), "%s", hostname);
    pstr = hostname;
    for(i=0; i<4; i++)
    {
        ipAddrInt[i] = *pstr - '0';
        ++pstr;
        while(*pstr != '.' && *pstr != '\0')
        {
            ipAddrInt[i] *= 10;
            ipAddrInt[i] += *pstr - '0';
            ++pstr;
        }
        ++pstr;
    }
     fprintf(stderr,"Host IP address: %d.%d.%d.%d\n", ipAddrInt[0], ipAddrInt[1], ipAddrInt[2], ipAddrInt[3]);

    // Download the image to tmp
    if(RETURN_OK != download_image_from_server(dlHttpUrl, dlFilename))
    {
        fprintf(stderr,"failed download the image to CPE_1\n");
        return RETURN_ERR;
    }
    return RETURN_OK;
}


/* fwupgrade_hal_get_download_status */
/**
Description: Get the FW Download Status
Parameters : <None>
@return the status of the HTTP Download.
?   0 ? Download is not started.
?   Number between 0 to 100: Values of percent of download.
?   200 ? Download is completed and waiting for reboot.
?   400 -  Invalided Http server Url
?   401 -  Cannot connect to Http server
?   402 -  File is not found on Http server
?   403 -  HW_Type_DL_Protection Failure
?   404 -  HW Mask DL Protection Failure
?   405 -  DL Rev Protection Failure
?   406 -  DL Header Protection Failure
?   407 -  DL CVC Failure
?   500 -  General Download Failure
?   */
INT fwupgrade_hal_get_download_status()
{
	fprintf(stderr,"Entering %s\n", __func__);
	FILE* DL_StatusFile = NULL;
	char str[16] = {0};
	int dl_stat = 0;
	DL_StatusFile = fopen("/mnt/bootpart/dload_status", "r");
	if(NULL != DL_StatusFile)
	{
		fgets(str, sizeof(str)-1, DL_StatusFile);
		fclose(DL_StatusFile);
		dl_stat = atoi(str);
		if(0 != dl_stat)
		{
			fprintf(stderr,"download from remote server failed!\n");
			return 500;
		}
		return 200;
	}
	if ( gDwdInProgressFlag == 1 )
	{
		return 100;
	}
	return 0;
}

/* fwupgrade_hal_reboot_ready */
/*
Description: Get the Reboot Ready Status
Parameters:
ULONG *pValue- Values of 1 for Ready, 2 for Not Ready
@return the status of the operation.
@retval RETURN_OK if successful.
@retval RETURN_ERR if any error is detected

*/
INT fwupgrade_hal_reboot_ready(ULONG *pValue)
{
    fprintf(stderr,"Entering %s\n", __func__);

    if (pValue == NULL)
    {
        return RETURN_ERR;
    }
    *pValue = 1;
    return RETURN_OK;
}

/* fwupgrade_hal_reboot_now */
/*
Description:  Http Download Reboot Now
Parameters : <None>
@return the status of the reboot operation.
@retval RETURN_OK if successful.
@retval RETURN_ERR if any reboot is in process.
*/
INT fwupgrade_hal_download_reboot_now()
{
	fprintf(stderr,"Entering %s\n", __func__);

	char wic_path[256];
        char target_boot[PATH_LEN], target_root[PATH_LEN];
        int update_fstab = 0;
        int bs_512 = 512;
        char decompress_cmd[300];
        char cmd[256];
        char output[512];
        int attempt = 0;
        char fstab_path[512];
        int ret;
        const char *OLD_STR = "/dev/mmcblk0p3";
        FILE *fp_in, *fp_out;
        char line[MAX_LINE], *pos;
        snprintf(wic_path, sizeof(wic_path), "/mnt/bootpart/%s", g_downloaded_file_name);
        // Step 1: Check if file exists
        if (access(wic_path, F_OK) != 0) {
               fprintf(stderr,"File %s does not exist.\n", wic_path);
                return RETURN_ERR;
        }
        fprintf(stderr,"File %s exists.\n", wic_path);

        // Step 2: Decompress
        snprintf(decompress_cmd, sizeof(decompress_cmd), "bzip2 -d %s", wic_path);
        ret = run_command(decompress_cmd, output, sizeof(output));
        if (ret != 0) {
                fprintf(stderr,"Decompression failed for: %s\n", wic_path);
		cleanup_mount(MOUNT_POINT_1);
                return RETURN_ERR;
        }
	// Step 3: Get decompressed path
        char decompressed_path[256];
        strncpy(decompressed_path, wic_path, sizeof(decompressed_path));
        decompressed_path[strlen(decompressed_path) - 4] = '\0';  // Strip ".bz2"

        // Proceed with flashing decompressed_path
        fprintf(stderr,"Decompressed image at: %s\n", decompressed_path);
	if (detect_root_partition() != RETURN_OK) {
        fprintf(stderr, "Could not detect root partition!\n");
	cleanup_mount(MOUNT_POINT_1);
        return RETURN_ERR;
        }


        // Step 3: Determine target partitions

	if (strcmp(g_root_partition, "/dev/mmcblk0p4") == 0) {
		fprintf(stderr, "Currently booted from ROOT-A, switching to ROOT-B\n");
                strcpy(target_boot, "/dev/mmcblk0p7");
                strcpy(target_root, "/dev/mmcblk0p8");
                update_fstab = 1;
        } else if (strcmp(g_root_partition, "/dev/mmcblk0p8") == 0) {
                fprintf(stderr, "Currently booted from ROOT-B, switching to ROOT-A\n");
                strcpy(target_boot, "/dev/mmcblk0p3");
                strcpy(target_root, "/dev/mmcblk0p4");

        } else {
                fprintf(stderr, "Unsupported root partition_1: %s\n", g_root_partition);
                return RETURN_ERR;
                cleanup_mount(MOUNT_POINT_1);
        }

	copy_blocks(decompressed_path, target_boot, 17408, 32768, bs_512);
	copy_blocks(decompressed_path, target_root, 50176, 2097152, bs_512);
	//copy_blocks(decompressed_path, target_root, 50176, 786432, bs_512);
	sync();
	fprintf(stderr, "boot and root are ready\n");
        if (update_fstab) {
		if (access(MOUNT_POINT, F_OK) != 0) {
    			if (mkdir(MOUNT_POINT, 0755) != 0) {
        			perror("mkdir failed");
				cleanup_mount(MOUNT_POINT_1);
        			return EXIT_FAILURE;
    			}
		}

		snprintf(fstab_path, sizeof(fstab_path), "%s/etc/fstab", MOUNT_POINT);

        	while (attempt < MAX_RETRIES) {
                	snprintf(cmd, sizeof(cmd),
                        	"mount -t ext4 -o relatime,sync %s %s 2>&1",
                        	target_root, MOUNT_POINT);
			memset(output, 0, sizeof(output));
                	ret = run_command(cmd, output, sizeof(output));

                	if (ret == 0) {
                        	if (access(fstab_path, F_OK) == 0) {
                                	fprintf(stderr,
                                        	"Mount successful and fstab found at %s\n",
                                        	fstab_path);
                                	break;
				} else {
					fprintf(stderr,"target fstab not found. Retrying...\n");
				}
                        } else {
				fprintf(stderr, "Mount failed with exit code %d (attempt %d/%d)\n",
                        	ret, attempt + 1, MAX_RETRIES);
			}

                	attempt++;
                	sleep(RETRY_DELAY);
        	}
		if (attempt == MAX_RETRIES) {
			fprintf(stderr, "Mount failed after %d attempts\n", MAX_RETRIES);
			cleanup_mount(MOUNT_POINT_1);
			return RETURN_ERR;
		}

        	fp_in = fopen(fstab_path, "r");
        	if (!fp_in) {
                	perror("Failed to open fstab for reading");
                	return -1;
        	}

        	fp_out = fopen("/opt/root_new/etc/fstab.tmp", "w");
        	if (!fp_out) {
                	perror("Failed to open temp file for writing");
                	fclose(fp_in);
                	return -1;
        	}

        	while (fgets(line, sizeof(line), fp_in)) {
                	if ((pos = strstr(line, OLD_STR))) {
                        	*(pos + 13) = '7';
                	}
                	fputs(line, fp_out);
        	}

        	fclose(fp_in);
        	fclose(fp_out);

        	if (rename("/opt/root_new/etc/fstab.tmp", fstab_path) != 0) {
                	perror("Failed to overwrite fstab");
                	return -1;
        	}

        	fprintf(stderr,"fstab updated successfully.\n");
                umount("/opt/root_new");
                rmdir("/opt/root_new");
        	} else {
                	fprintf(stderr, "No fstab update required for mmcblk0p4 boot.\n");
        	}

        copy_blocks("/dev/mmcblk0p1", "/mnt/bootpart/bl2.img", 0, 8158, bs_512);
        copy_blocks("/dev/mmcblk0p5", "/mnt/bootpart/bl2_b.img", 0, 8158, bs_512);
        copy_blocks("/dev/mmcblk0p2", "/mnt/bootpart/fip.img", 0, 4096, bs_512);
        copy_blocks("/dev/mmcblk0p6", "/mnt/bootpart/fip_b.img", 0, 4096, bs_512);
	sync();
        fprintf(stderr, "bl2 and fip 2 are ready\n");
        fprintf(stderr, "target boot and root are written\n");

        write_image_to_device("/mnt/bootpart/bl2_b.img", "/dev/mmcblk0p1", bs_512);
        write_image_to_device("/mnt/bootpart/bl2.img", "/dev/mmcblk0p5", bs_512);
        write_image_to_device("/mnt/bootpart/fip_b.img", "/dev/mmcblk0p2", bs_512);
        write_image_to_device("/mnt/bootpart/fip.img", "/dev/mmcblk0p6", bs_512);
	sync();
	fprintf(stderr,"value of the xconf flag is %d\n", g_xconf_flag);
	cleanup_mount(MOUNT_POINT_1);
	umount(MOUNT_POINT_1);
	sync();
	sleep(3);
	fprintf(stderr,"All done. Rebooting...\n");
        run_command("/sbin/reboot", NULL, 0);

    	return RETURN_OK;
}

INT fwupgrade_hal_get_data_from_Xconf() {
    char mac[32] = {0};
    char output[512];
    char cloud_url[512];
    char full_url[512];
    char cmd[512];
    int ret;
	char g_virtualIfName[32] = "erouter0";
    FILE *fp = fopen("/nvram/wan_name.txt", "r");
    

       if (fp)
       {
        if (fgets(g_virtualIfName, sizeof(g_virtualIfName), fp))
        {
           g_virtualIfName[strcspn(g_virtualIfName, "\n")] = '\0';

          if (g_virtualIfName[0] == '\0')
               strcpy(g_virtualIfName, "erouter0");
        }
            fclose(fp);
       }
	
    chat MAC_CMD[128]={0};
	snprintf(MAC_CMD, sizeof(path), "/sbin/ifconfig %s | grep HWaddr | cut -c39-55", g_virtualIfName);
    FILE *fp = popen(MAC_CMD, "r");
    if (!fp) {
        fprintf(stderr, "Failed to execute MAC command\n");
        return -1;
    }

    if (!fgets(mac, sizeof(mac), fp)) {
        pclose(fp);
        fprintf(stderr, "Failed to read MAC address\n");
        return -1;
    }
    pclose(fp);
    mac[strcspn(mac, "\n")] = 0;  // Remove trailing newline

    if (get_cloud_url(cloud_url, sizeof(cloud_url)) != 0) {
        return -1;    // Failed to read CLOUDURL
    }
    snprintf(full_url, sizeof(full_url), "%s%s", cloud_url, mac);
    snprintf(cmd, sizeof(cmd),
           "curl -s %s -o %s",
           full_url, TMP_JSON_FILE);
    ret = run_command(cmd, output, sizeof(output));
    if (ret != 0) {
        fprintf(stderr, "Curl command failed: %s\n", output);
	return -1;
    }

    // Step 3: Parse response JSON
    fp = fopen(TMP_JSON_FILE, "r");
    if (!fp) {
        perror("fopen response");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *data = malloc(size + 1);
    if (!data) {
        fclose(fp);
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }

    fread(data, 1, size, fp);
    data[size] = '\0';
    fclose(fp);

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        fprintf(stderr, "JSON parse error\n");
        return -1;
    }

    cJSON *fwFile = cJSON_GetObjectItemCaseSensitive(json, "firmwareFilename");
    cJSON *fwLoc  = cJSON_GetObjectItemCaseSensitive(json, "firmwareLocation");
    cJSON *fwVer  = cJSON_GetObjectItemCaseSensitive(json, "firmwareVersion");
    cJSON *fwProt = cJSON_GetObjectItemCaseSensitive(json, "firmwareDownloadProtocol");

    if (fwFile && fwLoc && fwVer && fwProt) {
        strncpy(g_firmwareFilename, fwFile->valuestring, sizeof(g_firmwareFilename));
        strncpy(g_firmwareLocation, fwLoc->valuestring, sizeof(g_firmwareLocation));
        strncpy(g_firmwareVersion, fwVer->valuestring, sizeof(g_firmwareVersion));
        strncpy(g_firmwareProtocol, fwProt->valuestring, sizeof(g_firmwareProtocol));

        fprintf(stderr,"cloudFWFile     : %s\n", g_firmwareFilename);
        fprintf(stderr,"cloudFWLocation : %s\n", g_firmwareLocation);
        fprintf(stderr,"cloudFWVersion  : %s\n", g_firmwareVersion);
        fprintf(stderr,"cloudProto      : %s\n", g_firmwareProtocol);
    } else {
        fprintf(stderr, "Missing fields in response\n");
        cJSON_Delete(json);
        return -1;
    }
    if (strcasecmp (g_firmwareProtocol, "http") == 0){
	    port_num = 80;
	    protocol = 1;
    }
    else if (strcasecmp (g_firmwareProtocol, "tftp") == 0) {
	    port_num = 69;
	    protocol = 2;
    }
    else {
	    fprintf(stderr," invalid protocal... please check");
	    return -1;
    }
    if (detect_root_partition() != RETURN_OK) {
	fprintf(stderr, "Could not detect root partition!\n");
	return;
	}
    if (check_image_version(g_firmwareVersion, g_firmwareLocation, g_root_partition) != RETURN_OK){
	    fprintf(stderr, "Firmware download failed, aborting upgrade.\n");
            return RETURN_ERR;
    }
    set_xconf_flag();
    fprintf(stderr,"the value of Xconf upgarde flag  is %d", g_xconf_upgrade_flag);
    cJSON_Delete(json);
    return RETURN_OK;
}

int get_cloud_url(char *url, int url_len)
{
    FILE *fp = fopen("/etc/include.properties", "r");
    if (!fp) {
        perror("Failed to open include.properties");
        return -1;
    }
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "CLOUDURL=", 9) == 0) {
            char *value = line + 9;  // skip "CLOUDURL="
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(url, value, url_len - 1);
            url[url_len - 1] = '\0';
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -1;
}

int detect_root_partition(void)
{
	FILE *fp;
	char cmd[CMD_BUF] = {0};

	fp = fopen("/proc/cmdline", "r");
	if (!fp) {
		fprintf(stderr, "Failed to open /proc/cmdline\n");
		return RETURN_ERR;
	}

	if (!fgets(cmd, CMD_BUF, fp)) {
		fclose(fp);
		fprintf(stderr, "Failed to read /proc/cmdline\n");
		return RETURN_ERR;
	}
	fclose(fp);

	char *start = strstr(cmd, "root=");
	if (!start) {
		fprintf(stderr, "Root partition not found in /proc/cmdline\n");
		return RETURN_ERR;
	}

	start += strlen("root=");	/* Move pointer to start of device path */
	char *end = strchr(start, ' ');	/* Find space after device path */
	if (!end)
		end = start + strlen(start);	/* No space, go till end of line */

	size_t len = end - start;
	if (len >= PATH_LEN)
		len = PATH_LEN - 1;

	snprintf(g_root_partition, sizeof(g_root_partition), "%.*s", (int)len, start);
	g_root_partition[strcspn(g_root_partition, "\r\n ")] = '\0';

	fprintf(stderr, "Active root partition: %s\n", g_root_partition);
	return RETURN_OK;
}
int check_image_version(const char *g_firmwareVersion,
			 const char *g_firmwareLocation,
			 const char *g_root_partition)
{
	char cmd[CMD_BUF], output[512];
	char currentVersion[256]   = {0};
	char line[512];
	int ret;
	char passiveFile[512];

	/* ---------- Step 1: Download file via TFTP ---------- */
	if ( protocol == 2 ) {
	       	snprintf(cmd, sizeof(cmd),
		 "cd /tmp && tftp -g -r %s %s 2>/dev/null",
		 g_firmwareVersion, g_firmwareLocation);
	}
	else if (protocol == 1) {
		fprintf(stderr, "downloading image using curl -fgLo /tmp/%s  http://%s/%s",
                 g_firmwareVersion, g_firmwareLocation,g_firmwareVersion);
		snprintf(cmd, sizeof(cmd),
                 "curl -fgLo /tmp/%s  http://%s/%s",
		 g_firmwareVersion, g_firmwareLocation,g_firmwareVersion);
	}
	ret = run_command(cmd, output, sizeof(output));
	if (ret != 0) {
		fprintf(stderr, "Download image from server failed: %s\n", output);
		return RETURN_ERR;
	}

	/* ---------- Step 2: Open downloaded file ---------- */
	char filePath[512];
	snprintf(filePath, sizeof(filePath), "/tmp/%s", g_firmwareVersion);

	FILE *fp = fopen(filePath, "r");
	if (!fp) {
		perror("fopen downloaded file");
		return;
	}

	if (fgets(line, sizeof(line), fp) != NULL) {
		char *filename = strrchr(line, ' ');
		if (filename) {
			filename++;
			filename[strcspn(filename, "\n")] = 0;
			/* Remove `.bin.wic.bz2`  */
			char *dot = strstr(filename, ".bin.wic.bz2");
			if (dot) *dot = '\0';

			strncpy(extractedVersion, filename, sizeof(extractedVersion) - 1);
			fprintf(stderr, "Xconf-server Version: %s\n", extractedVersion);
		} else {
			fprintf(stderr, "Failed to parse filename from downloaded file\n");
		}
	} else {
		fprintf(stderr, "Downloaded file is empty\n");
	}

	fclose(fp);

	/* ---------- Step 3: Active Partition Version ---------- */
	if (get_image_version("/version.txt", currentVersion, sizeof(currentVersion)) == 0) {
		fprintf(stderr, "Current Partition Version: %s\n", currentVersion);

		if (strcmp(currentVersion, extractedVersion) == 0) {
			fprintf(stderr,"Current  Partition version matched with xconf,  No need of Download and bank switch (%s)\n", extractedVersion);
		        return RETURN_ERR;
		}
	} else {
		fprintf(stderr,"Failed to read version from active partition\n");
	}

	/* ---------- Step 4: Passive Partition Version ---------- */
	const char *passive_partition =
		(strcmp(g_root_partition, "/dev/mmcblk0p4") == 0) ? "/dev/mmcblk0p8" : "/dev/mmcblk0p4";

	fprintf(stderr,"Passive Partition device: %s\n", passive_partition);

	if (access(MOUNT_POINT_2, F_OK) != 0) {
		if (mkdir(MOUNT_POINT_2, 0755) != 0) {
			perror("mkdir failed");
			return -1; 
		}
	}

	snprintf(cmd, sizeof(cmd),
		 "mount -t ext4 -o relatime,sync %s %s 2>&1",
		 passive_partition, MOUNT_POINT_2);
	memset(output, 0, sizeof(output));
	ret = run_command(cmd, output, sizeof(output));
	if (ret == 0) {
                char version_file[512];
                snprintf(version_file, sizeof(version_file),
                         "%s/version.txt", MOUNT_POINT_2);

                if (access(version_file, F_OK) == 0) {
                        fprintf(stderr,
                                "Mount successful and version.txt found at %s\n",
                                version_file);
                } else {
                        fprintf(stderr,
                                "Mount successful but version.txt not found in %s\n",
                                MOUNT_POINT_2);
                }
        } else {
                fprintf(stderr, "Mount not successful\n");
                return -1;
        }

	snprintf(passiveFile, sizeof(passiveFile), "%s/version.txt", MOUNT_POINT_2);

	ret = get_image_version(passiveFile, passiveVersion, sizeof(passiveVersion));
	if (ret == 0) {
                fprintf(stderr,"Passive Partition Version: %s\n", passiveVersion);

                if (strcmp(passiveVersion, extractedVersion) == 0) {
                        fprintf(stderr,"Passive Partition already has required version (%s). Switching banks...\n",
                                extractedVersion);
                        umount(MOUNT_POINT_2);
                        fwupgrade_hal_recover_image();
                        return -1;
                } else {
                        fprintf(stderr,"Passive partition does not contain version.txt yet.\n");
                }
        } else {
                fprintf(stderr, "Mount of passive partition failed\n");
        }
        umount(MOUNT_POINT_2);
        fprintf(stderr,"Neither active nor passive have the required version. Proceeding with upgrade...\n");
        return RETURN_OK;


}

int cleanup_mount(const char *mnt)
{
    char cmd[256];
    char output[512];
    int ret;
    snprintf(cmd, sizeof(cmd), "rm -rf %s/*", mnt);
    ret = run_command(cmd, output, sizeof(output));
    if (ret != 0) {
        fprintf(stderr, "Failed to cleanup %s\n", mnt);
        return -1;
    }
    return 0;
}

int get_image_version(const char *filePath, char *outVersion, size_t outSize)
{
	FILE *fp = fopen(filePath, "r");
	char line[512];

	if (!fp) {
		return -1;	
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "imagename:", 10) == 0) {
			char *val = line + 10;	/* skip "imagename:" */
			val[strcspn(val, "\n")] = 0;
			strncpy(outVersion, val, outSize - 1);
			outVersion[outSize - 1] = '\0';

			fclose(fp);
			return 0;
		}
	}

	fclose(fp);
	return -2;
}

/* fwupgrade_hal_update_and_factoryreset */
/*
Description:  Do FW update and Factory reset
Parameters : <None>
@return the status of the operation.
@retval RETURN_OK if successful.
@retval RETURN_ERR if any reboot/Download is in process.
*/
INT fwupgrade_hal_update_and_factoryreset()
{
    fprintf(stderr,"Entering %s\n", __func__);

    // Image Download to temp
    if(RETURN_OK != fwupgrade_hal_download())
    {
        fprintf(stderr,"failed download the image to CPE_hal download faild\n");
	return RETURN_ERR;
    }

    // Will do signature checks , switch banks and reboot
    if(RETURN_OK != fwupgrade_hal_download_reboot_now())
    {
        fprintf(stderr,"failed download_Reboot the CPE_reboot_now is d=failed\n");
       	return RETURN_ERR;
    }

    return RETURN_OK;
}

/*  fwupgrade_hal_download_install: */
/**
* @description: Downloads and upgrades the firmware
* @param None
* @return the status of the Firmware download and upgrade status
* @retval RETURN_OK if successful.
* @retval RETURN_ERR in case of remote server not reachable
*/
INT fwupgrade_hal_download_install(const char *url)
{
    return RETURN_OK;
}


void copy_blocks(const char *src, const char *dst, long skip_blocks, long count_blocks, int block_size) {
    FILE *fin = fopen(src, "rb");
    FILE *fout = fopen(dst, "wb");
    char buffer[MAX_BLOCK_SIZE];
    if (!fin || !fout) {
        perror("File open failed");
        exit(EXIT_FAILURE);
    }

    if (fseek(fin, skip_blocks * block_size, SEEK_SET) != 0) {
        perror("fseek failed");
        fclose(fin);
        fclose(fout);
        exit(EXIT_FAILURE);
    }

    for (long i = 0; i < count_blocks; ++i) {
        size_t read_bytes = fread(buffer, 1, block_size, fin);
        if (read_bytes != block_size && !feof(fin)) {
            perror("fread failed");
            break;
        }
        if (fwrite(buffer, 1, read_bytes, fout) != read_bytes) {
            perror("fwrite failed");
            break;
        }
    }

    fclose(fin);
    fclose(fout);
}

void write_image_to_device(const char *src_img, const char *dst_dev, int block_size) {
    FILE *fin = fopen(src_img, "rb");
    FILE *fout = fopen(dst_dev, "wb");
    char buffer[MAX_BLOCK_SIZE];
    if (!fin || !fout) {
        perror("File open failed");
        exit(EXIT_FAILURE);
    }

    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, block_size, fin)) > 0) {
        if (fwrite(buffer, 1, read_bytes, fout) != read_bytes) {
            perror("fwrite failed");
            break;
        }
    }

    fclose(fin);
    fclose(fout);
}

INT fwupgrade_hal_recover_image(){

	int bs_512 = 512;
	fprintf(stderr, "recovering base image has started");

        copy_blocks("/dev/mmcblk0p1", "/tmp/bl2.img", 0, 8158, bs_512);
        copy_blocks("/dev/mmcblk0p5", "/tmp/bl2_b.img", 0, 8158, bs_512);
        copy_blocks("/dev/mmcblk0p2", "/tmp/fip.img", 0, 4096, bs_512);
        copy_blocks("/dev/mmcblk0p6", "/tmp/fip_b.img", 0, 4096, bs_512);
	sync();

        fprintf(stderr, "bl2 and fip 2 are ready to swapping\n");

        write_image_to_device("/tmp/bl2_b.img", "/dev/mmcblk0p1", bs_512);
        write_image_to_device("/tmp/bl2.img", "/dev/mmcblk0p5", bs_512);
        write_image_to_device("/tmp/fip_b.img", "/dev/mmcblk0p2", bs_512);
        write_image_to_device("/tmp/fip.img", "/dev/mmcblk0p6", bs_512);
        sync();
        fprintf(stderr,"All done. Rebooting...\n");
        run_command("/sbin/reboot", NULL, 0);

        return RETURN_OK;


}

void set_xconf_flag() {
	g_xconf_upgrade_flag = 1;
	FILE *fp = fopen("/tmp/xconf_flag", "w");
	if (fp) {
	    fprintf(stderr, "Opened /tmp/xconf_flag for writing\n");  // Debug log
	    if (fprintf(fp, "%d\n", g_xconf_upgrade_flag) < 0) {
		perror("fprintf");
	    }
	    if (fclose(fp) == EOF) {
		perror("fclose");
	    } else {
		fprintf(stderr, "Successfully wrote to /tmp/xconf_flag\n");
	    }
	} else {
	    perror("fopen /tmp/xconf_flag");
	}
	
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "recover") == 0) {
        // Recovery Mode
        printf("[INFO] Running recovery process...\n");

        if (RETURN_OK != fwupgrade_hal_recover_image()) {
            fprintf(stderr, "Firmware recovery failed\n");
            return RETURN_ERR;
        }

        printf("Firmware recovery successful\n");
        return RETURN_OK;
    }

    char cmd[CMD_BUF];

    if (RETURN_OK != fwupgrade_hal_get_data_from_Xconf()) {
        fprintf(stderr, "No upgrade or failed to retrieve the data from Xconf server\n");
        return RETURN_ERR;
    } else {
        snprintf(cmd, sizeof(cmd),
            "dmcli eRT setv Device.DeviceInfo.X_RDKCENTRAL-COM_FirmwareDownloadProtocol string %s",
            g_firmwareProtocol);
        run_command(cmd, NULL, 0);

        snprintf(cmd, sizeof(cmd),
            "dmcli eRT setv Device.DeviceInfo.X_RDKCENTRAL-COM_FirmwareDownloadURL string \"%s://%s:%d\"",
            g_firmwareProtocol, g_firmwareLocation, port_num);
        run_command(cmd, NULL, 0);

        snprintf(cmd, sizeof(cmd),
            "dmcli eRT setv Device.DeviceInfo.X_RDKCENTRAL-COM_FirmwareToDownload string %s",
            g_firmwareFilename);
        run_command(cmd, NULL, 0);

        snprintf(cmd, sizeof(cmd),
            "dmcli eRT setv Device.DeviceInfo.X_RDKCENTRAL-COM_FirmwareDownloadAndFactoryReset int 1");
        run_command(cmd, NULL, 0);

        printf("[INFO] Firmware upgrade is in progress.....\n");
    }

    return 0;
}
