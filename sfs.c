/*
   Simple File System
   This code is derived from function prototypes found /usr/include/fuse/fuse.h
   Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
   His code is licensed under the LGPLv2.
   */

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
static int sfs_error(char *str)
{
  int ret = -errno;
  log_msg("    ERROR %s: %s\n", str, strerror(errno)); 
  return ret;
}

typedef struct superblock_struct{
  char sfsname[5];
  int num_inodes;
  int num_datablocks; 
  int total_num_inodes;
  int total_num_datablocks;
  char inode_map[100];
}superblock;

typedef struct inode_struct{
  int type;//1 if directory, 2 if regular file
  int link_count;//how many hardlinks are pointing to it
  int size_written;//number of bytes of remaining space in file
  int mode;//read or write mode?
  int db[11];
}inode;

typedef struct inode_array_struct{
  inode i[5];
}inode_array;

typedef struct direntry_struct{
  char name[120];
  int inode_num;
}direntry;

typedef struct direntry_array_struct{
  direntry d[4];
}direntry_array;



void *sfs_init(struct fuse_conn_info *conn)
{
  fprintf(stderr, "in bb-init\n");
  log_msg("\nsfs_init()\n");

  //log_conn(conn);
  //log_fuse_context(fuse_get_context());
  //log_msg("about to open disk (testfsfile)\n");
  disk_open(SFS_DATA->diskfile);

  //setting up the superblock struct in block 0 below
  char buf[512];
  memset(buf, '\0', 512);
  superblock sb;
  memset(sb.sfsname, '\0', sizeof(sb.sfsname));
  char src[] = "poop";
  strncpy(sb.sfsname, src, sizeof(src));
  sb.num_inodes = 100;
  sb.num_datablocks = 1100;
  sb.total_num_inodes = 100;
  sb.total_num_datablocks = 1100;

  //filling in the char map (instead of bit map) for inode and data blocks below 
  int i;
  for(i = 0; i < 100; i++){
    sb.inode_map[i] = 0;
  }
  block_write(0, &sb);

  // CREATING AND FILLING IN THE DATA CHAR MAPS, BLOCKS 1-3, (3 total)
  char data_map[367];
  memset(data_map, 0, 367);
  for(i = 1; i <= 3; i++){
    // set this to something unusable (not 0 or 1), since there should only be 1100 data blocks, not 1101
    if(i == 3){
      data_map[366] = 'a';
    }
    block_write(i, data_map);
  }

  memset(buf, 0, 512);

  // CREATING INODE ARRAY STRUCTS, BLOCKS 4-23 (20 total)
  inode_array x;
  int ii;
  for(i = 4; i <= 23; i++){
    for(ii = 0; ii < 5; ii++){
      x.i[ii].type = 0;
      x.i[ii].link_count = 0;
      x.i[ii].size_written = 0;
      x.i[ii].mode = 0;
      int d;
      for(d = 0; d <= 11; d++){
        x.i[ii].db[d] = -1;
      }
    }
    block_write(i, &x);
  }

  //writing in direntry array structs in blocks 24 - 48 (25 total)
  direntry_array y;
  for(i = 24; i <= 48; i++){
    for(ii = 0; ii < 4; ii++){
      memset(y.d[ii].name, '\0', sizeof(y.d[ii].name));
      y.d[ii].inode_num = -1;//this is the block num for testing 
    }
    block_write(i, &y);
  }

  return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */

void sfs_destroy(void *userdata)
{
    log_msg("about to close disk\n");  
    int i, j;
    char data_map_buf[512];
    for(i = 1; i<=3; i++){
      block_read(i, data_map_buf);
      for(j = 0; j<367; j++){
        if(data_map_buf[j] == 1){
          char data_block_buf[512];
          block_read(j+49, data_block_buf);
          memset(data_block_buf, '\0', 512);
          block_write(j+49, data_block_buf);
        }
      }
    }
    disk_close();
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}


static void sfs_fullpath(char fpath[PATH_MAX], const char *path)
{
  strcpy(fpath, SFS_DATA->diskfile);
  strncat(fpath, path, PATH_MAX); // ridiculously long paths will
  // break here
  log_msg(" sfs_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n", SFS_DATA->diskfile, path, fpath);
}

int * find_direntry ( const char * path, int * array)
{
  log_msg("\nfind_direntry( path=\"%s\")\n", path);

  int * copy = malloc( sizeof( int ) * 3);
  char buff[512];
  int i, j;

  for(i = 24; i < 49; i++) {
    block_read(i, buff);
    //iterating through direntry_array within block j (j between 24 - 48 inclusive)
    direntry_array *pde = (direntry_array *)buff;
    for(j = 0; j < 4; j++) {
      //log_msg("find_dirent LINE %d: direntry contents: name=%s, inode_num=%d\n",__LINE__, pde->d[j].name, pde->d[j].inode_num);
      if(strcmp(path, pde->d[j].name) == 0){
        log_msg("find_direntry LINE %d DIRENTRY FOUND, returning inode_num = %d\n",__LINE__, pde->d[j].inode_num);
        copy[0] = pde->d[j].inode_num;
        copy[1] = i;
        copy[2] = j;
        return copy;
      }
    }
  }
  log_msg("find_direntry LINE %d DIRENTRY NOT FOUND, returning -1\n",__LINE__);
  copy[0] = -1;
  copy[1] = i;
  copy[2] = j;
  return copy; 
}

/* Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */

int sfs_getattr(const char *path, struct stat *statbuf)
{
  int retstat = 0;
  char fpath[PATH_MAX];
  log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);
  memset(statbuf, 0, sizeof(struct stat));

  int array[3];
  int * array_ptr;
  inode attr_inode; 

  if (strcmp(path, "/") == 0) 
  {
    log_msg("sfs_getattr LINE %d: root directory",__LINE__);
    statbuf->st_mode = S_IFDIR | 0777;
    statbuf->st_nlink = 2;
    statbuf->st_mtime = time(NULL);
    statbuf->st_ctime = time(NULL);
    log_stat(statbuf);
    //I believe we don't have to free here beccause array_ptr was never malloced;
    return retstat;
  } else if ((array_ptr = find_direntry(path, array))[0] != -1) 
  {
    //log_msg("sfs_getattr LINE %d: direntry contents: name=%s, inode_num=%d\n", __LINE__, pde->d[i].name, pde->d[i].inode_num);
    int inode_num = array_ptr[0];
    int inode_block = inode_num/5 + 4;
    int inode_block_index = inode_num%5;
    char inode_buf[512];
    block_read(inode_block, inode_buf);
    inode_array *inode_arr = (inode_array *)inode_buf;
    log_msg("I am a file called=>  path=\"%s\")\n", path);
    
    statbuf->st_mode = S_IFREG | 0777;
    statbuf->st_nlink = 1;
    statbuf->st_size = inode_arr->i[inode_block_index].size_written;
  //  statbuf->st_blocks = 2;
    statbuf->st_mtime = time(NULL);
    statbuf->st_ctime = time(NULL);
    log_stat(statbuf);
    free(array_ptr);
    return retstat;
  } else 
  {
    log_msg("sfs_getattr LINE %d: DIRENTRY not found, returning -ENOENT",__LINE__ );
    retstat = -ENOENT;
    log_stat(statbuf);
    free(array_ptr);
    return retstat;
  }
}


