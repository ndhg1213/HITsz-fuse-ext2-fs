#include "../include/newfs.h"

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int newfs_calc_lvl(const char * path) {
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    //按照一个数据块大小(1024B)封装
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;

    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    //读写磁盘时需要按照磁盘块大小(512B)去读
    while (size_aligned != 0)
    {
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    //按照一个数据块大小(1024B)封装
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    //读写磁盘时需要按照磁盘块大小(512B)去写
    while (size_aligned != 0)
    {
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor = 0; 
    int ino_cursor = 0;     //记录inode块号
    int bno_cursor = 0;     //记录data块号
    int blk_cnt = 0;        //记录已读入的data个数
    boolean is_find_free_entry = FALSE;
    boolean is_find_enough_blk = FALSE;

    //先按照B查找inode位图
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); 
         byte_cursor++)
    {
        //再按照bit查找inode位图
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == newfs_super.max_ino)
        return -NEWFS_ERROR_NOSPACE;

    //此时有空闲inode可以分配
    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;

    //先按照B查找data位图
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); 
         byte_cursor++)
    {
        //再按照bit查找data位图
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前bno_cursor位置空闲 */
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                //将空闲的data块号记入inode中
                inode->bno[blk_cnt] = bno_cursor;
                blk_cnt++;
                if(blk_cnt == NEWFS_DATA_PER_FILE){
                    is_find_enough_blk = TRUE;
                    break;
                }
            }
            bno_cursor++;
        }
        if (is_find_enough_blk) {
            break;
        }
    }

    if (!is_find_enough_blk || bno_cursor == newfs_super.max_data){
        //data块数不够建立一个新文件回收已分配的inode
        free(inode);
        return -NEWFS_ERROR_NOSPACE;
    }
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    //inode指向文件类型需要预分配数据指针
    if (NEWFS_IS_REG(inode)) {
        for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
            inode->block_pointer[blk_cnt] = (uint8_t *)malloc(NEWFS_BLK_SZ());
        }
    }

    return inode;
}
/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    memcpy(inode_d.target_path, inode->target_path, NEWFS_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset = 0;
    int blk_cnt = 0;  

    for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
        inode_d.bno[blk_cnt] = inode->bno[blk_cnt];
    }
    //至此inode_d数据全部填写完毕
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    //当inode为目录时
    //inode的每个block pointer指向的是一堆dentry
    //被同一个指针指向的dentry应当存放在一个data块中
    //因此在刷回inode时需要依次将每个bno中的一堆dentry刷回
    if (NEWFS_IS_DIR(inode)) {   
        blk_cnt = 0;            
        dentry_cursor = inode->dentrys;
        while(dentry_cursor != NULL && blk_cnt < NEWFS_DATA_PER_FILE){
            offset = NEWFS_INO_OFS(inode->bno[blk_cnt]);
            //深搜遍历
            //当前块内最后一个dentry的兄弟指针指向的可能是下一个块内的dentry
            //当前块写完或写满时都要结束写
            while (dentry_cursor != NULL && offset < NEWFS_INO_OFS(inode->bno[blk_cnt] + 1))
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                     sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;                     
                }
            
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct newfs_dentry_d);
            }
            blk_cnt++;
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->bno[blk_cnt]), inode->block_pointer[blk_cnt], 
                             NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
        }
    }
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int dir_cnt = 0;
    int offset = 0;
    int blk_cnt = 0;
    //读取inode_d并根据其中数据对inode简单初始化
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, NEWFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
        inode->bno[blk_cnt] = inode_d.bno[blk_cnt];
    }

    if(NEWFS_IS_DIR(inode)){
        dir_cnt = inode_d.dir_cnt;
        blk_cnt = 0;
        //由于dentry_d中没有兄弟指针
        //循环条件改为dir_cnt > 0
        while(dir_cnt > 0 && blk_cnt < NEWFS_DATA_PER_FILE){
            offset = NEWFS_DATA_OFS(inode->bno[blk_cnt]);
            while(dir_cnt > 0 && offset < NEWFS_DATA_OFS(inode->bno[blk_cnt] + 1)){
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return NULL;                    
                }
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                newfs_alloc_dentry(inode, sub_dentry);

                offset += sizeof(struct newfs_dentry_d);
                dir_cnt--;
            }
            blk_cnt++;
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
            inode->block_pointer[blk_cnt] = (uint8_t *)malloc(NEWFS_BLK_SZ());
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->bno[blk_cnt]), inode->block_pointer[blk_cnt], 
                            NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}
