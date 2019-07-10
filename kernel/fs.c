// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xk/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include <cdefs.h>
#include <defs.h>
#include <file.h>
#include <fs.h>
#include <mmu.h>
#include <param.h>
#include <proc.h>
#include <sleeplock.h>
#include <spinlock.h>
#include <stat.h>

#include <buf.h>

// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;

// Read the super block.
void readsb(int dev, struct superblock *sb) {
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// range of blocks holding the file's content.
//
// The inodes themselves are contained in a file known as the
// inodefile. This allows the number of inodes to grow dynamically
// appending to the end of the inode file. The inodefile has an
// inum of 1 and starts at sb.startinode.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->flags.
//
// Since there is no writing to the file system there is no need
// for the callers to worry about coherence between the disk
// and the in memory copy, although that will become important
// if writing to the disk is introduced.
//
// Clients use iload() to populate an inode with valid information
// from the disk. idup() can be used to add an in memory reference
// to and inode. iput() will decrement the in memory reference count
// and will free the inode if there are no more references to it,
// freeing up space in the cache for the inode to be used again.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
  struct inode inodefile;
} icache;

// Find the inode file on the disk and load it into memory
// should only be called once, but is idempotent.
static void init_inodefile(int dev) {
  struct buf *b;
  struct dinode di;

  b = bread(dev, sb.inodestart);
  memmove(&di, b->data, sizeof(struct dinode));

  icache.inodefile.inum = INODEFILEINO;
  icache.inodefile.dev = dev;
  icache.inodefile.type = di.type;
  icache.inodefile.devid = di.devid;
  icache.inodefile.size = di.size;
  icache.inodefile.data = di.data;

  brelse(b);
}

