/* Copyright 2016 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <err.h>
#include <inttypes.h>

#include "bitutils.h"
#include "operations.h"
#include "target.h"

#define XSCOM_BASE_PATH "/sys/kernel/debug/powerpc/scom"

static uint64_t xscom_mangle_addr(uint64_t addr)
{
	uint64_t tmp;

	/*
	 * Shift the top 4 bits (indirect mode) down by 4 bits so we
	 * don't lose going through the debugfs interfaces.
	 */
	tmp = (addr & 0xf000000000000000) >> 4;
	addr &= 0x00ffffffffffffff;
	addr |= tmp;

	/* Shift up by 3 for debugfs */
	return addr << 3;
}

static int xscom_read(struct pib *pib, uint64_t addr, uint64_t *val)
{
	int rc;
	int fd = *(int *) pib->priv;

	addr = xscom_mangle_addr(addr);
	lseek64(fd, addr, SEEK_SET);
	rc = read(fd, val, 8);
	if (rc != 8)
		return -1;

	return 0;
}

static int xscom_write(struct pib *pib, uint64_t addr, uint64_t val)
{
	int rc;
	int fd = *(int *) pib->priv;

	addr = xscom_mangle_addr(addr);
	lseek64(fd, addr, SEEK_SET);
	rc = write(fd, &val, 8);
	if (rc != 8)
		return -1;

	return 0;
}

static int host_pib_probe(struct target *target)
{
	struct pib *pib = target_to_pib(target);
	int *fd;
	char *access_fn;
	uint32_t chip_id;

	fd = malloc(sizeof(fd));
	if (!fd)
		return -1;

	chip_id = dt_prop_get_u32(target->dn, "chip-id");
	if (asprintf(&access_fn, "%s/%08d/access", XSCOM_BASE_PATH, chip_id) < 0) {
		free(fd);
		return -1;
	}

	*fd = open(access_fn, O_RDWR);
	free(access_fn);
	if (fd < 0)
		return -1;

	pib->priv = fd;

	return 0;
}

struct pib host_pib = {
	.target = {
		.name = "Host based debugfs SCOM",
		.compatible  = "ibm,host-pib",
		.class_type = "pib",
		.probe = host_pib_probe,
	},
	.read = xscom_read,
	.write = xscom_write,
};
DECLARE_HW_UNIT(host_pib);
