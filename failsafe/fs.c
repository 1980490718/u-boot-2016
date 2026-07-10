#include "fs_wrapper.h"
#include "fsdata.c"

static int fs_strcmp(const char *str1, const char *str2)
{
	int i = 0;
	while (1) {
		if (str2[i] == 0 || str1[i] == '\r' || str1[i] == '\n')
			return 0;
		if (str1[i] != str2[i])
			return 1;
		i++;
	}
}

int fs_open(const char *name, struct fs_file *file)
{
	struct fsdata_file_noconst {
		struct fsdata_file *next;
		char *name;
		char *data;
		int len;
	};

	struct fsdata_file_noconst *f;
	for (f = (struct fsdata_file_noconst *)FS_ROOT; f != (struct fsdata_file_noconst *)0;
	     f = (struct fsdata_file_noconst *)f->next) {
		if (fs_strcmp(name, f->name) == 0) {
			file->data = f->data;
			file->len = f->len;
			return 1;
		}
	}
	return 0;
}

void fs_init(void)
{
}