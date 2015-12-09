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
  int size_remaining;//number of bytes of remaining space in file
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
          x.i[ii].size_remaining = 512*11;
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
   
    //log_msg("end of init function\n");
    //start of: hard coding a file in below to test read
    //int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
    //struct fuse_file_info *fie;
    //int testing = sfs_create("/try.c", 0 , fie);

/*
    char sbuf[512];
    char *longstring = "1234567890";
    strncpy(sbuf, longstring, sizeof(sbuf)-1);
    sbuf[10]='\0';
    sbuf[511]='\0';
    log_msg("LOOK: This string was written to block:\n");
    log_msg("%s\n", longstring);
    log_msg("SBUF before: %s\n", sbuf);
    block_write(49, sbuf);
    memset(sbuf, '\0', sizeof(sbuf));
    log_msg("SBUF middle: %s\n", sbuf);
    block_read(49, sbuf);
    log_msg("SBUF after HERE: %s\n", sbuf);
    //end of: hard coding a file in below to test read
*/

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
    //log_msg("about to close disk\n");  
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
  
  //sfs_fullpath(fpath, path);
  memset(statbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0) {
    //log_msg("I was here as a directory");
    statbuf->st_mode = S_IFDIR | 0777;
    statbuf->st_nlink = 2;
    statbuf->st_mtime = time(NULL);
    statbuf->st_ctime = time(NULL);
    log_stat(statbuf);
    //log_msg("I am leaving here as a directory");
    return retstat;
  } /*if (strcmp(path, hello_path) == 0)*/ 
      char buff[512];
      int i, j;
      for(j = 24; j < 49; j++) {
        block_read(j, buff);
        //iterating through direntry_array within block j (j between 24 - 48 inclusive)
        direntry_array *pde = (direntry_array *)buff;
        for(i = 0; i < 4; i++) {
          //log_msg("GET ATTR: direntry contents: name=%s, inode_num=%d\n", pde->d[i].name, pde->d[i].inode_num);
          //if( pde->d[i].name[0] == '\0' ){  
            //continue;
          //}
          if(strcmp(path, pde->d[i].name)){
            log_msg("GET ATTR: direntry contents: name=%s, inode_num=%d\n", pde->d[i].name, pde->d[i].inode_num);
            log_msg("I was here as a FILE");
            statbuf->st_mode = S_IFREG | 0777;
            statbuf->st_nlink = 1;
            statbuf->st_size = 0;
            statbuf->st_mtime = time(NULL);
            statbuf->st_ctime = time(NULL);
            log_msg("\nI am a file called=>  path=\"%s\")\n", path);
            log_stat(statbuf);
            return retstat;
          }  
        }
      } 
  
    retstat = -ENOENT;

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
    //log_fi(fi);
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
        path, mode, fi);

    // CHECK if file name already exists
    char buf[512];
    //THIS IS HOW YOU GO THROUGH THE DIRENTRIES
    //block_read(24, buf); // you will need to go through blocks 24-48
    //direntries = (direntry_array *)buf; // direntries is a direntry_array (initialized above)

    int i,j;

    // END TRAVERSING DIRENTRIES
    
    log_msg("now creating file\n");

    //time to go through inode char map to find next free direntry/inode
    char sb_b[512];
    block_read(0, sb_b);
    superblock *sb_buf = (superblock *)sb_b;
    char *inode_map = sb_buf->inode_map;
    int free_inode = -1;
    log_msg("num free inodes: %d\n", sb_buf->num_inodes);
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
        for(data_index = 0; data_index < 367; data_index++){
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
        } else if(data_block == 3 && data_index == 366){
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
    log_msg("Do we get here??");
    block_write(0, sb_buf);
    mode = S_IFREG | 0777;
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    char buf[512];
    int i,j, found;
    for(i = 24; i <= 48; i++){
      block_read(i, buf);
      direntry_array *direntries = (direntry_array *)buf;
      for(j = 0; j < 4; j++){
        if(strcmp(direntries->d[j].name, path) == 0){
          log_msg("DELETING file %s == %s\n", path, direntries->d[j].name);

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
          log_msg("CHANGED inode map at index: %d\n", inode_map_num);

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
          for(x = 0; x < 11; x++){
            if (curr_inode.db[x] >= 0){
              log_msg("inode datablock value at index %d is %d\n", x, curr_inode.db[x]);
              
              // increment available datablocks
              sb->num_datablocks++;
              // clean datablock
              log_msg("cleaned datablock at block %d\n", curr_inode.db[x]+49);
              block_read(curr_inode.db[x]+49, datablock_buf);
              memset(datablock_buf, 0, 512);
              block_write(curr_inode.db[x], datablock_buf);

              // change data map bit
              int data_map_block = curr_inode.db[x]/366+1;
              int data_map_index = curr_inode.db[x]%366;
              log_msg("data map bit changed at block %d index %d\n", data_map_block, data_map_index);
              char data_map_buf[512];
              block_read(data_map_block, data_map_buf);
              char *data_map = (char *)data_map_buf;
              data_map[data_map_index] = 0;
              block_write(data_map_block, data_map_buf);

              curr_inode.db[x] = -1;
            }

          }
          block_write(inode_block, inode_buf);
          block_write(i, buf);
            block_read(i, buf);
            direntries = (direntry_array *)buf;
            log_msg("Name of file is now: %s (SHOULD BE NOTHING)\n", direntries->d[j].name);
          block_write(0,sb_buf);
          //change data map

          found = -1;
          break;
        } 
      }
      if(found ==-1){
        break;
      }
    }
    log_msg("i: %d j: %d\n", i , j);
    if(i==49 && j==4){
      log_msg("ERROR: CANNOT DELETE FILE, FILE NOT FOUND.\n");
    }


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

    char buf[512];
    int i,j;
    for(i = 24; i <= 48; i++){
      block_read(i, buf);
      direntry_array *direntries = (direntry_array *)buf;
      for(j = 0; j < 4; j++){
        if(strcmp(direntries->d[j].name, path) == 0){
          log_msg("ERROR: Cannot create file with same name as an existing file.\n");
          j = -1;
          break;
        } 
      }
      if(j==-1){
        break;
      }
    }
    log_msg("i: %d j: %d\n", i , j);
    if(i==49 && j==4){
      log_msg("FLAGS: %d\n", fi->flags);
      if( (int)fi->flags == 34817 || (int)fi->flags == 33793 ) {
        log_msg("creating file...\n");
        sfs_create(path, 0, fi);

      }
      else{
        log_msg("ERROR: CANNOT CREATE FILE\n");
        return retstat;//should this return something that isn't zero?
      }
    }

    fd = open(path, fi->flags);
    log_msg("Did open() work?\n");
    
    fd = 1;
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
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
      path, buf, size, offset, fi);
    log_msg("BUF BEFORE: %s\n", buf);
    memset( buf, '\0' , sizeof(buf) );
    log_msg("BUF AFTER: %s\n", buf);
    //log_fi(fi);


    //finding direntry for file
    char dir_buf[512];
    int i,j, inode_num;
    for(i = 24; i <= 48; i++){
      block_read(i, dir_buf);
      direntry_array *direntries = (direntry_array *)dir_buf;
      for(j = 0; j < 4; j++){
        if(strcmp(direntries->d[j].name, path) == 0){
          log_msg("FOUND file to read from\n");
          log_msg("i: %d j: %d\n", i , j);
          inode_num = direntries->d[j].inode_num;
          j = -1;
          break;
        } 
      }
      if(j==-1){
        break;
      }
    }
    //log_msg("i: %d j: %d\n", i , j);
    if(i==49 && j==4){
      log_msg("READ ERROR: file to read from not found\n");
      return -1;//maybe -1?
    }

    log_msg("READING from file now:\n");
    int inode_block = inode_num/5 + 4;
    int inode_block_index = inode_num%5;
    log_msg("Inode block num: %d, inode index num: %d\n", inode_block, inode_block_index);
    char inode_buf[512];
    memset(inode_buf, '\0', 512);
    block_read(inode_block, inode_buf);
    inode_array *inode_arr = (inode_array *)inode_buf;

    int x;
    int first_db_block = offset/512;
    log_msg("LOOK HERE: first_db_block: %d\n", first_db_block);
    int last_db_block = (offset+size)/512;
    log_msg("LOOK HERE: last_db_block: %d\n", last_db_block);

    if ((offset+size)%512 > 0){
      last_db_block++;
    }
    log_msg("LOOK HERE: 0last_db_block: %d\n", last_db_block);
    int bytes_read = 0;
    char db_buf[512];
    memset(db_buf, '\0', 512);
    log_msg("inside buf: %s\n", buf);
    for (x = first_db_block; x<last_db_block; x++){
      log_msg("LOOK HERE: x: %d\n", x);
      if (inode_arr->i[inode_block_index].db[x] < 0){
        log_msg("LOOK HERE: <0bytes_read %d\n", bytes_read);
        break;
        //return bytes_read;
      }else {
        block_read(inode_arr->i[inode_block_index].db[x] + 49, db_buf);
        log_msg("Snig: READING from i = %d: %s\n", inode_arr->i[inode_block_index].db[x] + 49, db_buf);
        if(x==first_db_block){
          log_msg("LOOK HERE: I am in the first block x: %d\n", x);
          strncpy(buf, db_buf+(offset%512), strlen(db_buf));//assuming write null terminates properly
          bytes_read += strlen(db_buf);
          log_msg("LOOK HERE: bytes_read %d\n", bytes_read);
        } else if(x==last_db_block){
          log_msg("LOOK HERE: I am in the last block x: %d\n", x);
          strncpy(buf+bytes_read, db_buf, size-bytes_read);
          bytes_read += size-bytes_read;
          log_msg("LOOK HERE: bytes_read %d\n", bytes_read);
        } else{
          log_msg("LOOK HERE: I am in the middle block x: %d\n", x);
          strncpy(buf+bytes_read, db_buf, 512);
          bytes_read += 512;
          log_msg("LOOK HERE: bytes_read %d\n", bytes_read);
        }
      }
    }
    log_msg("LOOK HERE: bytes_read before strcat buf with null term %d\n", bytes_read);
    strcat(buf+bytes_read, "\0");
    
    //const char src[11] = "0123456789\0";
    //memcpy(buf, src, strlen(src)+1);
    //pread(fi->fh, buf, size, offset);
    log_msg("BUF AFTER: %s, length: %d", buf, sizeof(buf));
    //bytes_read++;
    return bytes_read * sizeof(char);
    //return 17*sizeof(char);
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
    
    //finding direntry for file
    char dir_buf[512];
    int i,j, inode_num;
    for(i = 24; i <= 48; i++){
      block_read(i, dir_buf);
      direntry_array *direntries = (direntry_array *)dir_buf;
      for(j = 0; j < 4; j++){
        if(strcmp(direntries->d[j].name, path) == 0){
          log_msg("FOUND file to write to\n");
          inode_num = direntries->d[j].inode_num;
          j = -1;
          break;
        } 
      }
      if(j==-1){
        break;
      }
    }
    log_msg("i: %d j: %d\n", i , j);
    if(i==49 && j==4){
      log_msg("ERROR: file to write to not found\n");
      sfs_create(path, 0, fi);
    }

    //testing
    char sb_buf[512];
    block_read(0, sb_buf);
    superblock *sb = (superblock *)sb_buf;
    log_msg("NUM INODES REM before: %d\n",sb->num_inodes);

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
    log_msg("size: %d, offset: %d, first: %d, last: %d\n", size, offset, first_db_block, last_db_block);
    for (x = first_db_block; x<last_db_block; x++){
      if (inode_arr->i[inode_block_index].db[x] < 0){
        int data_block = -1;
        int data_index = -1;
        int datablock_num = 0;
        int done = 0;
        char data_buf[512];
        if(sb->num_datablocks > 0){
          //log_msg("num free datablocks: %d\n", sb_buf->num_datablocks);
          for(data_block = 1; data_block <= 3; data_block++){
            block_read(data_block, data_buf);
            char *data_map = (char *)data_buf;
            for(data_index = 0; data_index < 367; data_index++){
              if(data_map[data_index] == 0){
                //WE HAVE A FREE DATA BLOCK;
                log_msg("FREE DATABLOCK: %d, %d\n", data_block, data_index);
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
              inode_arr->i[inode_block_index].db[x] = datablock_num;
              //log_msg("getting datablock num: %d\n", inode_arr->i[inode_block_index].db[x]);
            }
            if(done == 1){
              break;
            } else if(data_block == 3 && data_index == 366){
              //NO FREE DATA BLOCK
              block_write(data_block, data_buf);
              log_msg("*ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n");
            }
          }
        }
        else{
          block_write(data_block, data_buf);
          log_msg("*ERROR: NO FREE DATA BLOCKS <should never reach this error case>\n");
        }
      }
      memset(db_buf, '\0', 512);
      //log_msg("db: %d\n", inode_arr->i[inode_block_index].db[x]);
      block_read(inode_arr->i[inode_block_index].db[x] + 49, db_buf);
      memset(db_buf_cp, '\0', 512);
      //log_msg("before write: READING from i = %d: %s\n\n\n", x, db_buf);
      if(x==first_db_block){
        strncpy(db_buf_cp, db_buf, (offset%512));
        if(size > (512 - offset%512)){
          strncpy(db_buf_cp+(offset%512), buf, (512 - offset%512));
          bytes_written += 512 - offset%512;
        } else if(size > 0){
          log_msg("size: %d\n", size);
          strncpy(db_buf_cp+(offset%512), buf, size);
          strncpy(db_buf_cp+size+(offset%512)-1, db_buf+size-1, 512-size-offset%512+2);
          //log_msg("db_buf+size: %s\n", db_buf+size);
          bytes_written += size;
        }
      } else if(x==last_db_block-1){
        if((offset+size)%512 < 512){
          //log_msg("here\n");
          if(size == 4096){
            strncpy(db_buf_cp, db_buf, 512);
            strncpy(db_buf_cp, buf+bytes_written, (offset+size)%512-1);
          }else{
            //log_msg("here, bytes_written: %d, rem offset: %d\n", bytes_written, (offset+size)%512-1);
            //log_msg("INSIDE db_buf: %s\n**************\n\n", db_buf);
            //log_msg("BEFORE db_buf: %s\n**************\n\n", db_buf_cp);
            //log_msg("COPYING: %s\n**************\n\n", buf+bytes_written);
            //strncpy(db_buf_cp, db_buf, 512);
            strncpy(db_buf_cp, buf+bytes_written, (offset+size)%512-1);
            //log_msg("why is it: %s\n", db_buf);
            //log_msg("curr db: %s\n", db_buf_cp);
            bytes_written += (offset+size)%512;
            //log_msg("adding: db_buf: %s\n**************\n\n\n", db_buf+(offset+size)%512-1);
            strncpy(db_buf_cp+(offset+size)%512-1, db_buf+(offset+size)%512-1, 512-(size+offset)%512-1);
          }
        }else {
          strncpy(db_buf_cp, buf+bytes_written, (offset+size)%512);
          bytes_written += (offset+size)%512;
          strncpy(db_buf_cp+((offset+size)%512), buf+bytes_written, 512 - (offset+size)%512);
          //log_msg("last written: %d\n", bytes_written);
        }
      } else{
        //log_msg("putting in datablock %d: %s\n", x, buf+bytes_written);
        strncpy(db_buf_cp, buf+bytes_written, 512);
        bytes_written += 512;
      }
      block_write(inode_arr->i[inode_block_index].db[x] + 49, db_buf_cp);
    
    }
    block_write(inode_block, inode_buf);
    //log_msg("NUM INODES REM after: %d\n",sb->num_inodes);
    //testing
    int y;
    for(y = 0; y<11; y++){
      if(inode_arr->i[inode_block_index].db[y] >= 0){
        block_read(inode_arr->i[inode_block_index].db[y] + 49, db_buf);
        log_msg("READING from i = %d, db = %d: %s\n", y, inode_arr->i[inode_block_index].db[y] + 49, db_buf);
        block_write(inode_arr->i[inode_block_index].db[y] + 49, db_buf);
      }
      else break;

    }

    //memset(buf, '\0', size+1);
    return bytes_written;
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
        //log_msg("direntry contents: name=%s, inode_num=%d\n", pde->d[i].name, pde->d[i].inode_num);
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