/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct nfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;

    //广搜遍历
    //将传入的dir作为offset选择dentry
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct newfs_inode* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    //找到根目录
    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        //inode是文件则查找失败
        if (NEWFS_IS_REG(inode) && lvl < total_lvl) {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;
            
            //广搜遍历
            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    //如果dentry对应的inode还不存在
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}
/**
 * @brief 挂载newfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Data
 * 
 * BLK_SZ = 2 * IO_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                 ret = NEWFS_ERROR_NONE;
    int                 driver_fd;
    struct newfs_super_d  newfs_super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;

    int                 inode_num;
    int                 map_inode_blks;

    int                 data_num;
    int                 map_data_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    newfs_super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {        //打开失败
        return driver_fd;
    }

    //向超级块中写入相关信息
    newfs_super.driver_fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &newfs_super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    //BLK_SZ = 2 * IO_SZ
    newfs_super.sz_blk = newfs_super.sz_io * 2;

    root_dentry = new_dentry("/", NEWFS_DIR);      //构建根目录

    //驱动读
    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), 
                        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }  
                                                      /* 读取super */
    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM) {     /* 幻数无 */
                                                      /* 估算各部分大小 */
        //不再使用假设估算的方案
        //改为规定大小
        super_blks = NEWFS_SUPER_BLKS;

        inode_num  =  NEWFS_INODE_NUM;

        map_inode_blks = NEWFS_MAP_INODE_BLKS;

        data_num = NEWFS_DATA_NUM;

        map_data_blks = NEWFS_MAP_DATA_BLKS;
        
                                                      /* 布局layout */
        newfs_super.max_ino = inode_num; 
        newfs_super.max_data = data_num;

        newfs_super_d.magic_num = NEWFS_MAGIC_NUM;
        //inode位图的偏移量不变
        //在inode位图后新增data位图
        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(super_blks);
        newfs_super_d.map_data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(map_inode_blks);

        //inode和data布局依次后延
        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + NEWFS_BLKS_SZ(map_data_blks);
        newfs_super_d.data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(inode_num);

        //至此布局完毕
        newfs_super_d.map_inode_blks = map_inode_blks;
        newfs_super_d.map_data_blks = map_data_blks;
        newfs_super_d.max_ino = inode_num;
        newfs_super_d.max_data = data_num;
        
        newfs_super_d.sz_usage = 0;
        NEWFS_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }
    newfs_super.sz_usage   = newfs_super_d.sz_usage;      /* 建立 in-memory 结构 */

    //inode位图相关数据初始化
    newfs_super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_inode_blks = newfs_super_d.map_inode_blks;
    newfs_super.map_inode_offset = newfs_super_d.map_inode_offset;
    newfs_super.inode_offset = newfs_super_d.inode_offset;

    //data位图相关数据初始化
    newfs_super.map_data = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));
    newfs_super.map_data_blks = newfs_super_d.map_data_blks;
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;
    newfs_super.data_offset = newfs_super_d.data_offset;

    //读取inode位图
    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    //读取data位图
    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode            = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted  = TRUE;
    
    return ret;

}
/**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!newfs_super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    newfs_super_d.magic_num           = NEWFS_MAGIC_NUM;

    //记录超级块关于inode位图相关信息
    newfs_super_d.map_inode_blks      = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset    = newfs_super.map_inode_offset;

    //记录超级块关于data位图相关信息
    newfs_super_d.map_data_blks      = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset    = newfs_super.map_data_offset;

    //记录超级块关于inode和data相关信息
    newfs_super_d.inode_offset         = newfs_super.inode_offset;
    newfs_super_d.data_offset         = newfs_super.data_offset;

    newfs_super_d.sz_usage            = newfs_super.sz_usage;

    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    //更新磁盘上inode位图块信息
    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    //更新磁盘上data位图块信息
    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}
