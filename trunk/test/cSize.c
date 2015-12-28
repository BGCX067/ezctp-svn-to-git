/* ============================================================================
 * Project Name : ezctp Application Programming Interface
 * Module Name  : cSize.c
 *
 * Description  : ezctp API for CThostFtdcXXXApi class
 *
 * Copyright (C) 2012 by ezctp-project
 *
 * History      Rev       Description
 * 2012-03-09   0.1       Write it from scratch
 * ============================================================================
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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
#include <sys/poll.h>

#include "ezctpApi.h"
#include "ezcfg-api.h"
#include "ezctp-util.h"

int ezctp_util_get_sfit_packets_per_day(void)
{
	int i;
	i = (1*60 + 15); /* 9:00 - 10:15 */
	i += (1*60); /* 10:30 - 11:30 */
	i += (1*60 + 30); /* 13:30 - 15:00 */
	return (60*i*2);
}

int main(int argc, char **argv)
{
	int rc = EXIT_SUCCESS;

	printf("sizeof(struct CThostFtdcDepthMarketDataField)=[%lu]\n",
		sizeof(struct CThostFtdcDepthMarketDataField));
	
	printf("packets per day=[%d]\n",
		ezctp_util_get_sfit_packets_per_day());

	printf("data size per day=[%lu]\n",
		sizeof(struct CThostFtdcDepthMarketDataField) * ezctp_util_get_sfit_packets_per_day());
	return (rc);
}
