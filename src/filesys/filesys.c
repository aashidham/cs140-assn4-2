#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  static char filename[NAME_MAX + 1];
  int dir_inode = dir_new_pathname(name, filename);
  if(dir_inode == -1) return false;
  struct dir *dir = dir_open (inode_open(dir_inode));
  //printf("in filesys_create for %s with %d dir_inode and %s filename\n",name,dir_inode,filename);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  if(success) dir_print(dir);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  static char filename[NAME_MAX + 1];
  int dir_inode = dir_new_pathname(name, filename);
  if(dir_inode == -1) return false;
  struct dir *dir = dir_open (inode_open(dir_inode));
  struct inode *inode = NULL;
  //printf("filename in open() call is %s\n",filename);
  if (dir != NULL)
    dir_lookup (dir, filename, &inode);
  dir_print(dir);
  dir_close (dir);
  //printf("found inode %d, about to open it...\n",inode->sector);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  static char filename[NAME_MAX + 1];
  int dir_inode = dir_new_pathname(name, filename);
  if(dir_inode == -1 || dir_inode == ROOT_DIR_SECTOR) return false;

  struct dir *dir = dir_open (inode_open(dir_inode));
  
  bool success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
