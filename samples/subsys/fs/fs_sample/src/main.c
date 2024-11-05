/*
 * Copyright (c) 2019 Tavish Naruka <tavishnaruka@gmail.com>
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2023 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sample which uses the filesystem API and SDHC driver */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>

#if defined(CONFIG_FAT_FILESYSTEM_ELM)

#include <ff.h>

/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT   "/" DISK_DRIVE_NAME ":"

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

#elif defined(CONFIG_FILE_SYSTEM_EXT2)

#include <zephyr/fs/ext2.h>

#define DISK_DRIVE_NAME "SDMMC"
#define DISK_MOUNT_PT   "/ext"

static struct fs_mount_t mp = {
	.type = FS_EXT2,
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
	.storage_dev = (void *)DISK_DRIVE_NAME,
	.mnt_point = "/ext",
};

#endif

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#define FS_RET_OK FR_OK
#else
#define FS_RET_OK 0
#endif

LOG_MODULE_REGISTER(main);

#define MAX_PATH          128
#define SOME_FILE_NAME    "some.dat"
#define SOME_DIR_NAME     "some"
#define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))

static int lsdir(const char *path);
#ifdef CONFIG_FS_SAMPLE_CREATE_SOME_ENTRIES
static bool create_some_entries(const char *base_path)
{
	char path[MAX_PATH];
	struct fs_file_t file;
	int base = strlen(base_path);

	fs_file_t_init(&file);

	if (base >= (sizeof(path) - SOME_REQUIRED_LEN)) {
		LOG_ERR("Not enough concatenation buffer to create file paths");
		return false;
	}

	LOG_INF("Creating some dir entries in %s", base_path);
	strncpy(path, base_path, sizeof(path));

	path[base++] = '/';
	path[base] = 0;
	strcat(&path[base], SOME_FILE_NAME);

	if (fs_open(&file, path, FS_O_CREATE) != 0) {
		LOG_ERR("Failed to create file %s", path);
		return false;
	}
	fs_close(&file);

	path[base] = 0;
	strcat(&path[base], SOME_DIR_NAME);

	if (fs_mkdir(path) != 0) {
		LOG_ERR("Failed to create dir %s", path);
		/* If code gets here, it has at least successes to create the
		 * file so allow function to return true.
		 */
	}
	return true;
}
#endif

static const char *disk_mount_pt = DISK_MOUNT_PT;

int main(void)
{
	/* raw disk i/o */
	do {
		static const char *disk_pdrv = DISK_DRIVE_NAME;
		uint64_t memory_size_mb;
		uint32_t block_count;
		uint32_t block_size;

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_INIT, NULL) != 0) {
			LOG_ERR("Storage init ERROR!");
			break;
		}

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
			LOG_ERR("Unable to get sector count");
			break;
		}
		LOG_INF("Block count %u", block_count);

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
			LOG_ERR("Unable to get sector size");
			break;
		}
		printk("Sector size %u\n", block_size);

		memory_size_mb = (uint64_t)block_count * block_size;
		printk("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_DEINIT, NULL) != 0) {
			LOG_ERR("Storage deinit ERROR!");
			break;
		}
	} while (0);

	mp.mnt_point = disk_mount_pt;

	int res = fs_mount(&mp);

	if (res == FS_RET_OK) {
		printk("Disk mounted.\n");
		/* Try to unmount and remount the disk */
		res = fs_unmount(&mp);
		if (res != FS_RET_OK) {
			printk("Error unmounting disk\n");
			return res;
		}
		res = fs_mount(&mp);
		if (res != FS_RET_OK) {
			printk("Error remounting disk\n");
			return res;
		}

		if (lsdir(disk_mount_pt) == 0) {
#ifdef CONFIG_FS_SAMPLE_CREATE_SOME_ENTRIES
			printk("Creating some entries\n");
			if (create_some_entries(disk_mount_pt)) {
				lsdir(disk_mount_pt);
			}
#endif
		}
	} else {
		printk("Error mounting disk.\n");
	}

	fs_unmount(&mp);

	while (1) {
		k_sleep(K_MSEC(1000));
	}
	return 0;
}

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
static int lsdir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int count = 0;

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		printk("Error opening dir %s [%d]\n", path, res);
		return res;
	}

	printk("\nListing dir %s ...\n", path);
	for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			printk("[DIR ] %s\n", entry.name);
		} else {
			printk("[FILE] %s (size = %zu)\n", entry.name, entry.size);
		}
		count++;
	}
	printk("Total: %d entries\n", count);

	// // Read, write & seek
	// struct fs_file_t _file;
	// fs_file_t_init(&_file);
	// const char *fname = "/SD:/text.txt";
	// int rc = fs_open(&_file, fname, FS_O_CREATE | FS_O_RDWR);
	// if (rc < 0) {
	// 	printk("Error opening file %s [%d]\n", fname, rc);
	// 	return rc;
	// }

	// printk("Opened file %s\n", fname);

	// size_t idx = 0;
	// while (true) {
	// 	// Seek at the end
	// 	// if (fs_seek(&_file, 0, FS_SEEK_END)) {
	// 	// 	printk("Error seeking file %s [%d]\n", fname, rc);
	// 	// 	return rc;
	// 	// }

	// 	// Read data in buffer
	// 	char buffer[32];
	// 	int len = fs_read(&_file, buffer, sizeof(buffer));
	// 	if (len < 0) {
	// 		printk("Error reading file %s [%d]\n", fname, rc);
	// 		return rc;
	// 	}
	// 	printk("Read: %d; %s\n", len, buffer);

	// 	// Write some data
	// 	idx++;
	// 	char data[32];
	// 	sprintf(data, "Line %lu\n", idx);
	// 	printk("Attempting to write: %s", data);
	// 	rc = fs_write(&_file, data, strlen(data));
	// 	if (rc < 0) {
	// 		printk("Error writing file %s [%d]\n", fname, rc);
	// 		return rc;
	// 	}
	// 	printk("Wrote: %s", data);

	// 	int rcs = fs_sync(&_file);
	// 	if (rcs < 0) {
	// 		printk("Error syncing file %s [%d]\n", fname, rcs);
	// 		return rcs;
	// 	}

	// 	// Seek back to the beginning
	// 	// int rc = fs_seek(&_file, 0, FS_SEEK_SET);
	// 	// if (rc < 0) {
	// 	// 	printk("Error seeking file %s [%d]\n", fname, rc);
	// 	// 	return rc;
	// 	// }

	// 	// Read data in buffer
	// 	// char buffer[32];
	// 	// int len = fs_read(&_file, buffer, sizeof(buffer));
	// 	// if (len < 0) {
	// 	// 	printk("Error reading file %s [%d]\n", fname, rc);
	// 	// 	return rc;
	// 	// }

	// 	// printk("Read: %d\n", len);

	// 	k_sleep(K_MSEC(1000));
	// }

	/* Verify fs_closedir() */
	// printk("Closed dir %s\n", path);
	fs_closedir(&dirp);
	if (res == 0) {
		res = count;
	}

	printk("Closed dir %s\n", path);

	return res;
}
