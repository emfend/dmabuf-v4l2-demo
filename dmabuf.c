/*
 * Some basic dmabuf(-heap) helpers.
 * 
 * 2022, Matthias Fend <matthias.fend@emfend.at>
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

/*
 * Depending on the configuration method, the name of the device node name
 * of the dmabuf-heap changes. If the CMA area is configured from a device
 * tree node, the heap node is '/dev/dma_heap/linux,cma', otherwise the
 * node is '/dev/dma_heap/reserved'.
 * So let's just try both.
 */
int dmabuf_heap_open()
{
  int i;
  static const char *heap_names[] = { "/dev/dma_heap/linux,cma", "/dev/dma_heap/reserved" };

  for(i = 0; i < 2; i++)
  {
    int fd = open(heap_names[i], O_RDWR, 0);

    if(fd >= 0)
      return fd;
  }

  return -1;
}

void dmabuf_heap_close(int heap_fd)
{
  close(heap_fd);
}

int dmabuf_heap_alloc(int heap_fd, const char *name, size_t size)
{
  struct dma_heap_allocation_data alloc = { 0 };

  alloc.len = size;
  alloc.fd_flags = O_CLOEXEC | O_RDWR;

  if(ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0)
    return -1;

  if(name)
    ioctl(alloc.fd, DMA_BUF_SET_NAME, name);

  return alloc.fd;
}

static int dmabuf_sync(int buf_fd, bool start)
{
  struct dma_buf_sync sync = { 0 };

  sync.flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) | DMA_BUF_SYNC_RW;

  do
  {
    if(ioctl(buf_fd, DMA_BUF_IOCTL_SYNC, &sync) == 0)
      return 0;
  } while((errno == EINTR) || (errno == EAGAIN));

  return -1;
}

int dmabuf_sync_start(int buf_fd)
{
  return dmabuf_sync(buf_fd, true);
}

int dmabuf_sync_stop(int buf_fd)
{
  return dmabuf_sync(buf_fd, false);
}
