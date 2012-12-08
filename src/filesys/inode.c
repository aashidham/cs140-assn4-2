#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MAX_CACHE 64
#define BLOCK_ENTRIES (BLOCK_SECTOR_SIZE/sizeof(block_sector_t))


/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    unsigned magic;                     /* Magic number. */
    off_t length;                       /* File size in bytes. */
    block_sector_t dbl_indirect;        /* Double indirect data sector. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->length)
  {
  	static block_sector_t buf[BLOCK_ENTRIES];
  	int dbl_block_size = BLOCK_ENTRIES * BLOCK_SECTOR_SIZE;
  	int dbl_block_index = pos / dbl_block_size;
  	int single_block_index = (pos - dbl_block_index*dbl_block_size) / BLOCK_SECTOR_SIZE;
    block_read(fs_device,inode->dbl_indirect,buf);
    block_read(fs_device,buf[dbl_block_index],buf);
    return buf[single_block_index];
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct list block_cache_table;
static int cache_hand = 0;
static char zeros[BLOCK_SECTOR_SIZE];
static block_sector_t buf[BLOCK_ENTRIES]; //will map to the doubly indirect block
static block_sector_t buf2[BLOCK_ENTRIES]; //will map to individual singly indirect blocks


/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  list_init (&block_cache_table);
  int i;
  for(i = 0; i < MAX_CACHE; i++)
  {
  	struct cache_table_entry* curr = malloc(sizeof(struct cache_table_entry));
  	curr->sector_idx = 0;
  	curr->taken = false;
  	list_push_back(&block_cache_table,&curr->elem);
  }
}

static bool inode_inc_size (struct inode *curr, off_t length)
{
	if(length > 0)
	{
		int dbl_block_size = BLOCK_ENTRIES * BLOCK_SECTOR_SIZE;  		
			int dbl_block_index = length / dbl_block_size;
			int single_block_index = (length - dbl_block_index*dbl_block_size) / BLOCK_SECTOR_SIZE;

			ASSERT(free_map_allocate(1,&curr->dbl_indirect));
			block_read(fs_device,curr->dbl_indirect,buf);
			
			int i;
			for(i = 0; i <= dbl_block_index; i++)
			{
				ASSERT(free_map_allocate(1,&buf[i]));
				block_read(fs_device,buf[i],buf2);
				
				int end_single_index = (i == dbl_block_index) ? single_block_index : BLOCK_ENTRIES;
				int j;
				for(j = 0; j <= end_single_index; j++)
				{
					ASSERT(free_map_allocate(1,&buf2[j]));
					block_write (fs_device, buf2[j], zeros);
				}
				
				block_write(fs_device,buf[i],buf2);
			}
			
			block_write(fs_device,curr->dbl_indirect,buf);
			curr -> length = length;
		
	}
	return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode *curr = NULL;

  ASSERT (length >= 0);

  curr = calloc (1, sizeof *curr);
  if (curr != NULL)
    {
      curr->length = length;
      curr->magic = INODE_MAGIC;
      curr->sector = sector;
      inode_inc_size(curr,length);
      block_write (fs_device, sector, curr);
      free (curr);
    }
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;
  static char data[BLOCK_SECTOR_SIZE];

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  block_read(fs_device,sector,data);
  memcpy(inode,data,sizeof(*inode));
  list_push_back (&open_inodes, &inode->elem);
        /*for(e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e))
        {
        	struct inode* curr = list_entry(e,struct inode,elem);
        	printf("(in open) open_inode entry is %d\n",curr->sector);
        }
        printf("\n");*/
  
  inode->sector = sector;
  inode->open_cnt = 1;
  
  //check validity of data
  ASSERT(inode->deny_write_cnt == 0);
  ASSERT(inode->removed == false);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
    	/*struct list_elem *e;
        for(e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e))
        {
        	struct inode* curr = list_entry(e,struct inode,elem);
        	printf("(in close) open_inode entry is %d\n",curr->sector);
        }
        printf("\n");*/

      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
			int dbl_block_size = BLOCK_ENTRIES * BLOCK_SECTOR_SIZE;
			int dbl_block_index = inode->length / dbl_block_size;
			int single_block_index = (inode->length - dbl_block_index*dbl_block_size) / BLOCK_SECTOR_SIZE;
			
			block_read(fs_device,inode->dbl_indirect,buf);
			
			int i;
			for(i = 0; i <= dbl_block_index; i++)
			{
				block_read(fs_device,buf[i],buf2);
				int end_single_index = (i == dbl_block_index) ? single_block_index : BLOCK_ENTRIES;
				int j;
				for(j = 0; j <= end_single_index; j++)
				{
					free_map_release(buf2[j],1);
				}	
				free_map_release(buf[i],1);
			}	
          free_map_release (inode->dbl_indirect, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}



