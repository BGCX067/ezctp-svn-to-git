/* ============================================================================
 * Project Name : ezctp Application Programming Interface
 * Module Name  : ezctpSaveMarketData.c
 *
 * Description  : ezctp API for CThostFtdcXXXApi class
 *
 * Copyright (C) 2012 by ezctp-project
 *
 * History      Rev       Description
 * 2012-03-16   0.1       Write it from scratch
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

#include <sys/ipc.h>
#include <sys/shm.h>

#include "ezctpApi.h"
#include "ezcfg-api.h"
#include "ezctp-util.h"


#define MDUSER_PRIORITY               0
#define VERSION                       "0.1"

#define DB_SYNC_INTERVAL              60
#define DB_ROOT_PATH                  "/var/tmpfs/lib/ezctp"

/* minimum number of worker threads */
/* ctrl socket, nvram socket, uevent socket */
#define EZCFG_THREAD_MIN_NUM    3

#define handle_error_en(en, msg) \
	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#if 1
#define DBG(format, args...) do {\
	char path[256]; \
	FILE *dbg_fp; \
	snprintf(path, 256, "/tmp/%d-debug.txt", getpid()); \
        dbg_fp = fopen(path, "a"); \
	if (dbg_fp) { \
		fprintf(dbg_fp, format, ## args); \
		fclose(dbg_fp); \
	} \
} while(0)
#else
#define DBG(format, args...)
#endif

#define INFO(format, args...) do {\
	char path[256]; \
	FILE *info_fp; \
	snprintf(path, 256, "/tmp/%d-debug.txt", getpid()); \
        info_fp = fopen(path, "a"); \
	if (info_fp) { \
		fprintf(info_fp, format, ## args); \
		fclose(info_fp); \
	} \
} while(0)

static bool debug = false;
static int rc = EXIT_FAILURE;
static pthread_t root_thread;
/* 0: stopped, 1:running, 2:stopping */
static int run_state = 0;

static char ezctp_ConfPath[EZCTP_PATH_SIZE];
static char ezctp_RawFilePath[EZCTP_PATH_SIZE];

static void ezctp_mduser_show_usage(void)
{
	printf("Usage: ezctpSaveMarketData -c config_file_path -r raw_file_path [-d] [-D] [-t max_worker_threads]\n");
	printf("\n");
	printf("  -c\tcofig file path\n");
	printf("  -d\tdaemonize\n");
	printf("  -D\tdebug mode\n");
	printf("  -r\traw data file path\n");
	printf("  -t\tmax worker threads\n");
	printf("  -h\thelp\n");
	printf("\n");
}

static int mem_size_mb(void)
{
	FILE *fp;
	char buf[1024];
	long int memsize = -1;

	fp = fopen("/proc/meminfo", "r");
	if (fp == NULL)
		return -1;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		long int value;

		if (sscanf(buf, "MemTotal: %ld kB", &value) == 1) {
			memsize = value / 1024;
			break;
		}
	}

	fclose(fp);
	return memsize;
}

static void *save_thread_routine(void *arg)
{
	FILE *raw_fp = NULL;

	/* tag save thread being running */
	run_state = 1;
	raw_fp = fopen(ezctp_RawFilePath, "a+b");
	if (raw_fp == NULL) {
		//printf("%s(%d) can't open [%s]\n", __func__, __LINE__, ezctp_RawFilePath);
	}

	/* do the save task */
	while (run_state == 1) {
		//printf("%s(%d)\n", __func__, __LINE__);
		if (raw_fp != NULL) {
			if (ezctp_util_save_market_data(raw_fp, sizeof(struct CThostFtdcDepthMarketDataField), 0) == false) {
				//printf("%s(%d)\n", __func__, __LINE__);
			}
		}
		sleep(DB_SYNC_INTERVAL);
	}

	/* save thread stopped */
	run_state = 0;
	if (raw_fp != NULL) {
		fclose(raw_fp);
	}

	return NULL;
}

