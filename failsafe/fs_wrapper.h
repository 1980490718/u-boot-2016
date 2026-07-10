#ifndef __FS_WRAPPER_H__
#define __FS_WRAPPER_H__

struct fs_file {
	char *data;
	int len;
};

struct fsdata_file {
	const struct fsdata_file *next;
	const char *name;
	const char *data;
	const int len;
};

int fs_open(const char *name, struct fs_file *file);
void fs_init(void);

extern const struct fsdata_file file_index_html[];
extern const struct fsdata_file file_404_html[];

#endif