/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */

//CHECK if file name already exists
//THIS IS HOW YOU GO THROUGH THE DIRENTRIES
//block_read(24, buf); // you will need to go through blocks 24-48
//direntries = (direntry_array *)buf; // direntries is a direntry_array (initialized above)

int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
  //log_fi(fi);
  char buf[512];
  int retstat = 0;
  int i,j;
  log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n", path, mode, fi);
  log_msg("sfs_create LINE %d: SNIGGY SAYS THIS IS THE PATH: %s\n",__LINE__, path);
  log_msg("now creating file\n");

  //time to go through inode char map to find next free direntry/inode
  char sb_b[512];
  block_read(0, sb_b);
  superblock *sb_buf = (superblock *)sb_b;
  char *inode_map = sb_buf->inode_map;
  int free_inode = -1;
  log_msg("sfs_create LINE %d: num free inodes: %d\n",__LINE__, sb_buf->num_inodes);
  if(sb_buf->num_inodes > 0){
    //log_msg("num free inodes: %d\n", sb_buf->num_inodes);
    for(free_inode = 0; free_inode < 100; free_inode++){
      log_msg("free_inode num: %d, val: %d\n", free_inode, inode_map[free_inode]);
      if(inode_map[free_inode] == 0){
        //WE HAVE A FREE INODE
        log_msg("FREE INODE: %d\n", free_inode);
        sb_buf->num_inodes--;
        inode_map[free_inode] = 1;
        break;
      }
    }
    if(free_inode == 99){
      log_msg("sfs_create LINE %d: *ERROR: NO FREE INODES, CANNOT CREATE ANY MORE FILES IN DIRECTORY <should never reach this case>",__LINE__);
      return retstat;
    }
  }
  else{
    log_msg("sfs_create LINE %d: ERROR: NO FREE INODES, CANNOT CREATE ANY MORE FILES IN DIRECTORY",__LINE__);
    return retstat;
  }

  //time to go through data char map to find next free data block
  int data_block = -1;
  int data_index = -1;
  int datablock_num = 0;
  int done = 0;
  char data_buf[512];
  if(sb_buf->num_datablocks > 0){
    //log_msg("sfs_create LINE %d: num free datablocks: %d\n",__LINE__, sb_buf->num_datablocks);
    for(data_block = 1; data_block <= 3; data_block++){
      block_read(data_block, data_buf);
      char *data_map = (char *)data_buf;
      for(data_index = 0; data_index < 367; data_index++){
        if(data_map[data_index] == 0){
          //WE HAVE A FREE DATA BLOCK;
          log_msg("sfs_create LINE %d: FREE DATABLOCK: %d, %d\n",__LINE__, data_block, data_index);
          sb_buf->num_datablocks--;
          data_map[data_index] = 1;
          block_write(data_block, data_buf);
          done = 1;
          break;
        }
        datablock_num++;
      }
      if(done == 1){
        break;
      } else if(data_block == 3 && data_index == 366){
        //NO FREE DATA BLOCK
        log_msg("sfs_create LINE %d: *ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n",__LINE__);
      }
    }
  }
  else{
    log_msg("sfs_create LINE %d: *ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n",__LINE__);
  }

  log_msg("sfs_create LINE %d: DATABLOCK_NUM: %d\n",__LINE__, datablock_num);

  //find and alter inode struct
  int inode_block_num = 4 + free_inode/5;
  int inode_block_index = free_inode%5;
  log_msg("sfs_create LINE %d: INODE USED AT block %d index %d\n",__LINE__, inode_block_num, inode_block_index);
  log_msg("sfs_create LINE %d: pointer to datablock %d\n",__LINE__, datablock_num);
  char inode_buf[512];
  block_read(inode_block_num, inode_buf);
  inode_array *inode_block = (inode_array*) inode_buf;
  for(i = 0; i<=11; i++){
    if(inode_block->i[inode_block_index].db[i] < 0){
      inode_block->i[inode_block_index].db[i] = datablock_num;
      inode_block->i[inode_block_index].mode = (int) mode;
      log_msg("sfs_create LINE %d: WE HAVE A FREE DATABLOCK!!! %d pointing to %d\n",__LINE__, i, inode_block->i[inode_block_index].db[i]);
      block_write(inode_block_num, inode_buf);
      break;
    }
  }

  //find and alter direntries struct
  int direntry_block_num = 24 + free_inode/4;
  int direntry_block_index = free_inode%4;
  log_msg("sfs_create LINE %d: DIRENTRY USED AT block %d index %d\n",__LINE__, direntry_block_num, direntry_block_index);
  char direntry_buf[512];
  block_read(direntry_block_num, direntry_buf);
  direntry_array *direntry_block = (direntry_array *)direntry_buf;
  strncpy(direntry_block->d[direntry_block_index].name, path, 120);
  direntry_block->d[direntry_block_index].inode_num = free_inode;
  block_write(direntry_block_num, direntry_buf);
  log_msg("sfs_create LINE %d: Do we get here??",__LINE__);
  block_write(0, sb_buf);
  mode = S_IFREG | 0777;
  return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
  log_msg("\nsfs_unlink(path=\"%s\")\n", path);

  int retstat = 0;
  char buf[512];
  int i,j, found;


  int array[3];
  int * array_ptr;
  array_ptr = find_direntry(path, array);
  found = array_ptr[0];
  i =   array_ptr[1];
  j =   array_ptr[2];

  block_read(i, buf);
  direntry_array *direntries = (direntry_array *)buf;

  log_msg("i: %d j: %d\n", i , j);  
  
  if(found != -1) 
  {
    log_msg("sfs_unlink LINE %d: DELETING file %s == %s\n",__LINE__, path, direntries->d[j].name);

    // remove file!
    direntry dirent = direntries->d[j];
    memset(direntries->d[j].name, '\0', 120);

    //change superblock
    char sb_buf[512];
    block_read(0, sb_buf);
    superblock *sb = (superblock *)sb_buf;
    sb->num_inodes++;
    int inode_map_num = (i-24)*4 + j;
    sb->inode_map[inode_map_num] = 0;
    log_msg("sfs_unlink LINE %d: CHANGED inode map at index: %d\n",__LINE__, inode_map_num);

    //change inode
    char inode_buf[512];
    int inode_block = dirent.inode_num/5+4;
    int inode_block_index = dirent.inode_num%5;
    block_read(inode_block, inode_buf);
    log_msg("inode found at block %d index %d\n", inode_block, inode_block_index);
    inode_array *inode_arr = (inode_array *)inode_buf;
    inode curr_inode = inode_arr->i[inode_block_index];

    char datablock_buf[512];
    int x;
    for(x = 0; x < 11; x++)
    {
      if (curr_inode.db[x] >= 0) {
        log_msg("sfs_unlink LINE %d: inode datablock value at index %d is %d\n",__LINE__, x, curr_inode.db[x]);

        // increment available datablocks
        sb->num_datablocks++;
        // clean datablock
        log_msg("sfs_unlink LINE %d: cleaned datablock at block %d\n",__LINE__, curr_inode.db[x]+49);
        block_read(curr_inode.db[x]+49, datablock_buf);
        memset(datablock_buf, 0, 512);
        block_write(curr_inode.db[x], datablock_buf);

        // change data map bit
        int data_map_block = curr_inode.db[x]/366+1;
        int data_map_index = curr_inode.db[x]%366;
        log_msg("sfs_unlink LINE %d: data map bit changed at block %d index %d\n",__LINE__, data_map_block, data_map_index);
        char data_map_buf[512];
        block_read(data_map_block, data_map_buf);
        char *data_map = (char *)data_map_buf;
        data_map[data_map_index] = 0;
        block_write(data_map_block, data_map_buf);

        inode_arr->i[inode_block_index].db[x] = -1;
      }

    }

    block_write(inode_block, inode_buf);
    block_write(i, buf);
    block_read(i, buf);
    direntries = (direntry_array *)buf;
    log_msg("sfs_unlink LINE %d: Name of file is now: %s (SHOULD BE NOTHING)\n",__LINE__, direntries->d[j].name);
    block_write(0,sb_buf);
    //change data map
  }

  log_msg("i: %d j: %d\n", i , j);
  if(found == -1)
  {
    log_msg("sfs_unlink LINE %d: ERROR: CANNOT DELETE FILE, FILE NOT FOUND.\n",__LINE__);
  }
  free(array_ptr);
  return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
  log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n", path, fi);

  int retstat = 0;
  int fd;
