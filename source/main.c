#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#define ERR_ALREADY_EXISTS (Result)0xC82044BE
#define ERR_NO_GAME_CARD (Result)0xC880448D
#define ERR_INVALID_STATE (Result)0xC8A04555
// Inner FAT file/dir names are ascii and can only go up to 16 characters anyway, so this should fit the name + a null byte
#define NAME_UTF8_BUF_SIZE 17

Result r(char *name, Result res) {
	/*if (R_FAILED(res))*/ printf("%s: 0x%08lx\n", name, res);
	return res;
}

Result copy_dir(FS_Archive dstarc, char *dstpath, FS_Archive srcarc, char *srcpath) {
	Result res;
	u32 read = 0;
	Handle srchandle = 0;
	Handle dsthandle = 0;
	Handle srcdirhandle = 0;

	FS_DirectoryEntry entry;
	FS_Path dstpathfsp;

	u64 file_size = 0;
	u32 total_read = 0;
	u8 *file_data = NULL;

	u8 *name_utf8 = calloc(NAME_UTF8_BUF_SIZE, 1); // temporary buffer to use utf16_to_utf8

	res = FSUSER_OpenDirectory(&srcdirhandle, srcarc, fsMakePath(PATH_ASCII, srcpath));
	if (R_FAILED(res)) return r("FSUSER_OpenDirectory", res);

	if (strncmp(dstpath, "/", 2) != 0) {
		// don't attempt to create the root dir
		res = FSUSER_CreateDirectory(dstarc, fsMakePath(PATH_ASCII, dstpath), 0);
		if (R_FAILED(res) && res != ERR_ALREADY_EXISTS) return r("FSUSER_CreateDirectory", res);
	}

	char *src_full_path = calloc(0xFF, sizeof(char));
	char *dst_full_path = calloc(0xFF, sizeof(char));

	do {
		res = FSDIR_Read(srcdirhandle, &read, 1, &entry);
		r("FSDIR_Read", res);
		if (R_FAILED(res)) {
			read = 0;
		}
		if (read == 0) break;

		memset(name_utf8, 0, NAME_UTF8_BUF_SIZE);
		utf16_to_utf8(name_utf8, entry.name, NAME_UTF8_BUF_SIZE - 1);
		// don't include the slash for the root directory
		char *src_format_str = strncmp(srcpath, "/", 2) == 0 ? "%s%s" : "%s/%s";
		char *dst_format_str = strncmp(dstpath, "/", 2) == 0 ? "%s%s" : "%s/%s";

		snprintf(src_full_path, 0xFF, src_format_str, srcpath, name_utf8);
		snprintf(dst_full_path, 0xFF, dst_format_str, dstpath, name_utf8);

		printf(" - %s\n", src_full_path);

		if (entry.attributes & FS_ATTRIBUTE_DIRECTORY) {
			copy_dir(dstarc, dst_full_path, srcarc, src_full_path);
		} else {
			dstpathfsp = fsMakePath(PATH_ASCII, dst_full_path);

			res = FSUSER_OpenFile(&srchandle, srcarc, fsMakePath(PATH_ASCII, src_full_path), FS_OPEN_READ, 0);
			r("FSUSER_OpenFile(src)", res);
			if (R_FAILED(res)) {
				break;
			}

			res = FSFILE_GetSize(srchandle, &file_size);
			r("FSFILE_GetSize", res);
			if (R_FAILED(res)) {
				FSFILE_Close(srchandle);
				break;
			}

			// the largest known save data size is 50mb
			// however there's no way a single file in any of these saves could be that large... right?
			file_data = malloc(file_size);
			if (!file_data) {
				printf("Failed to allocate %llu bytes.\n", file_size);
				FSFILE_Close(srchandle);
				break;
			}

			res = FSFILE_Read(srchandle, &total_read, 0, file_data, file_size);
			r("FSFILE_Read", res);
			r("FSFILE_Close", FSFILE_Close(srchandle));
			if (R_FAILED(res)) {
				free(file_data);
				break;
			}

			res = FSUSER_CreateFile(dstarc, dstpathfsp, entry.attributes, file_size);
			r("FSUSER_CreateFile", res);
			if (R_FAILED(res)) {
				free(file_data);
				break;
			}

			res = FSUSER_OpenFile(&dsthandle, dstarc, dstpathfsp, FS_OPEN_WRITE, entry.attributes);
			r("FSUSER_OpenFile(dst)", res);
			if (R_FAILED(res)) {
				free(file_data);
				break;
			}

			res = FSFILE_Write(dsthandle, NULL, 0, file_data, total_read, 0);
			r("FSFILE_Write", res);
			free(file_data);
			if (R_FAILED(res)) {
				FSFILE_Close(dsthandle);
				break;
			}

			res = FSFILE_Flush(dsthandle);
			r("FSFILE_Flush", res);

			r("FSFILE_Close", FSFILE_Close(dsthandle));
			if (R_FAILED(res)) {
				break;
			}
		}
	} while (read);

fail:
	r("FSDIR_Close", FSDIR_Close(srcdirhandle));
	free(name_utf8);
	free(src_full_path);
	free(dst_full_path);

	return res;
}