static void *sig_thread_routine(void *arg)
{
	sigset_t *set = (sigset_t *) arg;
	int s, sig;

	for (;;) {
		s = sigwait(set, &sig);
		if (s != 0) {
			DBG("<6>ezctpSaveMarketData: sigwait errno = [%d]\n", s);
			continue;
		}
		DBG("<6>ezctpSaveMarketData: Signal handling thread got signal %d\n", sig);
		switch(sig) {
		case SIGTERM :
			/* tag save thread to be stopped */
			if (run_state == 1) {
				run_state = 2;
			}
			else {
				run_state = 0;
			}

			while (run_state != 0) {
				sleep(1);
			}
			/* now release resources */
			ezctp_util_release_ezcfg_api();
			rc = EXIT_SUCCESS;
			return NULL;
		case SIGUSR1 :
			DBG("<6>ezctpSaveMarketData: [%d]\n", sig);
			break;
		case SIGCHLD :
			/* do nothing for child exit */
			break;
		default :
			DBG("<6>ezctpSaveMarketData: unknown signal [%d]\n", sig);
			break;
		}
	}

	return NULL;
}

int main(int argc, char **argv)
{
	bool daemonize = false;
	int fd = -1;
	int threads_max = 0;
	int s;
	pthread_t sig_thread, save_thread;
	sigset_t sigset;
	struct ezcfg *ezcfg = NULL;
	char *p;

	/* clean config file path */
	ezctp_ConfPath[0] = '\0';
	ezctp_RawFilePath[0] = '\0';

	for (;;) {
		int c;
		c = getopt( argc, argv, "c:dDhr:t:");
		if (c == EOF) break;
		switch (c) {
			case 'c':
				snprintf(ezctp_ConfPath, sizeof(ezctp_ConfPath), "%s", optarg);
				break;
			case 'r':
				snprintf(ezctp_RawFilePath, sizeof(ezctp_RawFilePath), "%s", optarg);
				break;
			case 'd':
				daemonize = true;
				break;
			case 'D':
				debug = true;
				break;
			case 't':
				threads_max = atoi(optarg);
				break;
			case 'h':
				ezctp_mduser_show_usage();
				return (EXIT_SUCCESS);
			default:
				ezctp_mduser_show_usage();
				return (EXIT_FAILURE);
		}
        }

	if (ezctp_ConfPath[0] == '\0') {
		ezctp_mduser_show_usage();
		return (EXIT_FAILURE);
	}

	if (ezctp_RawFilePath[0] == '\0') {
		ezctp_mduser_show_usage();
		return (EXIT_FAILURE);
	}

	ezcfg = ezcfg_api_common_new(ezctp_ConfPath);
	if (ezcfg == NULL) {
		fprintf(stderr, "%s format error.\n", ezctp_ConfPath);
		exit(EXIT_FAILURE);
	}

	p = ezcfg_api_common_get_root_path(ezcfg);
	if ((p != NULL) && (*p == '/')) {
		if (ezctp_util_mkdir(p, 0777, true) == -1) {
			fprintf(stderr, "%s ezctp_util_mkdir.\n", p);
			ezcfg_api_common_delete(ezcfg);
			exit(EXIT_FAILURE);
		}
	}

	p = ezcfg_api_common_get_sem_ezcfg_path(ezcfg);
	if ((p == NULL) || (*p == '\0')) {
		fprintf(stderr, "%s no sem_ezcfg_path.\n", ezctp_ConfPath);
		ezcfg_api_common_delete(ezcfg);
		exit(EXIT_FAILURE);
	}
	if ((p != NULL) && (*p == '/')) {
		if (ezctp_util_mkdir(p, 0777, false) == -1) {
			fprintf(stderr, "%s ezctp_util_mkdir.\n", p);
			ezcfg_api_common_delete(ezcfg);
			exit(EXIT_FAILURE);
		}
		fd = open(p, O_CREAT|O_RDWR, S_IRWXU);
		if (fd < 0) {
			fprintf(stderr, "cannot open %s\n", p);
			ezcfg_api_common_delete(ezcfg);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

	p = ezcfg_api_common_get_shm_ezctp_path(ezcfg);
	if ((p == NULL) || (*p == '\0')) {
		fprintf(stderr, "%s no shm_ezctp_path.\n", ezctp_ConfPath);
		ezcfg_api_common_delete(ezcfg);
		exit(EXIT_FAILURE);
	}
	if ((p != NULL) && (*p == '/')) {
		if (ezctp_util_mkdir(p, 0777, false) == -1) {
			fprintf(stderr, "%s ezctp_util_mkdir.\n", p);
			ezcfg_api_common_delete(ezcfg);
			exit(EXIT_FAILURE);
		}
		fd = open(p, O_CREAT|O_RDWR, S_IRWXU);
		if (fd < 0) {
			fprintf(stderr, "cannot open %s\n", p);
			ezcfg_api_common_delete(ezcfg);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

	ezcfg_api_common_delete(ezcfg);
	ezcfg = NULL;

	/* before opening new files, make sure std{in,out,err} fds are in a sane state */
	fd = open("/dev/null", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cannot open /dev/null\n");
		exit(EXIT_FAILURE);
	}
	if (write(STDOUT_FILENO, 0, 0) < 0)
		dup2(fd, STDOUT_FILENO);
	if (write(STDERR_FILENO, 0, 0) < 0)
		dup2(fd, STDERR_FILENO);

	if (!debug) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
	}
	if (fd > STDERR_FILENO)
		close(fd);

	if (daemonize)
	{
		pid_t pid = fork();
		switch (pid) {
		case 0:
			/* child process */
			break;

		case -1:
			/* error */
			return (EXIT_FAILURE);

		default:
			/* parant process */
			return (EXIT_SUCCESS);
		}
	}

        /* set scheduling priority for the main daemon process */
	setpriority(PRIO_PROCESS, 0, MDUSER_PRIORITY);

	setsid();

	/* main process */
	INFO("<6>ezctpSaveMarketData: booting...\n");
	/* prepare signal handling thread */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);
	sigaddset(&sigset, SIGUSR1);
	sigaddset(&sigset, SIGTERM);
	s = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (s != 0) {
		DBG("<6>ezctpSaveMarketData: pthread_sigmask\n");
		handle_error_en(s, "pthread_sigmask");
	}

	/* get root thread id */
	root_thread = pthread_self();
	//printf("root_thread=[%ld]\n", (long int)&root_thread);

	s = pthread_create(&sig_thread, NULL, &sig_thread_routine, (void *) &sigset);
	if (s != 0) {
		DBG("<6>ezctpSaveMarketData: pthread_create\n");
		handle_error_en(s, "pthread_create");
	}
	//printf("sig_thread=[%ld]\n", (long int)&sig_thread);

	s = pthread_create(&save_thread, NULL, &save_thread_routine, (void *) &sigset);
	if (s != 0) {
		DBG("<6>ezctpSaveMarketData: pthread_create save\n");
		handle_error_en(s, "pthread_create save");
	}
	//printf("save_thread=[%ld]\n", (long int)&save_thread);

	if (threads_max < EZCFG_THREAD_MIN_NUM) {
		int memsize = mem_size_mb();

		/* set value depending on the amount of RAM */
		if (memsize > 0)
			threads_max = EZCFG_THREAD_MIN_NUM + (memsize / 8);
		else
			threads_max = EZCFG_THREAD_MIN_NUM;
	}

	if (ezctp_util_init_ezcfg_api(ezctp_ConfPath) == false) {
		DBG("<6>ezctp: init ezcfg_api failed\n");
		return (EXIT_FAILURE);
	};

	s = pthread_join(sig_thread, NULL);
	if (s != 0) {
		ezctp_util_release_ezcfg_api();
		DBG("<6>ezctpSaveMarketData: pthread_join\n");
		handle_error_en(s, "pthread_join");
	}

	/* wait for exit signal */
	return (rc);
}