//  (void) fi;
  //finding direntry for file 
  log_msg("sfs_open LINE %d: entering find_direntry with path %s\n",__LINE__, path);
  int array[3];
  int * array_ptr;
  int inode_num = (find_direntry( path, array ))[0];
  log_msg("sfs_open LINE %d: leaving find_direntry with inode_num %d\n",__LINE__,inode_num );

  if( inode_num == -1 )
  {
    log_msg("sfs_open LINE %d FLAGS: %d\n",__LINE__, fi->flags);
    if( (int)fi->flags == 34817 || (int)fi->flags == 33793 ) {
      log_msg("sfs_open LINE %d: reating file...\n",__LINE__);
      sfs_create(path, 0, fi);
    } else {
      log_msg("sfs_open LINE %d ERROR: CANNOT CREATE FILE\n",__LINE__);
      free(array_ptr);
      return retstat; //should this return something that isn't zero?
    }
  }

  fd = open(path, fi->flags);
  log_msg("sfs_open LINE %d: Did open() work?\n",__LINE__);
  fd = 1;//This seems like  a hack to keep things working, should probably fix!

  if( fd < 0 ) 
  {
    retstat = sfs_error("sfs_open open");
  }

  fi->fh = fd;
  log_fi(fi);
  free(array_ptr);
  return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
  int retstat = 0;
  log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n", path, fi);

  //log_fi(fi);
  fi->fh = 0;
  //log_msg("This is the fi after setting fi->fh to 0\n");
  //log_fi(fi);
  return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n", path, buf, size, offset, fi);
  (void) fi;
  int retstat = 0;
  
  int offset_copy = offset;
  //log_msg("sfs_read LINE %d: BUF BEFORE: %s\n",__LINE__, buf);
  memset( buf, '\0' , sizeof(buf) );
  //log_msg("sfs_read LINE %d: BUF AFTER: %s\n",__LINE__, buf);
  //log_fi(fi);

  //finding direntry for file 
  log_msg("sfs_write LINE %d: entering find_direntry with path %s\n",__LINE__, path);
  int array[3];
  int * array_ptr;
  int inode_num = (find_direntry( path, array ))[0];
  log_msg("sfs_write LINE %d: leaving find_direntry with inode_num %d\n",__LINE__,inode_num );

  if(inode_num == -1 )
  {
    log_msg("sfs_read LINE: READ ERROR: file to read from not found\n",__LINE__);
    free(array_ptr);
    return -1;//maybe -1?
  }


  log_msg("sfs_read LINE %d: READING from file now:\n",__LINE__);
  int inode_block = inode_num/5 + 4;
  int inode_block_index = inode_num%5;
  log_msg("sfs_read LINE %d: Inode block num: %d, inode index num: %d\n",__LINE__, inode_block, inode_block_index);
  char inode_buf[512];
  memset(inode_buf, '\0', 512);
  block_read(inode_block, inode_buf);
  inode_array *inode_arr = (inode_array *)inode_buf;

  //CHANGE
  int x=1;
  int first_db_block = offset/512;
  log_msg("sfs_read LINE %d: first_db_block: %d\n",__LINE__, first_db_block);
  int last_db_block = (offset+size)/512;
  log_msg("sfs_read LINE %d: last_db_block: %d\n",__LINE__,  last_db_block);

  if ((offset+size)%512 > 0){
    last_db_block++;
  }

  log_msg("sfs_read LINE %d: 0last_db_block: %d\n",__LINE__, last_db_block);
  int bytes_read = 0;
  char db_buf[512];
  memset(db_buf, '\0', 512);
  log_msg("sfs_read LINE %d: inside buf: %s\n",__LINE__, buf);
  for (x = first_db_block; x<last_db_block; x++)
   {
    log_msg("sfs_read LINE %d: x: %d\n",__LINE__, x);
    if (inode_arr->i[inode_block_index].db[x] < 0)
    {
      log_msg("sfs_read LINE %d: <0bytes_read %d\n",__LINE__, bytes_read);
      break;
      //return bytes_read;
    }else 
    {
      block_read(inode_arr->i[inode_block_index].db[x] + 49, db_buf);
      log_msg("sfs_read LINE %d: READING from i = %d: %s\n",__LINE__, inode_arr->i[inode_block_index].db[x] + 49, db_buf);
      //log_msg("INSIDE index %d is block %d: %s\n", x,inode_arr->i[inode_block_index].db[x] + 49, db_buf);

      if(x==first_db_block){
        log_msg("sfs_read LINE %d: I am in the first block x: %d\n",__LINE__, x);
        log_msg("LENGTH: %d\n", (int)strlen(db_buf));
        strncpy(buf, db_buf+(offset%512), 512-(offset%512));//assuming write null terminates properly
        bytes_read += (int)strlen(db_buf);
        log_msg("offset: %d bytes read: %d, int bytes: %d\n", (offset%512), strlen(db_buf), bytes_read);
        log_msg("sfs_read LINE %d: bytes_read %d\n",__LINE__, bytes_read);
      } else if(x==last_db_block-1) {
        log_msg("sfs_read LINE %d: I am in the last block x: %d\n",__LINE__, x);
        strncpy(buf+bytes_read, db_buf, size-bytes_read);
        bytes_read += size-bytes_read;
        log_msg("sfs_read LINE %d: bytes_read %d\n",__LINE__, bytes_read);
      } else {
        log_msg("sfs_read LINE %d: I am in the middle block x: %d\n",__LINE__, x);
        strncpy(buf+bytes_read, db_buf, 512);
        bytes_read += 512;
        log_msg("sfs_read LINE %d: bytes_read %d\n",__LINE__, bytes_read);
      }
    }
   }

  log_msg("sfs_read LINE %d: bytes_read before strcat buf with null term %d\n",__LINE__, bytes_read);
  if(bytes_read==0){
    return 0;
  }
  strcat(buf+bytes_read, "\n\0");
  log_msg("sfs_read LINE %d: BUF AFTER: %s, length: %d",__LINE__, buf, sizeof(buf));
  free(array_ptr);
  
  return bytes_read+1;
}


