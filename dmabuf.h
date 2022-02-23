/*
 * Some basic dmabuf(-heap) helpers.
 * 
 * 2022, Matthias Fend <matthias.fend@emfend.at>
 */
#ifndef _DMABUF_H_
#define _DMABUF_H_

int dmabuf_heap_open();
void dmabuf_heap_close(int heap_fd);
int dmabuf_heap_alloc(int heap_fd, const char *name, size_t size);
int dmabuf_sync_start(int buf_fd);
int dmabuf_sync_stop(int buf_fd);

#endif
