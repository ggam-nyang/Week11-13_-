#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/thread.h"
#include "filesys/fat.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();

	thread_current ()->current_dir = dir_open_root ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	bool success = false;
#ifdef EFILESYS

	char* cp_name = (char *)malloc(strlen(name) + 1);
    strlcpy(cp_name, name, strlen(name) + 1);

    char* file_name = (char *)malloc(strlen(name) + 1);
    struct dir* dir = parse_path(cp_name, file_name);

    cluster_t inode_cluster = fat_create_chain(0);

    success = (dir != NULL
               && inode_create(inode_cluster, initial_size, 0)
               && dir_add(dir, file_name, inode_cluster));

    if (!success && inode_cluster != 0)
        fat_remove_chain(inode_cluster, 0);

    dir_close(dir);
    free(cp_name);
    free(file_name);
    return success;

#else

    disk_sector_t inode_sector = 0;
    struct dir *dir = dir_open_root();
    success = (dir != NULL
                && free_map_allocate(1, &inode_sector)
                && inode_create(inode_sector, initial_size, 0)
                && dir_add(dir, name, inode_sector));
    if (!success && inode_sector != 0)
        free_map_release(inode_sector, 1);
    dir_close(dir);

    return success;

#endif
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {

#ifdef EFILESYS

    char* cp_name = (char *)malloc(strlen(name) + 1);
    char* file_name = (char *)malloc(strlen(name) + 1);

    struct dir* dir = NULL;
    struct inode *inode = NULL;

    while(true)
    {
        strlcpy(cp_name, name, strlen(name) + 1);
        dir = parse_path(cp_name, file_name);

        if (dir != NULL)
        {
            dir_lookup(dir, file_name, &inode);
            if(inode && inode->data.is_link)
            {
                dir_close(dir);
                name = inode->data.link_name;
                continue;
            }

        }
        free(cp_name);
        free(file_name);
        dir_close(dir);
        break;
    }
    return file_open(inode);

#else

	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);

#endif
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
#ifdef EFILESYS

    char* cp_name = (char *)malloc(strlen(name) + 1);
    strlcpy(cp_name, name, strlen(name) + 1);

    char* file_name = (char *)malloc(strlen(name) + 1);
    struct dir* dir = parse_path(cp_name, file_name);

    struct inode *inode = NULL;
    bool success = false;

    if (dir != NULL)
    {
        dir_lookup(dir, file_name, &inode);

        if(inode_is_dir(inode))
        {   
            struct dir* cur_dir = dir_open(inode);
            char* tmp = (char *)malloc(NAME_MAX + 1);
            dir_seek(cur_dir, 2 * sizeof (struct dir_entry));

            if(!dir_readdir(cur_dir, tmp))
            {   
                if(inode_get_inumber(dir_get_inode(thread_current()->current_dir)) != inode_get_inumber(dir_get_inode(cur_dir)))
                    success = dir_remove(dir, file_name);
            }

            else
            {   
                success = dir_remove(cur_dir, file_name);
            }
            
            dir_close(cur_dir);
            free(tmp);
        }
        else
        {
            inode_close(inode);
            success = dir_remove(dir, file_name);
        }
    }

    dir_close(dir);	
    free(cp_name);
    free(file_name);

    return success;

#else


	struct dir *dir = thread_current ()->current_dir;
	if (dir == NULL)
		dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;

#endif
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}


struct dir *parse_path(char *path_name, char *file_name)
{
    struct dir *dir = NULL;
    if (path_name == NULL || file_name == NULL)
        return NULL;
    if (strlen(path_name) == 0)
        return NULL;

    if(path_name[0] == '/')
    {
        dir = dir_open_root();
    }
    else
        dir = dir_reopen(thread_current()->current_dir);


    char *token, *nextToken, *savePtr;
    token = strtok_r(path_name, "/", &savePtr);
    nextToken = strtok_r(NULL, "/", &savePtr);

    if(token == NULL)
    {
        token = (char*)malloc(2);
        strlcpy(token, ".", 2);
    }

    struct inode *inode;
    while (token != NULL && nextToken != NULL)
    {
        if (!dir_lookup(dir, token, &inode))
        {
            dir_close(dir);
            return NULL;
        }

        if(inode->data.is_link)
        {   

            char* new_path = (char*)malloc(sizeof(strlen(inode->data.link_name)) + 1);
            strlcpy(new_path, inode->data.link_name, strlen(inode->data.link_name) + 1);

            
            strlcpy(path_name, new_path, strlen(new_path) + 1);
            free(new_path);
 
            strlcat(path_name, "/", strlen(path_name) + 2);
            strlcat(path_name, nextToken, strlen(path_name) + strlen(nextToken) + 1);
            strlcat(path_name, savePtr, strlen(path_name) + strlen(savePtr) + 1);

            dir_close(dir);

            if(path_name[0] == '/')
            {
                dir = dir_open_root();
            }
            else
                dir = dir_reopen(thread_current()->current_dir);


            token = strtok_r(path_name, "/", &savePtr);
            nextToken = strtok_r(NULL, "/", &savePtr);

            continue;
        }
        
        if(!inode_is_dir(inode))
        {
            dir_close(dir);
            inode_close(inode);
            return NULL;
        }
        dir_close(dir);

        dir = dir_open(inode);

        token = nextToken;
        nextToken = strtok_r(NULL, "/", &savePtr);
    }
    strlcpy (file_name, token, strlen(token) + 1);

    return dir;
}