void iinit(int dev) {
  int i;

  initlock(&icache.lock, "icache");
  for (i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
  initsleeplock(&icache.inodefile.lock, "inodefile");

  readsb(dev, &sb);

  // Initialize first user disk
  udiskinit(0);

  cprintf("sb: size %d nblocks %d udiskstart %d bmap start %d inodestart %d freeblock %d\n", sb.size,
          sb.nblocks, sb.udiskstart, sb.bmapstart, sb.inodestart, sb.freeblock);

  for (int i = 0; i < NDISK; i++) {
    cprintf("cids[%d] = %d\n", i, sb.cids[i]);
  }

  init_inodefile(dev);
}

static void read_dinode(uint inum, struct dinode *dip) {
  readi(&icache.inodefile, (char *)dip, INODEOFF(inum), sizeof(*dip));
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not read
// the inode from from disk.
static struct inode *iget(uint dev, uint inum) {
  struct inode *ip, *empty;
  struct dinode dip;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if (empty == 0 && ip->ref == 0) // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if (empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->ref = 1;
  ip->dev = dev;
  ip->inum = inum;

  release(&icache.lock);

  read_dinode(ip->inum, &dip);
  ip->type = dip.type;
  ip->devid = dip.devid;
  ip->size = dip.size;
  ip->data = dip.data;

  if (ip->type == 0)
    panic("iget: no type");

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode *idup(struct inode *ip) {
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
void irelease(struct inode *ip) {
  acquire(&icache.lock);
  // inode has no links and no other references release
  if (ip->ref == 1)
    ip->type = 0;
  ip->ref--;
  release(&icache.lock);
}

// Copy stat information from inode.
void stati(struct inode *ip, struct stat *st) {
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->size = ip->size;
}

// Read data from inode.
int readi(struct inode *ip, char *dst, uint off, uint n) {
  uint tot, m;
  struct buf *bp;

  if (ip->type == T_DEV) {
    if (ip->devid < 0 || ip->devid >= NDEV || !devsw[ip->devid].read)
      return -1;
    return devsw[ip->devid].read(ip, dst, n);
  }

  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > ip->size)
    n = ip->size - off;

  for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    uint cid = myproc()->cid;
    bp = bread(ip->dev, ip->data.startblkno + (cid + 1)*UDISKSIZE + off / BSIZE);
    m = min(n - tot, BSIZE - off % BSIZE);
    memmove(dst, bp->data + off % BSIZE, m);
    brelse(bp);
  }
  return n;
}

// Write data to inode.
int writei(struct inode *ip, char *src, uint off, uint n) {
  if (ip->type == T_DEV) {
    if (ip->devid < 0 || ip->devid >= NDEV || !devsw[ip->devid].write)
      return -1;
    return devsw[ip->devid].write(ip, src, n);
  } else {
    uint b, boff, bn, write, ioff = off;
    uint cid = myproc()->cid;
    write = 0;
    while (n > 0) {
      // Fetch each blook we need to modify
      b = ip->data.startblkno + (cid + 1)*UDISKSIZE + off/BSIZE;
      boff = off%BSIZE;
      if (n < BSIZE-boff) {
        bn = n;
        n = 0;
      } else {
        bn = BSIZE-boff;
        n -= bn;
      }

      // Read in the block
      struct buf *tmp = bread(ROOTDEV, b);
      memmove(tmp->data + boff, src+write, bn);
      bwrite(tmp);
      brelse(tmp);

      off += bn;
      write += bn;
    }

    if (ioff + write > ip->size) {
      ip->size = ioff + write;

      if (ip->inum != 0 || ioff != 0) {
        updateInodeFile(ip);
      }
    }

    return write;
  }
}

// Directories

int namecmp(const char *s, const char *t) { return strncmp(s, t, DIRSIZ); }

struct inode *rootlookup(char *name) {
  return dirlookup(namei("/"), name, 0);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode *dirlookup(struct inode *dp, char *name, uint *poff) {
  uint off, inum;
  struct dirent de;

  if (dp->type != T_DIR)
    panic("dirlookup not DIR");

  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0) {
      // entry matches path element
      if (poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char *skipelem(char *path, char *name) {
  char *s;
  int len;

  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode *namex(char *path, int nameiparent, char *name) {
  struct inode *ip, *next;

  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(namei("/"));

  while ((path = skipelem(path, name)) != 0) {
    if (ip->type != T_DIR)
      goto notfound;

    // Stop one level early.
    if (nameiparent && *path == '\0')
      return ip;

    if ((next = dirlookup(ip, name, 0)) == 0)
      goto notfound;

    irelease(ip);
    ip = next;
  }
  if (nameiparent)
    goto notfound;

  return ip;

notfound:
  irelease(ip);
  return 0;
}

struct inode *namei(char *path) {
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode *nameiparent(char *path, char *name) {
  return namex(path, 1, name);
}


// Initialize a new user disk for container cid
void udiskinit(int cid) {
  for (int i = 0; i < UDISKSIZE; i++) {
    struct buf *b_kernel = bread(ROOTDEV, i);
    struct buf *b_guest = bread(ROOTDEV, (cid + 1)*UDISKSIZE + i);
    memmove(&b_guest->data, &b_kernel->data, BSIZE);
    bwrite(b_guest);
    brelse(b_kernel);
    brelse(b_guest);
  }
}

// Copy a new user disk from cid_src = cid_dest
void udiskcopy(int cid_src, int cid_dest) {
  for (int i = 0; i < UDISKSIZE; i++) {
    struct buf *b_src = bread(ROOTDEV, (cid_src + 1)*UDISKSIZE + i);
    struct buf *b_dest = bread(ROOTDEV, (cid_dest + 1)*UDISKSIZE + i);
    memmove(&b_dest->data, &b_src->data, BSIZE);
    bwrite(b_dest);
    brelse(b_src);
    brelse(b_dest);
  }
}

/* This function finds n contiguous blocks in extent region at
 * or after block #b and sets the bitmap for returned blocks to 1.
 *
 * @param b is the index of block to start searching at
 * @param n is the number of contiguous blocks we need
 *
 * @return index of starting block on success,
 *         and 0 on error
 */
uint
findBlocks(uint b, uint n) {
  struct buf *cur;
  uint i, j;
  bool found = false;
  // Loop through each block in bitmap
  while (!found) {
    i = BBLOCK(b, sb);
    cur = bread(ROOTDEV, i);
    if (b%BPB + n < BPB) {
      // Case 1: bit for n blocks starting at b are in i
      for (j = 0; j < n; j++) {
        // Get byte containing info for block b+j
        uchar byte = cur->data[((b+j)%BPB)/8];
        // Check if b+j bit is on
        byte = byte << (((b+j)%BPB)%8);
        if (byte) {
          // Block b+j used, continue searching at b+j+1
          b = b+j+1;
          j = n+1;
        } else {
          // Turn on the bit
          cur->data[((b+j)%BPB)/8] |= 1 << (7-(((b+j)%BPB)%8));
        }
      }
      if (j <= n) {
        found = true;
        bwrite(cur);
      }
      brelse(cur);
    } else {
      // Case 2: bit for n blocks starting at b are in i and i+1
      // Check for fitst b%BPB + n - BPB bits
      uint bits = BPB-(b%BPB);
      for (j = 0; j < bits; j++) {
        // Get byte containing info for block b+j
        uchar byte = cur->data[((b+j)%BPB)/8];
        // Check if b+j bit is on
        byte = byte << (((b+j)%BPB)%8);
        if (byte) {
          // Block b+j used, continue searching at b+j+1
          b = b+j+1;
          j = bits+1;
        } else {
          // Turn on the bit
          cur->data[((b+j)%BPB)/8] |= 1 << (7-(((b+j)%BPB)%8));
        }
      }

      if (j > bits) {
        brelse(cur);
        continue;
      }
      // Check for remaining
      bits = b%BPB + n - BPB;
      struct buf *cur1 = bread(ROOTDEV, i+1);
      for (j = 0; j < bits; j++) {
        // Get byte containing info for block b+j
        uchar byte = cur1->data[((b+j)%BPB)/8];
        // Check if b+j bit is on
        byte = byte << (((b+j)%BPB)%8);
        if (byte) {
          // Block b+j used, continue searching at b+j+1
          b = b+n-bits+j;
          j = bits+1;
        } else {
          // Turn on the bit
          cur->data[((b+j)%BPB)/8] |= 1 << (7-(((b+j)%BPB)%8));
        }
      }

      if (j <= bits) {
        found = true;
        bwrite(cur);
        bwrite(cur1);
      }
      brelse(cur);
      brelse(cur1);
    }
  }

  return b;
}

// Rewrites the given inode to the inodefile to update it.
void
updateInodeFile(struct inode *ip)
{
  struct dinode di;

  di.type = ip->type;
  di.size = ip->size;
  di.data = ip->data;

  writei(&icache.inodefile, (char*)&di, INODEOFF(ip->inum), sizeof(di));
}

// Rewrites the given inode to the inodefile to update it.
void
updateRootdir(struct dirent de)
{
  struct inode *rootdir = iget(ROOTDEV, ROOTINO);
  writei(rootdir, (char*)&de, rootdir->size, sizeof(de));
  irelease(rootdir);
}

void
createInode(char *name) {
  acquiresleep(&icache.inodefile.lock);

  struct inode in;
  struct dirent de;
  // Get inum of new inode
  in.inum = icache.inodefile.size/sizeof(struct dinode);

  // Set up new inode
  in.dev = ROOTDEV;
  in.type = T_FILE;
  in.size = 0;
  in.data.startblkno = findBlocks(sb.inodestart, 20);
  in.data.nblocks = 20;

  // Update inodefile
  updateInodeFile(&in);

  // Add dirent to root directory
  de.inum = in.inum;
  memmove(de.name, name, DIRSIZ);
  updateRootdir(de);

  releasesleep(&icache.inodefile.lock);
}
