
/*
 * uart_logger.c
 *
 *  (c) 2020 SparkCharge
 * All rights reserved.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "lib.h"
#include "terminal.h"

/* for hardware timestamps - since Linux 2.6.30 */
#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#endif

/* from #include <linux/net_tstamp.h> - since Linux 2.6.30 */
#define SOF_TIMESTAMPING_SOFTWARE (1 << 4)
#define SOF_TIMESTAMPING_RX_SOFTWARE (1 << 3)
#define SOF_TIMESTAMPING_RAW_HARDWARE (1 << 6)

#define MAXSOCK 16    /* max. number of CAN interfaces given on the cmdline */
#define MAXIFNAMES 30 /* size of receive name index to omit ioctls */
#define MAXCOL 6      /* number of different colors for colorized output */
#define ANYDEV                                                                \
  "any"            /* name of interface to receive from any CAN interface     \
                    */
#define ANL "\r\n" /* newline in ASC mode */

#define SILENT_INI 42 /* detect user setting on commandline */
#define SILENT_OFF 0  /* no silent mode */
#define SILENT_ANI 1  /* silent mode with animation */
#define SILENT_ON 2   /* silent mode (completely silent) */

#define FORMAT_CANDUMP 0 /* Use the same log format as candump.c */
#define FORMAT_SAVVY 1   /* Use the SavvyCAN CSV log format */
#define FORMAT_BINARY 2  /* Store binary data in the log */

#define ERROR_LENGTH (64) /* Sizeof error message strings */

#define BOLD ATTBOLD
#define RED ATTBOLD FGRED
#define GREEN ATTBOLD FGGREEN
#define YELLOW ATTBOLD FGYELLOW
#define BLUE ATTBOLD FGBLUE
#define MAGENTA ATTBOLD FGMAGENTA
#define CYAN ATTBOLD FGCYAN

const char col_on[MAXCOL][19] = { BLUE, RED, GREEN, BOLD, MAGENTA, CYAN };
const char col_off[] = ATTRESET;

static char *cmdlinename[MAXSOCK];
static __u32 dropcnt[MAXSOCK];
static __u32 last_dropcnt[MAXSOCK];
static char devname[MAXIFNAMES][IFNAMSIZ + 1];
static int dindex[MAXIFNAMES];
static int max_devname_len; /* to prevent frazzled device name output */
const int canfd_on = 1;

#define MAXANI 4
#define BUFFER_AVAILABLE (960)
#define BUFFER_PREFIX (64)

const char anichar[MAXANI] = { '|', '/', '-', '\\' };
const char extra_m_info[4][4] = { "- -", "B -", "- E", "B E" };

extern int optind, opterr, optopt;

static volatile int running = 1;
FILE *logfile = NULL;
unsigned char silent = SILENT_INI;

// Changes to support UART logging
#define MAX_DEVICE_NAME (83)
static char uart_name[MAX_DEVICE_NAME] = "\0";
static char
    uart_buffer[BUFFER_AVAILABLE + BUFFER_PREFIX]; /* basic input buffer */
static char print_buffer[BUFFER_AVAILABLE + BUFFER_PREFIX];
static int uart_buffer_size;
static int print_buffer_size;
static bool non_blank;
static speed_t uart_speed = B115200;
static int uart_fd = 0;
static struct timespec uart_tv;
static long rcvTimeout = 5000;
static long idleTimems;
static struct timespec now_tv;
static bool logOpened;

