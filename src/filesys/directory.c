#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

bool dir_add (struct dir *dir, const char *name, block_sector_t inode_sector);

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  bool to_return = inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
  if(sector == ROOT_DIR_SECTOR && to_return)
  {
  	struct dir* dir = dir_open(inode_open(sector));
  	dir_add(dir,".",ROOT_DIR_SECTOR);
  	dir_add(dir,"..",ROOT_DIR_SECTOR);
  	dir_close(dir);
  }
  return to_return;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}


/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

void dir_print(const struct dir *dir)
{
  return;
  struct dir_entry e;
  size_t ofs;
  printf("in inode %d:\n",dir->inode->sector);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) 
  {
	  printf("found %s pointing to %d\n",e.name,e.inode_sector);
  }
  printf("\n");
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    {
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
    }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}


//Returns -1 on failing to parse the pathname.
int dir_used_pathname(const char* pathname)
{
	//printf("dir_used_pathname called on %s\n",pathname);
  struct dir* curr;
  if (pathname == NULL||pathname[0] == '\0')
  {
  	return -1;
  }
  if(pathname[0] == '/')
  {
  	curr = dir_open_root();
  }
  else
  {
  	curr = dir_open(inode_open(thread_current()->curr_directory));
  }
  char *token, *save_ptr;
  char* pathname2 = malloc(strlen(pathname) + 1);
  strlcpy(pathname2,pathname,strlen(pathname) + 1);
  for (token = strtok_r (pathname2, "/", &save_ptr); token != NULL; token = strtok_r (NULL, "/", &save_ptr))
  {
  	struct inode *inode;
	if(!dir_lookup(curr,token,&inode)) 
	{
		return -1;
	}
	else
	{
		dir_close(curr);
		curr = dir_open(inode);
	}
  }
  block_sector_t ret_val = curr->inode->sector;
  dir_close(curr);
  return ret_val;
}

/*
writes the 'new' portion of the given PATHNAME to FILENAME, which is supplied
by the callee. Returns the inode of the enclosing directory of FILENAME
or -1 on failure.
*/
int dir_new_pathname(const char* pathname, char* filename)
{
	//printf("dir_new_pathname called on %s\n",pathname);
	char* pch = strrchr(pathname,'/');
	if(!pch) 
	{
		if(strlen(pathname) > NAME_MAX) return -1;
		strlcpy(filename,pathname,strlen(pathname)+1);
		return thread_current()->curr_directory;
	}
	else
	{
		//make sure pch didn't find trailing slash
		//ASSERT(pch != pathname + strlen(pathname)-1);
				
		//copy const pathname to modify it
		char* pathname2 = malloc(strlen(pathname)+1);
		strlcpy(pathname2,pathname,strlen(pathname)+1);
		
		//set found '/' to 0, to split into pathname2 and file
		int index = pch - pathname;
		pathname2[index] = '\0';
		char* file = pathname2+index+1;
		
		//make sure filename isn't too long
		if(strlen(file) > NAME_MAX) return -1;
		if(strlen(file) == 0)
			strlcpy(filename,".",strlen(".")+1);
		else
			strlcpy(filename,file,strlen(file)+1);
		if(index == 0) return dir_used_pathname("/");
		else return dir_used_pathname(pathname2);
	}
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

bool dir_chdir(char* name)
{
	int new_dir_inode = dir_used_pathname(name);
	if(new_dir_inode == -1) return false;
	thread_current()->curr_directory = new_dir_inode;
	struct dir* end_dir = dir_open(inode_open(new_dir_inode));
	dir_print(end_dir);
	dir_close(end_dir);
	return true;
}

bool dir_mkdir(char* name)
{
	block_sector_t inode_dir;
	ASSERT(free_map_allocate(1,&inode_dir));
	
	static char filename[NAME_MAX + 1];
  	int paren_inode = dir_new_pathname(name, filename);
  	if(paren_inode == -1) return false;
  	struct dir *dir = dir_open (inode_open(paren_inode));
	
	if(dir_create(inode_dir,0) && dir_add (dir, filename, inode_dir))
	{
		struct dir* created_dir = dir_open(inode_open(inode_dir));
		dir_add(created_dir,".",inode_dir);
		dir_add(created_dir,"..",paren_inode);
		dir_print(dir);
		dir_print(created_dir);
		dir_close(created_dir);
		dir_close(dir);
		return true;
	}
	else
	{
		free_map_release(inode_dir,1);
		dir_close(dir);
		return false;
	}
}


/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      bool real_dir = strcmp (e.name, "..") && strcmp (e.name, ".");
      if ((e.in_use) && real_dir)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