/** 
 * Write data to an open file
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
  int retstat = 0;
  log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n", path, buf, size, offset, fi);

  //finding direntry for file

  log_msg("sfs_write LINE %d: entering find_direntry with path %s\n",__LINE__, path);
  int array[3];
  int * array_ptr;
  int inode_num = (find_direntry( path, array ))[0];
  log_msg("sfs_write LINE %d: leaving find_direntry with inode_num %d\n",__LINE__,inode_num );

  if(inode_num == -1)
  {
    log_msg("sfs_write LINE %d: ERROR: file to write to not found\n",__LINE__);
    sfs_create(path, 0, fi);
    // for testing (reads)
    //log_msg("sfs_write LINE %d: testing reads, going into find_direntry with path: %s",__LINE__, path);

    /*
       DOES ALL THIS DO IS TEST??? SHOULD WE REFACTOR IT OR CAN WE SAFELY REMOVE IT DOWN THE LINE??
       char dir_buf[512];
       int i, j;
       for(i = 24; i <= 48; i++){
       block_read(i, dir_buf);
       direntry_array *direntries = (direntry_array *)dir_buf;
       for(j = 0; j < 4; j++){
       if(strcmp(direntries->d[j].name, path) == 0){
       log_msg("sfs_write LINE %d: FOUND file to write to\n", __LINE__);
       inode_num = direntries->d[j].inode_num;
       j = -1;
       break;
       } 
       }
       block_write(i, dir_buf);
       if(j==-1){
       break;
       }
       }
    // end test
    */
  }

  //testing
  char sb_buf[512];
  block_read(0, sb_buf);
  superblock *sb = (superblock *)sb_buf;
  log_msg("sfs_write LINE %d: NUM INODES REM before: %d\n",__LINE__, sb->num_inodes);
  block_write(0, sb);
  // end test

  int inode_block = inode_num/5 + 4;
  int inode_block_index = inode_num%5;
  char inode_buf[512];
  block_read(inode_block, inode_buf);
  inode_array *inode_arr = (inode_array *)inode_buf;

  int x;
  int first_db_block = offset/512;
  int last_db_block = (offset+size)/512;
  if ((offset+size)%512 > 0){
    last_db_block++;
  }
  if (last_db_block > 11){
    last_db_block = 11;
  }
  int bytes_written = 0;
  char db_buf[512];
  char db_buf_cp[512];
  //log_msg("inside buf: %s\n", buf);

  log_msg("sfs_write LINE %d: size: %d, offset: %d, first: %d, last: %d\n",__LINE__, size, offset, first_db_block, last_db_block);

  for (x = first_db_block; x<last_db_block; x++){
    if (inode_arr->i[inode_block_index].db[x] < 0)
    {
      int data_block = -1;
      int data_index = -1;
      int datablock_num = 0;
      int done = 0;
      char data_buf[512];

      if(sb->num_datablocks > 0)
      {
        //log_msg("sfs_write LINE %d: num free datablocks: %d\n",__LINE__, sb_buf->num_datablocks);
        for(data_block = 1; data_block <= 3; data_block++)
        {
          block_read(data_block, data_buf);
          char *data_map = (char *)data_buf;
          for(data_index = 0; data_index < 367; data_index++)
          {
            if(data_map[data_index] == 0)
            {
              //WE HAVE A FREE DATA BLOCK;
              log_msg("sfs_write LINE %d: FREE DATABLOCK: %d, %d\n",__LINE__, data_block, data_index);
              //clean and reset data block (just in case)
              char set_block[512];
              block_read(datablock_num, set_block);
              memset(set_block, '\0', 512);
              block_write(datablock_num, set_block);

              sb->num_datablocks--;
              data_map[data_index] = 1;
              block_write(data_block, data_buf);
              done = 1;
              break;
            }

            datablock_num++;
            log_msg("sfs_write LINE %d: db = %d is now %d\n",__LINE__, inode_arr->i[inode_block_index].db[x], datablock_num);
            inode_arr->i[inode_block_index].db[x] = datablock_num;
            //log_msg("sfs_write LINE %d: getting datablock num: %d\n",__LINE__, inode_arr->i[inode_block_index].db[x]);
          }

          if (done == 1) { 
            break;
          } else if(data_block == 3 && data_index == 366) {
            //NO FREE DATA BLOCK
            block_write(data_block, data_buf);
            log_msg("sfs_write LINE %d: *ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n",__LINE__);
          }
        }
      }
      else{
        block_write(data_block, data_buf);
        log_msg("sfs_write LINE %d: *ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n",__LINE__);
      }
    }

    memset(db_buf, '\0', 512);
    //log_msg("sfs_write LINE %d: db: %d\n",__LINE__, inode_arr->i[inode_block_index].db[x]);
    block_read(inode_arr->i[inode_block_index].db[x] + 49, db_buf);
    memset(db_buf_cp, '\0', 512);
    //log_msg("sfs_write LINE %d: before write: READING from i = %d: %s\n\n\n",__LINE__, x, db_buf);
    if(x==first_db_block)
    {
      strncpy(db_buf_cp, db_buf, (offset%512));

      if(size > (512 - offset%512)) {
        strncpy(db_buf_cp, buf, (512 - offset%512));
        bytes_written += 512 - offset%512;
        inode_arr->i[inode_block_index].size_written = inode_arr ->i[inode_block_index].size_written + bytes_written;

      } else if(size > 0) {
        log_msg("sfs_write LINE %d: size: %d\n",__LINE__, size);
        strncpy(db_buf_cp+(offset%512), buf, size);
        strncpy(db_buf_cp+size+(offset%512)-1, db_buf+size-1, 512-size-offset%512+2);
        //log_msg("sfs_write LINE %d: db_buf+size: %s\n",__LINE__, db_buf+size);
        bytes_written += size;
        inode_arr->i[inode_block_index].size_written = inode_arr ->i[inode_block_index].size_written + bytes_written;
      }
    } else if(x==last_db_block-1) 
    {
      if((offset+size)%512 < 512)
      {
        //log_msg("sfs_write LINE %d: here\n",__LINE__);
        if(size == 4096) {
          strncpy(db_buf_cp, db_buf, 512);
          strncpy(db_buf_cp, buf+bytes_written, (offset+size)%512-1);
        } else {
          strncpy(db_buf_cp, buf+bytes_written, (offset+size)%512-1);
          bytes_written += (offset+size)%512;
          strncpy(db_buf_cp+(offset+size)%512-1, db_buf+(offset+size)%512-1, 512-(size+offset)%512-1);
          inode_arr->i[inode_block_index].size_written = inode_arr ->i[inode_block_index].size_written + bytes_written;
        }
      } else 
      {
        strncpy(db_buf_cp, buf+bytes_written, (offset+size)%512);
        bytes_written += (offset+size)%512;
        strncpy(db_buf_cp+((offset+size)%512), buf+bytes_written, 512 - (offset+size)%512);
        inode_arr->i[inode_block_index].size_written = inode_arr ->i[inode_block_index].size_written + bytes_written;

        //log_msg("sfs_write LINE %d: last written: %d\n",__LINE__, bytes_written);

      }
    } else 
    {
      //log_msg("sfs_write LINE %d: putting in datablock %d: %s\n",__LINE__, x, buf+bytes_written);
      strncpy(db_buf_cp, buf+bytes_written, 512);
      bytes_written += 512;
      inode_arr->i[inode_block_index].size_written = inode_arr ->i[inode_block_index].size_written + bytes_written;

    }
    block_write(inode_arr->i[inode_block_index].db[x] + 49, db_buf_cp);
  }
  block_write(inode_block, inode_buf);
  //log_msg("sfs_write LINE %d: NUM INODES REM after: %d\n",__LINE__, sb->num_inodes);

  //testing (reads)
  int y;
  for(y = 0; y<11; y++){
    if(inode_arr->i[inode_block_index].db[y] >= 0){
      block_read(inode_arr->i[inode_block_index].db[y] + 49, db_buf);
      log_msg("sfs_write LINE %d: READING from i = %d, db = %d: %s\n",__LINE__, y, inode_arr->i[inode_block_index].db[y] + 49, db_buf);
      block_write(inode_arr->i[inode_block_index].db[y] + 49, db_buf);
    }
    else break;

  }
  // end test
  free(array_ptr);
  return bytes_written;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
  int retstat = 0;
  log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n", path, mode);

  return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
  int retstat = 0;
  log_msg("sfs_rmdir(path=\"%s\")\n", path);

  return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
  int retstat = 0;
  log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);

  return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  int retstat = 0;
  char buff[512];
  int i, j, h;

  log_msg("\nsfs_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n", path, buf, filler, offset, fi);
  memset(buff, 0, sizeof(char)*512);
  filler( buf, ".\0",  NULL, 0 );
  filler( buf, "..\0", NULL, 0 );

  //iterating through the blocks
  for(j = 24; j < 49; j++)
  {
    block_read(j, buff);
    //iterating through direntry_array within block j (j between 24 - 48 inclusive)
    direntry_array *pde = (direntry_array *)buff;
    for(i = 0; i < 4; i++)
    {
      //log_msg("sfs_readdir LINE %d: direntry contents: name=%s, inode_num=%d\n",__LINE__, pde->d[i].name, pde->d[i].inode_num);
      if( pde->d[i].name[0] == '\0' )
      {
        continue;
      }
      char *pChar = malloc(sizeof(char)*120);
      memset(pChar, '\0', sizeof(char)*120);
      strncpy(pChar, pde->d[i].name+1, 10);
      filler(buf, pChar, NULL, 0);
    }
  }
  return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
  int retstat = 0;


  return retstat;
}

struct fuse_operations sfs_oper = {
  .init = sfs_init,
  .destroy = sfs_destroy,

  .getattr = sfs_getattr,
  .create = sfs_create,
  .unlink = sfs_unlink,
  .open = sfs_open,
  .release = sfs_release,
  .read = sfs_read,
  .write = sfs_write,

  .rmdir = sfs_rmdir,
  .mkdir = sfs_mkdir,

  .opendir = sfs_opendir,
  .readdir = sfs_readdir,
  .releasedir = sfs_releasedir
};

void sfs_usage()
{
  fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
  abort();
}

int main(int argc, char *argv[])
{
  int fuse_stat;
  struct sfs_state *sfs_data;

  // sanity checking on the command line
  if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
    sfs_usage();

  sfs_data = malloc(sizeof(struct sfs_state));
  if (sfs_data == NULL) {
    perror("main calloc");
    abort();
  }

  // Pull the diskfile and save it in internal data
  sfs_data->diskfile = argv[argc-2];
  argv[argc-2] = argv[argc-1];
  argv[argc-1] = NULL;
  argc--;

  sfs_data->logfile = log_open();

  // turn over control to fuse
  fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
  fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
  fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

  return fuse_stat;
}