int open_uart (char *device_name, speed_t baud)
{
  int file_descriptor;
  char outstring[ERROR_LENGTH];
  struct termios uart_options;

  file_descriptor = open (device_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (0 > file_descriptor)
  {
    snprintf (outstring, ERROR_LENGTH, "TTY open failed on '%s'", device_name);
    perror (outstring);
  }
  else
  {
    tcgetattr (file_descriptor, &uart_options); // Set line options
    // Set the BAUD rate
    cfsetispeed (&uart_options, baud);
    cfsetospeed (&uart_options, baud);
    // N81 symbols
    uart_options.c_cflag &= ~PARENB;
    uart_options.c_cflag &= ~CSTOPB;
    uart_options.c_cflag &= ~CSIZE;
    uart_options.c_cflag |= CS8;
    // Check then strip parity bits
    uart_options.c_iflag |= (INPCK | ISTRIP);
    // No hardware flow control
    uart_options.c_cflag &= ~CRTSCTS;
    // Line oriented input, no echoing
    uart_options.c_lflag |= ICANON;
    uart_options.c_lflag &= ~(ECHO | ECHOE | ISIG);
    uart_options.c_cflag |= (CLOCAL | CREAD);
    // Output processing - no thanks
    uart_options.c_oflag &= ~OPOST;
    tcsetattr (file_descriptor, TCSANOW, &uart_options);
  }
  return (file_descriptor);
}

void print_usage (char *prg)
{
  fprintf (stderr, "%s - Record UART traffic to a file.\n", prg);
  fprintf (stderr, "\nUsage: %s [options] \n", prg);
  fprintf (stderr, "  (use CTRL-C to terminate %s)\n\n", prg);
  fprintf (stderr, "Options:\n");
  fprintf (stderr, "         -T <msecs>  (close log after <msecs> without any "
                   "reception)\n");
  fprintf (stderr, "         -u <device> (read input from UART 'device')\n");
  fprintf (stderr, "         -b <speed>  (set UART 'device' to BAUD rate "
                   "'speed'; default is B115200)\n");
  fprintf (stderr, "\n");
}

void sigterm (int signo) { running = 0; }

int openLog ()
{
  time_t epochTime;
  struct tm calendarTime;
  char fileName[83];
  int retCode = 0;

  if (time (&epochTime) == (time_t)-1)
  {
    perror ("Error retrieving system time");
    retCode = 1;
  }
  else
  {
    localtime_r (&epochTime, &calendarTime);

    sprintf (fileName, "UARTLog-%04d-%02d-%02d_%02d%02d%02d.log",
             calendarTime.tm_year + 1900, calendarTime.tm_mon + 1,
             calendarTime.tm_mday, calendarTime.tm_hour, calendarTime.tm_min,
             calendarTime.tm_sec);

    fprintf (stderr, "Enabling Logfile '%s'\n", fileName);

    logfile = fopen (fname, "w");
    if (NULL == logfile)
    {
      perror ("logfile");
      retCode = 2;
    }
  }
  return retCode;
}

int main (int argc, char **argv)
{
  fd_set rdfs;
  int s[MAXSOCK];
  unsigned char timestamp = 0;
  unsigned char hwtimestamp = 0;
  unsigned char down_causes_exit = 1;
  unsigned char dropmonitor = 0;
  unsigned char extra_msg_info = 0;
  unsigned char silentani = 0;
  unsigned char color = 0;
  unsigned char view = 0;
  unsigned char log = 0;
  unsigned char logfrmt = 0;
  unsigned char logformat = 0;
  int count = 0;
  int rcvbuf_size = 0;
  int opt, ret;
  int currmax, numfilter;
  int join_filter;
  char *ptr, *nptr;
  struct sockaddr_can addr;
  char ctrlmsg[CMSG_SPACE (sizeof (struct timeval)
                           + 3 * sizeof (struct timespec) + sizeof (__u32))];
  struct iovec iov;
  struct msghdr msg;
  struct cmsghdr *cmsg;
  struct can_filter *rfilter;
  can_err_mask_t err_mask;
  struct canfd_frame frame;
  int nbytes, index, maxdlen;
  struct ifreq ifr;
  struct timeval tv, last_tv;
  struct timeval timeout, timeout_config = { 0, 0 }, *timeout_current = NULL;

  strncpy (uart_name, "/dev/ttyUSB0", MAX_DEVICE_NAME - 1); // Set default UART

  signal (SIGTERM, sigterm); // Terminate cleanly on <ctrl-C>
  signal (SIGHUP, sigterm);  //  or other causes.
  signal (SIGINT, sigterm);

  last_tv.tv_sec = 0;
  last_tv.tv_usec = 0;

  while ((opt = getopt (argc, argv, "T:u:b:?")) != -1)
  {
    switch (opt)
    {
    case 'u':
      strncpy (uart_name, optarg, MAX_DEVICE_NAME - 1);
      break;

    case 'b':
      i = atoi (optarg);
      if (9600 == i)
      {
        uart_speed = B9600;
      }
      else if (19200 == i)
      {
        uart_speed = B19200;
      }
      else if (38400 == i)
      {
        uart_speed = B38400;
      }
      else if (57600 == i)
      {
        uart_speed = B57600;
      }
      else if (115200 == i)
      {
        uart_speed = B115200;
      }
      break;

    case 'T':
      errno = 0;
      rcvTimeout = strtol (optarg, NULL, 0);
      if (errno != 0)
      {
        print_usage (basename (argv[0]));
        exit (1);
      }
      break;

    default:
      print_usage (basename (argv[0]));
      exit (1);
      break;
    }
  }

  uart_fd = open_uart (uart_name, uart_speed);
  if (0 > uart_fd)
  {
    perror("UART open failed");
    return 1;
  }

  logOpened = false;

  while (running)
  {

    if (0 < rcvTimeout) // We can be told to close a log file if the UART is
    {                   // idle too long.  It will get opened when another line is recieved
      if (logOpened)
      {
        clock_gettime (CLOCK_REALTIME, &now_tv);
        idleTimems = ((1000 * now_tv.tv_sec - uart_tv.tv_sec)
                      + (now_tv.tv_nsec / 1000000))
                      - (uart_tv.tv_nsec / 1000000);
        if (idleTimems < rcvTimeout)
        {
          fclose (logfile);
          logOpened = false;
        }
      }
    }

    uart_buffer_size = read (uart_fd, uart_buffer, BUFFER_AVAILABLE);
    if (0 < uart_buffer_size)
    {
      if (false == logOpened) // Log file is opened when we get a message,mclosed when timed out
      {                       //  so open a log if there isn't one at the moment
        if (0 != openLog ())
        {
          logOpened = true;
        }
      }

      uart_buffer[uart_buffer_size] = '\0';
      if (false == logOpened)
      {
        strncpy (print_buffer, "UNLOGGED --  \0", BUFFER_PREFIX);
        print_buffer_size = strlen (print_buffer);
        strncpy (&print_buffer[print_buffer_size], uart_buffer,
                 BUFFER_AVAILABLE);
        print_buffer_size = strlen (print_buffer);
        printf (print_buffer);
      }
      else
      {
        printf (uart_buffer);

        index = 0;
        non_blank = false;
        while (('\0' != uart_buffer[index]) && (index < sizeof (uart_buffer)))
        {
          if (('\r' == uart_buffer[index]) || ('\n' == uart_buffer[index]))
          {
            uart_buffer[index] = ' ';
          }
          else if ('\x20' < uart_buffer[index])
          {
            non_blank = true;
          }
          ++index;
        }
        if (non_blank)
        {
          clock_gettime (CLOCK_REALTIME, &uart_tv);
          fprintf (logfile, "(%010lu.%06lu) uart %s\n", uart_tv.tv_sec,
                   uart_tv.tv_nsec / 1000, uart_buffer);
        }
      }
    }
  }

  for (i = 0; i < currmax; i++)
    close (s[i]);

  if (0 < uart_fd)
  {
    close (uart_fd);
  }

  if (log)
    fclose (logfile);

  return 0;
}
