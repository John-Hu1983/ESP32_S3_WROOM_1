#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "user_config.h"

#define USER_FS_PATH_MAX_LEN 192U

/* Mount SPIFFS partition and print usage info. */
esp_err_t usr_fs_init(void);
/* True after SPIFFS mount is completed. */
bool usr_fs_is_ready(void);
/* Return configured mount point, e.g. /storage. */
const char *usr_fs_mount_point(void);

/* Check if a path exists and is a regular file. */
bool usr_fs_path_exists(const char *path);
/* Check if path ends with suffix, e.g. .ogg. */
bool usr_fs_path_has_suffix(const char *path, const char *suffix);

/* Build an absolute path under mount point. */
esp_err_t usr_fs_format_asset_path(const char *group,
                                   const char *locale,
                                   const char *file_name,
                                   char *out_path,
                                   size_t out_path_size);
/* Scan directory and copy matching file names into a flat output buffer. */
esp_err_t usr_fs_list_dir_files_with_suffix(const char *dir_path,
                                            const char *suffix,
                                            char *out_names,
                                            size_t max_files,
                                            size_t name_capacity,
                                            size_t *out_count);
/* Resolve prompt path in locales/<locale>, locales/default, then common. */
esp_err_t usr_fs_resolve_prompt_path(const char *locale,
                                     const char *prompt_name,
                                     char *out_path,
                                     size_t out_path_size);
/* Recursively search all regular files and return malloc-allocated full paths. */
esp_err_t usr_fs_search_all_files(const char *root_dir,
                                  char ***out_paths,
                                  size_t *out_count);
/* Free file path list returned by usr_fs_search_all_files. */
void usr_fs_free_file_paths(char **paths, size_t count);

/* Read full file into heap buffer; caller owns out_data and must free(). */
esp_err_t usr_fs_read_file(const char *full_path,
                           uint8_t **out_data,
                           size_t *out_size);
/* Write bytes to file (truncate/create). */
esp_err_t usr_fs_write_file(const char *full_path,
                            const uint8_t *data,
                            size_t size);
/* Append bytes to file (create if not exists). */
esp_err_t usr_fs_append_file(const char *full_path,
                             const uint8_t *data,
                             size_t size);
