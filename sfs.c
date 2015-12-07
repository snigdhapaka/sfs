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
  int size;//size of file in bytes
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
    
    log_conn(conn);
    log_fuse_context(fuse_get_context());
    log_msg("about to open disk (testfsfile)\n");
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

    // block_write(0, &sb);
    // block_read(0, buf);
    // superblock *psb = (superblock *)buf;

    //filling in the char map (instead of bit map) for inode and data blocks below 
    int i;
    for(i = 0; i < 100; i++){
      sb.inode_map[i] = 0;
    }
    block_write(0, &sb);
  
    // CREATING AND FILLING IN THE DATA CHAR MAPS, BLOCKS 1-3, (3 total)
    char data_map[267];
    memset(data_map, 0, 267);
    for(i = 1; i <= 3; i++){
      // set this to something unusable (not 0 or 1), since there should only be 1100 data blocks, not 1101
      if(i == 3){
        data_map[266] = 'a';
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
          x.i[ii].size = 0;
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
   
  log_msg("end of init function\n");
    
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
    disk_close();
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}

static void sfs_fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, SFS_DATA->diskfile);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
            // break here

    log_msg("    sfs_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n", 
    SFS_DATA->diskfile, path, fpath);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
    path, statbuf);
  

    retstat = lstat(path, statbuf);
    if (retstat != 0)
    retstat = sfs_error("sfs_getattr lstat");
  statbuf->st_mode = S_IFDIR | 0777;
    log_stat(statbuf);
    
    return retstat;
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
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    log_msg("SNIGGY SAYS THIS IS THE PATH: %s", path);
    log_fi(fi);
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
        path, mode, fi);

    // CHECK if file name already exists
    char buf[512];
    /* 
    //test code
    block_read(0, buf);
    superblock *sb = (superblock *)buf;
    char *inode_map_t = sb->inode_map;
    sb->num_inodes--;
    inode_map_t[0] = 1;
    block_write(0, buf);

    block_read(24, buf);
    direntry_array *direntries = (direntry_array *)buf;
    strncpy(direntries->d[0].name, "/test", 120);
    block_write(24, buf);
*/
    //THIS IS HOW YOU GO THROUGH THE DIRENTRIES
    //block_read(24, buf); // you will need to go through blocks 24-48
    //direntries = (direntry_array *)buf; // direntries is a direntry_array (initialized above)

    int i,j;
    for(i = 24; i <= 48; i++){
      block_read(i, buf);
      direntry_array *direntries = (direntry_array *)buf;
      for(j = 0; j < 4; j++){
        if(strcmp(direntries->d[j].name, path) == 0){
          log_msg("ERROR: Cannot create file with same name as an existing file.\n");
          return retstat;
        } 
      }
    }

    // END TRAVERSING DIRENTRIES
    
    log_msg("now creating file\n");

    //time to go through inode char map to find next free direntry/inode
    char sb_b[512];
    block_read(0, sb_b);
    superblock *sb_buf = (superblock *)sb_b;
    char *inode_map = sb_buf->inode_map;
    int free_inode = -1;
    if(sb_buf->num_inodes > 0){
      //log_msg("num free inodes: %d\n", sb_buf->num_inodes);
      for(free_inode = 0; free_inode < 100; free_inode++){
        //log_msg("free_inode num: %d, val: %d\n", free_inode, inode_map[free_inode]);
        if(inode_map[free_inode] == 0){
          //WE HAVE A FREE INODE
          log_msg("FREE INODE: %d\n", free_inode);
          sb_buf->num_inodes--;
          inode_map[free_inode] = 1;
          break;
        }
      }
      if(free_inode == 99){
        log_msg("*ERROR: NO FREE INODES, CANNOT CREATE ANY MORE FILES IN DIRECTORY <should never reach this case>");
        return retstat;
      }
    }
    else{
      log_msg("ERROR: NO FREE INODES, CANNOT CREATE ANY MORE FILES IN DIRECTORY");
      return retstat;
    }

    //time to go through data char map to find next free data block
    int data_block = -1;
    int data_index = -1;
    int datablock_num = 0;
    int done = 0;
    char data_buf[512];
    if(sb_buf->num_datablocks > 0){
      //log_msg("num free datablocks: %d\n", sb_buf->num_datablocks);
      for(data_block = 1; data_block <= 3; data_block++){
        block_read(data_block, data_buf);
        char *data_map = (char *)data_buf;
        for(data_index = 0; data_index < 267; data_index++){
          if(data_map[data_index] == 0){
            //WE HAVE A FREE DATA BLOCK;
            log_msg("FREE DATABLOCK: %d, %d\n", data_block, data_index);
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
        } else if(data_block == 3 && data_index == 266){
          //NO FREE DATA BLOCK
          log_msg("*ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n");
        }
      }
    }
    else{
      log_msg("*ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n");
    }

    log_msg("DATABLOCK_NUM: %d\n", datablock_num);

    //find and alter inode struct
    int inode_block_num = 4 + free_inode/5;
    int inode_block_index = free_inode%5;
    log_msg("INODE USED AT block %d index %d\n", inode_block_num, inode_block_index);
    log_msg("pointer to datablock %d\n", datablock_num);
    char inode_buf[512];
    block_read(inode_block_num, inode_buf);
    inode_array *inode_block = (inode_array*) inode_buf;
    for(i = 0; i<=11; i++){
      if(inode_block->i[inode_block_index].db[i] < 0){
        inode_block->i[inode_block_index].db[i] = datablock_num;
        inode_block->i[inode_block_index].mode = (int) mode;
        log_msg("WE HAVE A FREE DATABLOCK!!! %d pointing to %d\n", i, inode_block->i[inode_block_index].db[i]);
        block_write(inode_block_num, inode_buf);
        break;
      }
    }


    //find and alter direntries struct
    int direntry_block_num = 24 + free_inode/4;
    int direntry_block_index = free_inode%4;
    log_msg("DIRENTRY USED AT block %d index %d\n", direntry_block_num, direntry_block_index);
    char direntry_buf[512];
    block_read(direntry_block_num, direntry_buf);
    direntry_array *direntry_block = (direntry_array *)direntry_buf;
    strncpy(direntry_block->d[direntry_block_index].name, path, 120);
    direntry_block->d[direntry_block_index].inode_num = free_inode;
    block_write(direntry_block_num, direntry_buf);


    //CHECKING TO SEE IF IT WORKED


    /*
    superblock *ptr = (superblock *)buf;
    if(ptr->num_inodes > 0 && ptr->num_datablocks > 0){
      int i, j;
      for(i = 0; i < sizeof(ptr->inode_map); i++){
        if(ptr->inode_map[i] == 0){
          for(j = 0; j < sizeof(ptr->data_map); j++){
            if(ptr->data_map[j] == 0){
              memset(buf, 0, 512);
              block_read(1, buf);
              inodes *inodesptr = (inodes *)buf;
            }
          }
        }
      }
    }
  */
    block_write(0, sb_buf);
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

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
    int retstat = 0;
    int fd;
    char fpath[120];

    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
      path, fi);

    sfs_fullpath(fpath, path);

    fd = open(fpath, fi->flags);

    if( fd < 0 ) {
      retstat = sfs_error("sfs_open open");
    }

    fi->fh =fd;
    log_fi(fi);
    
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
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
    path, fi);
    

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
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
      path, buf, size, offset, fi);

   
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
       struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
      path, buf, size, offset, fi);
    
    
    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
      path, mode);
   
    
    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
      path);
    
    
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

    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
    path, fi);
    
    
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
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
         struct fuse_file_info *fi)
{
    
    int retstat = 0;

    log_msg("\nsfs_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
      path, buf, filler, offset, fi);

    char buff[512];
    memset(buff, 0, sizeof(char)*512);
    //block_read(6, &buff);
    //char readbuff[124];
    int i, j, h;
    //direntry_array *pde;

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
        //changes contents of readbuff to reflect each direntry
        //memcpy((char *)&readbuff, (char*)&buff +(i*124), sizeof(char)*124);
        //checks to see if readbuff contains anything, if not break out of loop
        
        
        //pde = (direntry_array *)readbuff;
        log_msg("direntry contents: name=%s, inode_num=%d\n", pde->d[i].name, pde->d[i].inode_num);
        //print to terminal file names
        
        
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