u32 get_buckets(u32 count) {
	if (count < 20) {
		return (count < 4) ? 3 : count | 1;
	}

	for (u32 i = 0; i < 100; ++i) {
		u32 ret = count + i;
		if (ret & 1 && ret % 3 && ret % 5 && ret % 7 && ret % 11 && ret % 13 && ret % 17) {
			return ret;
		}
	}
	return count | 1;
}

Result format_with_same_info(FS_Path dst, FS_Path src) {
	Result res;
	u32 directories;
	u32 files;
	bool duplicate_data;

	res = FSUSER_GetFormatInfo(NULL, &directories, &files, &duplicate_data, ARCHIVE_USER_SAVEDATA, src);
	if (R_FAILED(res)) {
		return r("FSUSER_GetFormatInfo", res);
	}

	printf("Directories:    %lu\n", directories);
	printf("Files:          %lu\n", files);
	printf("Duplicate data: %s\n", duplicate_data ? "yes" : "no");
	printf("Formatting game save...\n");
	res = FSUSER_FormatSaveData(ARCHIVE_USER_SAVEDATA, dst, 0x200, directories, files, get_buckets(directories), get_buckets(files), duplicate_data);
	if (R_FAILED(res)) {
		return r("FSUSER_FormatSaveData", res);
	}

	return res;
}

Result get_gamecard_tid(u64 *title_id) {
	Result res;
	u32 count;

	res = AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
	if (R_FAILED(res)) {
		if (res == ERR_NO_GAME_CARD) {
			title_id = 0;
			return 0;
		} else {
			return r("AM_GetTitleCount", res);
		}
	}

	// i'm gonna assume there is one title here
	// unless nintendo releases a new 3DS with two game card slots then i'm fine
	if (count != 1) return 0;

	res = r("AM_GetTitleList", AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, 1, title_id));

	return res;
}

Result create_backup_dir(char *path, u64 title_id, FS_Archive sdarc) {
	Result res;

	res = FSUSER_CreateDirectory(sdarc, fsMakePath(PATH_ASCII, "/sdct-backup"), 0);
	if (R_FAILED(res) && res != ERR_ALREADY_EXISTS) {
		printf("Failed to create: /sdct-backup\n");
		return r("FSUSER_CreateDirectory(/sdct-backup)", res);
	}

	snprintf(path, 0x100, "/sdct-backup/%016llx-%lld", title_id, time(NULL));

	res = FSUSER_CreateDirectory(sdarc, fsMakePath(PATH_ASCII, path), 0);
	if (R_FAILED(res) && res != ERR_ALREADY_EXISTS) {
		printf("Failed to create: %s\n", path);
		return r("FSUSER_CreateDirectory", res);
	}

	return res;
}

