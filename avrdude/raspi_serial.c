/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2013 Jennifer Temkin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* $Id */

/*
 * avrdude interface for programming via the Raspberry Pi's UART with autoreset.
 *
 * This is basically the same as the arduino, but using a GPIO pin to reset 
 * instead of DTR/RTS.
 *
 * GPIO access adapted from Michael Hennerich's code in linuxgpio.c in this 
 * project, copyright 2009 Analog Devices Inc.
 * 
 * It should be noted that the pin numbers used here refer to the chipset pin
 * number, and -not- the pin number on the P1 header.
 */

#include "ac_cfg.h"

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "avrdude.h"
#include "pgm.h"
#include "stk500_private.h"
#include "stk500.h"
#include "serial.h"


/* read signature bytes - arduino version */
static int raspi_read_sig_bytes(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m)
{
  unsigned char buf[32];

  /* Signature byte reads are always 3 bytes. */

  if (m->size < 3) {
    fprintf(stderr, "%s: memsize too small for sig byte read", progname);
    return -1;
  }

  buf[0] = Cmnd_STK_READ_SIGN;
  buf[1] = Sync_CRC_EOP;

  serial_send(&pgm->fd, buf, 2);

  if (serial_recv(&pgm->fd, buf, 5) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    fprintf(stderr, "%s: stk500_cmd(): programmer is out of sync\n",
			progname);
	return -1;
  } else if (buf[0] != Resp_STK_INSYNC) {
    fprintf(stderr,
			"\n%s: raspi_read_sig_bytes(): (a) protocol error, "
			"expect=0x%02x, resp=0x%02x\n",
			progname, Resp_STK_INSYNC, buf[0]);
	return -2;
  }
  if (buf[4] != Resp_STK_OK) {
    fprintf(stderr,
			"\n%s: raspi_read_sig_bytes(): (a) protocol error, "
			"expect=0x%02x, resp=0x%02x\n",
			progname, Resp_STK_OK, buf[4]);
    return -3;
  }

  m->buf[0] = buf[1];
  m->buf[1] = buf[2];
  m->buf[2] = buf[3];

  return 3;
}

static int raspi_open(PROGRAMMER * pgm, char * port)
{
  int fd;
  char buf[60];


  strcpy(pgm->port, port);
  if (serial_open(port, pgm->baudrate? pgm->baudrate: 115200, &pgm->fd)==-1) {
    return -1;
  }

  /* Export reset pin */
  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (fd < 0) {
    perror("Can't open /sys/class/gpio/export");
    return -1;
  }
  sprintf(buf, "%d", PIN_AVR_RESET);
  write(fd, buf, sizeof(buf));
  close(fd);

  /* Set data dir */
  sprintf(buf, "/sys/class/gpio/gpio%d/direction", PIN_AVR_RESET);
  fd = open(buf, O_WRONLY);
  write(fd, "out", 4);
  close(fd);
  
  /* Bring reset pin low */
  sprintf(buf, "/sys/class/gpio%d/value", PIN_AVR_RESET);
  fd = open(buf, O_RDWR);
  write(fd, "0", 1);
  usleep(50*1000);

  /* Bring reset pin high again */
  write(fd, "1", 1);
  usleep(50*1000);
  close(fd);


  /*
   * drain any extraneous input
   */
  stk500_drain(pgm, 0);

  if (stk500_getsync(pgm) < 0)
    return -1;

  return 0;
}

static void raspi_close(PROGRAMMER * pgm)
{
  int fd;
  unsigned char buf[60];

  /* Reset the device. */
  sprintf(buf, "/sys/class/gpio%d/value", PIN_AVR_RESET);
  fd = open(buf, O_RDWR);
  write(fd, "0", 1);
  usleep(50*1000);
  write(fd, "1", 1);
  close(fd);

  /* Close the GPIO port. */
  sprintf(buf, "/sys/class/gpio/gpio%d/direction", PIN_AVR_RESET);
  fd = open(buf, O_WRONLY);
  write(fd, "in", 3);
  close(fd);

  fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (fd < 0) {
    perror("Can't open /sys/class/gpio/unexport");
    return -1;
  }
  sprintf(buf, "%d", PIN_AVR_RESET);
  write(fd, buf, sizeof(buf));
  close(fd);



  serial_close(&pgm->fd);
  pgm->fd.ifd = -1;
}

const char raspi_desc[] = "RasPi serial programmer for Arduino bootloader";

void raspi_initpgm(PROGRAMMER * pgm)
{
	/* This is mostly a STK500; just the signature is read
     differently than on real STK500v1 
     and the DTR signal is set when opening the serial port
     for the Auto-Reset feature */
  stk500_initpgm(pgm);

  strcpy(pgm->type, "RasPi Serial");
  pgm->read_sig_bytes = raspi_read_sig_bytes;
  pgm->open = raspi_open;
  pgm->close = raspi_close;
}