/*
 Adds sector_idx into cache. Returns false on failure.
*/
bool fill_cache(block_sector_t sector_idx)
{
	  struct list_elem *e;
      for(e = list_begin(&block_cache_table); e != list_end(&block_cache_table); e = list_next(e))
      {
      	struct cache_table_entry* curr = list_entry(e,struct cache_table_entry,elem);
      	if(!curr->taken)
      	{
      		curr->taken = true;
      		curr->accessed = true;
			block_read (fs_device, sector_idx, curr->block);
			curr->sector_idx = sector_idx;
			return true;
		}
      }
	return false;
}

void evict_cache(block_sector_t sector_idx)
{
	bool begin = false;
	int i = 0;
	struct list_elem *e = list_begin (&block_cache_table);
	while(true)
	{
		if(i==cache_hand) begin = true;
		if(!begin) i++;
		else
		{
			if (e == list_end(&block_cache_table)) e = list_begin(&block_cache_table);
			struct cache_table_entry* curr = list_entry(e,struct cache_table_entry,elem);
			
			if(curr->accessed)
			{	
				curr->accessed = false;
				cache_hand = (cache_hand+1)%(list_size(&block_cache_table));
			}
			else
			{
				block_write(fs_device,curr->sector_idx,curr->block);
				curr->taken = false;
				curr->sector_idx = 0;
				cache_hand = (cache_hand+1)%(list_size(&block_cache_table));
				ASSERT(fill_cache(sector_idx));
				break;
			}
		}
		e = list_next(e);
	}
}

void evaluate_cache(struct inode *inode,block_sector_t sector_idx, off_t offset,off_t bytes_read, int sector_ofs, void *buffer, off_t size,int chunk_size, bool read)
{
      struct list_elem *e;
      bool found = false;
      for(e = list_begin(&block_cache_table); e != list_end(&block_cache_table); e = list_next(e))
      {
      	struct cache_table_entry* curr = list_entry(e,struct cache_table_entry,elem);
      	if(curr->sector_idx == sector_idx)
      	{
      		found = true;
      		if(read) memcpy (buffer + bytes_read, curr->block + sector_ofs, chunk_size);
      		else memcpy (curr->block + sector_ofs,buffer + bytes_read, chunk_size);
      	}
      }
      if(!found)
      {
      	bool added = fill_cache(sector_idx);
      	if(added)
      	 	inode_read_at(inode,buffer,size,offset);
      	else
      	{
      		evict_cache(sector_idx);
      		inode_read_at(inode,buffer,size,offset);
      	}
      }
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
    	uint8_t *cache = NULL;
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          if (cache == NULL) 
            {
              cache = malloc (BLOCK_SECTOR_SIZE);
              if (cache == NULL)
                break;
            }
          block_read (fs_device, sector_idx, cache);
          memcpy (buffer + bytes_read, cache + sector_ofs, chunk_size);
        }
      free(cache);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
    	  uint8_t *cache = NULL;
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          if (cache == NULL) 
            {
              cache = malloc (BLOCK_SECTOR_SIZE);
              if (cache == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, cache);
          else
            memset (cache, 0, BLOCK_SECTOR_SIZE);
          memcpy (cache + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, cache);
        }
        free(cache);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}
