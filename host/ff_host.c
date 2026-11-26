/*
 * FRANK DOS host build: the FatFs API on a host directory, which stands in
 * for the SD card (host_sd_root, set by main.c).
 *
 * FatFs's own types are kept (ff.h), so the code that uses them compiles
 * unchanged. A FIL keeps its host descriptor in obj.sclust and its size in
 * obj.objsize (f_size() reads it); a DIR keeps a host DIR * slot index.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ff.h"

const char *host_sd_root = "sd";

static void host_path(char *out, size_t n, const TCHAR *path)
{
	if (path[0] && path[1] == ':')          /* "0:" */
		path += 2;
	while (*path == '/' || *path == '\\')
		path++;
	snprintf(out, n, "%s/%s", host_sd_root, path);
	for (char *p = out; *p; p++)
		if (*p == '\\')
			*p = '/';
}

static FRESULT from_errno(void)
{
	switch (errno) {
	case ENOENT: return FR_NO_FILE;
	case ENOTDIR: return FR_NO_PATH;
	case EEXIST: return FR_EXIST;
	case EACCES: case EPERM: case EROFS: return FR_DENIED;
	case EMFILE: case ENFILE: return FR_TOO_MANY_OPEN_FILES;
	default: return FR_DISK_ERR;
	}
}

FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) { (void)fs; (void)path; (void)opt; return FR_OK; }

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
	char p[1024];
	host_path(p, sizeof p, path);
	int fl;
	if ((mode & (FA_READ | FA_WRITE)) == (FA_READ | FA_WRITE))
		fl = O_RDWR;
	else if (mode & FA_WRITE)
		fl = O_WRONLY;
	else
		fl = O_RDONLY;
	if (mode & FA_CREATE_ALWAYS)
		fl |= O_CREAT | O_TRUNC;
	else if (mode & FA_CREATE_NEW)
		fl |= O_CREAT | O_EXCL;
	else if (mode & (FA_OPEN_ALWAYS & ~FA_OPEN_EXISTING))
		fl |= O_CREAT;
	memset(fp, 0, sizeof *fp);
	int fd = open(p, fl, 0644);
	if (fd < 0)
		return from_errno();
	struct stat st;
	if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
		close(fd);
		return FR_NO_FILE;
	}
	fp->obj.fs = (FATFS *)(uintptr_t)1;
	fp->obj.sclust = (DWORD)fd;
	fp->obj.objsize = (FSIZE_t)st.st_size;
	fp->flag = mode;
	if ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND)
		fp->fptr = fp->obj.objsize;
	return FR_OK;
}

FRESULT f_close(FIL *fp)
{
	if (!fp->obj.fs)
		return FR_INVALID_OBJECT;
	close((int)fp->obj.sclust);
	fp->obj.fs = NULL;
	return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
	*br = 0;
	if (!fp->obj.fs)
		return FR_INVALID_OBJECT;
	ssize_t n = pread((int)fp->obj.sclust, buff, btr, (off_t)fp->fptr);
	if (n < 0)
		return FR_DISK_ERR;
	fp->fptr += (FSIZE_t)n;
	*br = (UINT)n;
	return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
	*bw = 0;
	if (!fp->obj.fs)
		return FR_INVALID_OBJECT;
	if (!(fp->flag & FA_WRITE))
		return FR_DENIED;
	ssize_t n = pwrite((int)fp->obj.sclust, buff, btw, (off_t)fp->fptr);
	if (n < 0)
		return FR_DISK_ERR;
	fp->fptr += (FSIZE_t)n;
	if (fp->fptr > fp->obj.objsize)
		fp->obj.objsize = fp->fptr;
	*bw = (UINT)n;
	return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
	if (!fp->obj.fs)
		return FR_INVALID_OBJECT;
	if (ofs > fp->obj.objsize && !(fp->flag & FA_WRITE))
		ofs = fp->obj.objsize;
	fp->fptr = ofs;
	return FR_OK;
}

FRESULT f_truncate(FIL *fp)
{
	if (!fp->obj.fs)
		return FR_INVALID_OBJECT;
	if (ftruncate((int)fp->obj.sclust, (off_t)fp->fptr) != 0)
		return FR_DISK_ERR;
	fp->obj.objsize = fp->fptr;
	return FR_OK;
}

FRESULT f_sync(FIL *fp)
{
	if (!fp->obj.fs)
		return FR_INVALID_OBJECT;
	fsync((int)fp->obj.sclust);
	return FR_OK;
}

static void fill_info(FILINFO *fno, const char *name, const struct stat *st)
{
	memset(fno, 0, sizeof *fno);
	snprintf(fno->fname, sizeof fno->fname, "%s", name);
	/* the 8.3 alias: the name itself when it fits, upper-cased */
	size_t len = strlen(name);
	const char *dot = strrchr(name, '.');
	size_t base = dot ? (size_t)(dot - name) : len;
	if (base <= 8 && (!dot || strlen(dot + 1) <= 3) && len <= 12)
		for (size_t i = 0; i <= len; i++)
			fno->altname[i] = (TCHAR)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]);
	fno->fsize = S_ISDIR(st->st_mode) ? 0 : (FSIZE_t)st->st_size;
	fno->fattrib = S_ISDIR(st->st_mode) ? AM_DIR : AM_ARC;
	if (!(st->st_mode & S_IWUSR))
		fno->fattrib |= AM_RDO;
	struct tm tm;
	localtime_r(&st->st_mtime, &tm);
	int y = tm.tm_year + 1900 - 1980;
	if (y < 0)
		y = 0;
	fno->fdate = (WORD)(y << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
	fno->ftime = (WORD)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
}

