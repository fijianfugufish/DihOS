static int sacx_read_file_all(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    KFile file;
    uint64_t size64 = 0u;
    uint32_t size = 0u;
    uint32_t total_read = 0u;
    uint32_t pages = 0u;
    uint8_t *buf = 0;

    if (!path || !out_buf || !out_size)
        return -1;

    *out_buf = 0;
    *out_size = 0u;

    if(sacx_loader_checkpoint())return -1;
    if (kfile_open(&file, path, KFILE_READ) != 0)
        return -1;

    size64 = kfile_size(&file);
    if (size64 == 0u || size64 > SACX_MAX_FILE_BYTES)
    {
        kfile_close(&file);
        return -1;
    }

    size = (uint32_t)size64;
    pages = (size + 4095u) / 4096u;
    buf = (uint8_t *)pmem_alloc_pages((uint64_t)pages);
    if (!buf)
    {
        kfile_close(&file);
        return -1;
    }

    while (total_read < size)
    {
        uint32_t chunk = size - total_read;
        uint32_t read = 0u;
        if (chunk > 16384u)
            chunk = 16384u;
        if (sacx_loader_checkpoint() || kfile_read(&file, buf + total_read, chunk, &read) != 0 || read != chunk)
        {
            pmem_free_pages(buf, pages);
            kfile_close(&file);
            return -1;
        }
        total_read += read;
        if(sacx_loader_checkpoint()){
            pmem_free_pages(buf,pages);kfile_close(&file);return -1;
        }
    }

    kfile_close(&file);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

