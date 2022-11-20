#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum newfs_file_type {
    NEWFS_REG_FILE,
    NEWFS_DIR,
    NEWFS_SYM_LINK
} NEWFS_FILE_TYPE;
/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NEWFS_MAGIC_NUM           0x00000403  //魔法数自己定义
#define NEWFS_SUPER_OFS           0           //文件系统超级块偏移量
#define NEWFS_ROOT_INO            0

#define NEWFS_SUPER_BLKS          1
#define NEWFS_MAP_INODE_BLKS      1     //由于只有512个inode块所以inode位图只需要1块
#define NEWFS_MAP_DATA_BLKS       1     //由于只有2048个data块所以data位图只需要1块

//考虑FUSE模拟了对4MB容量磁盘的操作
//一个数据块包含两个磁盘块即1KB
//由于存在超级块，位图以及inode所以数据块不能填满至4096
//向下对齐到2048
#define NEWFS_DATA_NUM            2048

//考虑到只有2048个数据块
//只采用直接索引则一个文件最大为6KB
//向下对齐到4KB即每个inode最多使用4个直接索引指针
//那么只需要512个inode
//此时使用磁盘块数目为1+1+512+4096 = 4610 < 8192
#define NEWFS_INODE_NUM           512



#define NEWFS_ERROR_NONE          0           //没有错误
#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NEWFS_MAX_FILE_NAME       128       //文件名长度
#define NEWFS_INODE_PER_FILE      1
#define NEWFS_DATA_PER_FILE       4         //每个文件使用4个数据块
#define NEWFS_DEFAULT_PERM        0777

#define NEWFS_IOC_MAGIC           'S'
#define NEWFS_IOC_SEEK            _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY      0x1
#define NEWFS_FLAG_BUF_OCCUPY     0x2 
/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NEWFS_IO_SZ()                     (newfs_super.sz_io)
#define NEWFS_BLK_SZ()                    (newfs_super.sz_blk)
#define NEWFS_DISK_SZ()                   (newfs_super.sz_disk)
#define NEWFS_DRIVER()                    (newfs_super.driver_fd)

#define NEWFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define NEWFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)

#define NEWFS_BLKS_SZ(blks)               (blks * NEWFS_BLK_SZ())
#define NEWFS_ASSIGN_FNAME(pnewfs_dentry, _fname) memcpy(pnewfs_dentry->fname, _fname, strlen(_fname))
#define NEWFS_INO_OFS(ino)                (newfs_super.inode_offset +NEWFS_BLKS_SZ(ino))
#define NEWFS_DATA_OFS(bno)               (newfs_super.data_offset + NEWFS_BLKS_SZ(bno))

#define NEWFS_IS_DIR(pinode)              (pinode->dentry->ftype == NEWFS_DIR)
#define NEWFS_IS_REG(pinode)              (pinode->dentry->ftype == NEWFS_REG_FILE)
#define NEWFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == NEWFS_SYM_LINK)
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

struct custom_options {
	const char*        device;
	boolean            show_help;
};

struct newfs_inode
{
    int                ino;                           /* 在inode位图中的下标 */
    int                size;                          /* 文件已占用空间 */
    char               target_path[NEWFS_MAX_FILE_NAME];/* store traget path when it is a symlink */
    int                dir_cnt;
    struct newfs_dentry* dentry;                        /* 指向该inode的dentry */
    struct newfs_dentry* dentrys;                       /* 所有目录项 */ 
    uint8_t *          block_pointer[NEWFS_DATA_PER_FILE];  //指向数据块的指针
    int                bno[NEWFS_DATA_PER_FILE];            //指向数据块的块号        
};  

struct newfs_dentry
{
    char               fname[NEWFS_MAX_FILE_NAME];
    struct newfs_dentry* parent;                        /* 父亲Inode的dentry */
    struct newfs_dentry* brother;                       /* 兄弟 */
    int                ino;
    struct newfs_inode*  inode;                         /* 指向inode */
    NEWFS_FILE_TYPE      ftype;
};

struct newfs_super
{
    int                driver_fd;
    
    int                sz_io;
    int                sz_disk;
    int                sz_blk;
    int                sz_usage;
    
    int                max_ino;
    int                max_data;
    
    uint8_t*           map_inode;       //inode位图
    int                map_inode_blks;
    int                map_inode_offset;

    uint8_t*           map_data;        //data位图
    int                map_data_blks;
    int                map_data_offset;
    
    int                inode_offset;
    int                data_offset;

    boolean            is_mounted;

    struct newfs_dentry* root_dentry;
};

static inline struct newfs_dentry* new_dentry(char * fname, NEWFS_FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;     
    return dentry;                                       
}
/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct newfs_super_d
{
    uint32_t           magic_num;
    int                sz_usage;
    
    int                max_ino;             //inode节点数
    int                max_data;            //数据块数

    int                map_inode_blks;      //inode位图块数
    int                map_inode_offset;    //inode位图偏移量

    int                map_data_blks;      //data位图块数
    int                map_data_offset;    //data位图偏移量

    int                inode_offset;       //inode节点偏移量
    int                data_offset;        //数据块偏移量
};

struct newfs_inode_d
{
    int                ino;                           /* 在inode位图中的下标 */
    int                size;                          /* 文件已占用空间 */
    char               target_path[NEWFS_MAX_FILE_NAME];/* store traget path when it is a symlink */
    int                dir_cnt;                             //目录项数量
    NEWFS_FILE_TYPE    ftype;   
    int                bno[NEWFS_DATA_PER_FILE];            //指向数据块的块号
};  

struct newfs_dentry_d
{
    char               fname[NEWFS_MAX_FILE_NAME];
    NEWFS_FILE_TYPE    ftype;
    int                ino;                           /* 指向的ino号 */
};  


#endif /* _TYPES_H_ */