Result backup_save(u64 title_id, FS_Archive backuparc) {
	Result res;

	FS_Archive sdarc;

	res = FSUSER_OpenArchive(&sdarc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
	if (R_FAILED(res)) {
		return r("FSUSER_OpenArchive(sdmc)", res);
	}

	char destination[0x100];
	res = create_backup_dir(destination, title_id, sdarc);
	if (R_FAILED(res)) {
		FSUSER_CloseArchive(sdarc);
		return res;
	}

	printf("Creating backup at:\n  %s\n", destination);
	res = copy_dir(sdarc, destination, backuparc, "/");
	if (R_SUCCEEDED(res)) printf("Done!\n");

	FSUSER_CloseArchive(sdarc);
	return res;
}

Result copy_game_save(u64 title_id, FS_MediaType dst_mt, FS_MediaType src_mt) {
	Result res;

	FS_Archive dstarc_backup = 0;
	FS_Archive dstarc = 0;
	FS_Archive srcarc = 0;

	printf("Title ID: %016llx\n", title_id);

	u32 tid_high = (title_id >> 32) & 0xFFFFFFFF;
	u32 tid_low = title_id & 0xFFFFFFFF;

	u32 src_path_info[3] = {src_mt, tid_low, tid_high};
	u32 dst_path_info[3] = {dst_mt, tid_low, tid_high};
	FS_Path src_path = (FS_Path){PATH_BINARY, 12, src_path_info};
	FS_Path dst_path = (FS_Path){PATH_BINARY, 12, dst_path_info};

	char *dest_type = dst_mt == MEDIATYPE_SD ? "digital" : "gamecard";

	printf("Target: %s\n", dst_mt == MEDIATYPE_SD ? "digital" : "gamecard");
	
	res = FSUSER_OpenArchive(&dstarc_backup, ARCHIVE_USER_SAVEDATA, dst_path);
	if (R_FAILED(res)) {
		if (res == ERR_INVALID_STATE) {
			printf("%s save doesn't exist, skipping backup\n", dest_type);
		} else {
			printf("Could not open %s save for backup.\n", dest_type);
			return r("FSUSER_OpenArchive(dst)", res);
		}
	} else {
		res = backup_save(title_id, dstarc_backup);
		FSUSER_CloseArchive(dstarc_backup);
		if (R_FAILED(res)) {
			printf("Failed to create backup, not continuing!\n");
			return res;
		}
	}

	res = format_with_same_info(dst_path, src_path);
	if (R_FAILED(res)) return res;

	res = FSUSER_OpenArchive(&dstarc, ARCHIVE_USER_SAVEDATA, dst_path);
	if (R_FAILED(res)) {
		return r("FSUSER_OpenArchive(dst)", res);
	}

	res = FSUSER_OpenArchive(&srcarc, ARCHIVE_USER_SAVEDATA, src_path);
	if (R_FAILED(res)) {
		FSUSER_CloseArchive(srcarc);
		return r("FSUSER_OpenArchive(src)", res);
	}

	res = copy_dir(dstarc, "/", srcarc, "/");
	if (R_SUCCEEDED(res)) printf("Done!\n");

	r("FSUSER_CloseArchive(dst)", FSUSER_CloseArchive(dstarc));
	r("FSUSER_CloseArchive(src)", FSUSER_CloseArchive(srcarc));
	return res;
}

void print_help(void) {
	consoleClear();
	printf("WARNING: This will OVERWRITE the target save.\n");
	printf("WARNING: This is in development. It can go wrong.\n");
	printf("WARNING: You are responsible for making backups.\n");
	printf("\n");
	printf("Press A to copy a save between gamecard and \n  digital.\n");
	printf("Press START or B to exit.\n");
}

void start_process(void) {
	Result res;
	u64 title_id = 0;

	res = get_gamecard_tid(&title_id);
	if (R_FAILED(res)) {
		printf("Failed to get the gamecard title id.\n");
		return;
	}

	if (title_id == 0) {
		printf("No gamecard found. Insert one and try again.\n");
		return;
	}

	printf("--------------------------------\n");
	printf("Title ID: %016llx\n", title_id);
	printf("\n");
	printf("Press X to copy from GAMECARD to DIGITAL.\n");
	printf("Press Y to copy from DIGITAL to GAMECARD.\n");
	printf("Press B to cancel.\n");
	
	bool wait = false;
	while (aptMainLoop()) {
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (wait) {
			// waiting to return to the main screen
			if (kDown & KEY_B) {
				print_help();
				return;
			}
		} else {
			if (kDown & KEY_X) {
				copy_game_save(title_id, MEDIATYPE_SD, MEDIATYPE_GAME_CARD);
				wait = true;
				printf("Press B to go back.\n");
			} else if (kDown & KEY_Y) {
				copy_game_save(title_id, MEDIATYPE_GAME_CARD, MEDIATYPE_SD);
				wait = true;
				printf("Press B to go back.\n");
			} else if (kDown & KEY_B) {
				print_help();
				return;
			}
		}
	}
}

int main(int argc, char* argv[]) {
	amInit();
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	print_help();

	// Main loop
	while (aptMainLoop()) {
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();

		// Your code goes here
		u32 kDown = hidKeysDown();
		if (kDown & KEY_A) start_process();
		if (kDown & KEY_START || kDown & KEY_B)
			break; // break in order to return to hbmenu
	}

	gfxExit();
	amExit();
	return 0;
}
