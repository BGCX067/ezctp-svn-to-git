/* ============================================================================
 * Project Name : ezctp Application Programming Interface
 * Module Name  : util/ezctp_util_release_ezcfg_api.c
 *
 * Description  : ezctp API for CThostFtdcXXXApi class
 *
 * Copyright (C) 2012 by ezctp-project
 *
 * History      Rev       Description
 * 2012-03-23   0.1       Copy it from ezbox-project
 * ============================================================================
 */


#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/mount.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <syslog.h>
#include <ctype.h>
#include <stdarg.h>

#include "ezcfg-api.h"
#include "ezctp-util.h"

extern void *ezctp_common_shm_ezcfg_addr;

bool ezctp_util_release_ezcfg_api(void)
{
	if ((ezctp_common_shm_ezcfg_addr != NULL) &&
	    (ezctp_common_shm_ezcfg_addr != (void *)-1)) {
		shmdt(ezctp_common_shm_ezcfg_addr);
	}

	return true;
}
