/*
 * FRANK DOS host build: the host's directories, for ff_host.c (whose
 * FatFs DIR type clashes with dirent.h's).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define MAX_DIRS 32
static struct {
	DIR *d;
	char path[1024];
} dirs[MAX_DIRS];

int host_dir_open(const char *path)
{
	for (int i = 0; i < MAX_DIRS; i++)
		if (!dirs[i].d) {
			dirs[i].d = opendir(path);
			if (!dirs[i].d)
				return -1;
			snprintf(dirs[i].path, sizeof dirs[i].path, "%s", path);
			return i;
		}
	return -1;
}

int host_dir_next(int h, char *name, size_t n)
{
	struct dirent *e = readdir(dirs[h].d);
	if (!e)
		return 0;
	snprintf(name, n, "%s", e->d_name);
	return 1;
}

const char *host_dir_path(int h) { return dirs[h].path; }

void host_dir_close(int h)
{
	if (dirs[h].d)
		closedir(dirs[h].d);
	dirs[h].d = NULL;
}