FRESULT f_stat(const TCHAR *path, FILINFO *fno)
{
	char p[1024];
	host_path(p, sizeof p, path);
	struct stat st;
	if (stat(p, &st) != 0)
		return from_errno();
	if (fno) {
		const char *n = strrchr(p, '/');
		fill_info(fno, n ? n + 1 : p, &st);
	}
	return FR_OK;
}

FRESULT f_unlink(const TCHAR *path)
{
	char p[1024];
	host_path(p, sizeof p, path);
	struct stat st;
	if (stat(p, &st) != 0)
		return from_errno();
	if ((S_ISDIR(st.st_mode) ? rmdir(p) : unlink(p)) != 0)
		return errno == ENOTEMPTY ? FR_DENIED : from_errno();
	return FR_OK;
}

FRESULT f_rename(const TCHAR *from, const TCHAR *to)
{
	char a[1024], b[1024];
	host_path(a, sizeof a, from);
	host_path(b, sizeof b, to);
	if (access(b, F_OK) == 0)
		return FR_EXIST;
	return rename(a, b) == 0 ? FR_OK : from_errno();
}

FRESULT f_mkdir(const TCHAR *path)
{
	char p[1024];
	host_path(p, sizeof p, path);
	return mkdir(p, 0755) == 0 ? FR_OK : from_errno();
}

/* ---- directories ------------------------------------------------------------ */
/* (the host's DIR is dirent.h's, which clashes with FatFs's: host_dir.c holds it) */
int host_dir_open(const char *path);
int host_dir_next(int h, char *name, size_t n);
void host_dir_close(int h);

static int pattern_match(const char *pat, const char *s)
{
	for (; *pat; pat++, s++) {
		if (*pat == '*') {
			while (pat[1] == '*')
				pat++;
			if (!pat[1])
				return 1;
			for (; *s; s++)
				if (pattern_match(pat + 1, s))
					return 1;
			return pattern_match(pat + 1, s);
		}
		if (!*s)
			return 0;
		if (*pat != '?' && tolower((unsigned char)*pat) != tolower((unsigned char)*s))
			return 0;
	}
	return !*s;
}

/* FatFs's "*.*" also takes names without a dot, as DOS does */
static int name_match(const char *pat, const char *s)
{
	if (pattern_match(pat, s))
		return 1;
	size_t n = strlen(pat);
	if (n >= 2 && pat[n - 2] == '.' && pat[n - 1] == '*' && !strchr(s, '.')) {
		char p2[256];
		snprintf(p2, sizeof p2, "%.*s", (int)(n - 2), pat);
		return pattern_match(p2, s);
	}
	return 0;
}

FRESULT f_opendir(FFDIR *dp, const TCHAR *path)
{
	char p[1024];
	host_path(p, sizeof p, path);
	memset(dp, 0, sizeof *dp);
	int h = host_dir_open(p);
	if (h < 0)
		return from_errno() == FR_NO_FILE ? FR_NO_PATH : from_errno();
	dp->obj.fs = (FATFS *)(uintptr_t)1;
	dp->obj.sclust = (DWORD)h;
	snprintf((char *)dp->fn, sizeof dp->fn, "%s", "");
	/* the directory's host path, for f_stat of each entry */
	dp->obj.objsize = 0;
	return FR_OK;
}

FRESULT f_closedir(FFDIR *dp)
{
	if (!dp->obj.fs)
		return FR_INVALID_OBJECT;
	host_dir_close((int)dp->obj.sclust);
	dp->obj.fs = NULL;
	return FR_OK;
}

const char *host_dir_path(int h);

FRESULT f_readdir(FFDIR *dp, FILINFO *fno)
{
	if (!dp->obj.fs)
		return FR_INVALID_OBJECT;
	char name[256];
	for (;;) {
		if (!host_dir_next((int)dp->obj.sclust, name, sizeof name)) {
			fno->fname[0] = 0;
			return FR_OK;
		}
		if (!strcmp(name, ".") || !strcmp(name, "..") || name[0] == '.')
			continue;
		char full[1400];
		snprintf(full, sizeof full, "%s/%s", host_dir_path((int)dp->obj.sclust), name);
		struct stat st;
		if (stat(full, &st) != 0)
			continue;
		fill_info(fno, name, &st);
		return FR_OK;
	}
}

FRESULT f_findnext(FFDIR *dp, FILINFO *fno)
{
	for (;;) {
		FRESULT r = f_readdir(dp, fno);
		if (r != FR_OK || !fno->fname[0])
			return r;
		if (!dp->pat || name_match(dp->pat, fno->fname) || (fno->altname[0] && name_match(dp->pat, fno->altname)))
			return FR_OK;
	}
}

FRESULT f_findfirst(FFDIR *dp, FILINFO *fno, const TCHAR *path, const TCHAR *pattern)
{
	FRESULT r = f_opendir(dp, path);
	if (r != FR_OK)
		return r;
	dp->pat = pattern;
	return f_findnext(dp, fno);
}

FRESULT f_chdir(const TCHAR *path) { (void)path; return FR_OK; }
FRESULT f_chdrive(const TCHAR *path) { (void)path; return FR_OK; }
FRESULT f_getcwd(TCHAR *buff, UINT len) { if (len) snprintf(buff, len, "/"); return FR_OK; }
FRESULT f_getfree(const TCHAR *path, DWORD *nclst, FATFS **fatfs)
{
	(void)path;
	static FATFS fs;               /* (the host's: a 4 GB card, half free) */
	fs.csize = 64;
	fs.n_fatent = 0x20000 + 2;
	*nclst = 0x10000;
	if (fatfs)
		*fatfs = &fs;
	return FR_OK;
}
