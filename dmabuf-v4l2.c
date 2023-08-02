/*
 * Simple example application to show how to allocate dmabufs from user space 
 * (from a dmabuf heap) and use them for v4l2 capture.
 * 
 * 2022, Matthias Fend <matthias.fend@emfend.at>
 */
#define _GNU_SOURCE  
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdarg.h>
#include <linux/videodev2.h>

#include "dmabuf.h"

struct arguments_t
{
  char *vdev_name;
  uint32_t timeout_ms;
  unsigned int loop_count;
  char *output_dir;
  uint32_t width;
  uint32_t height;
  uint32_t fourcc;
};

static void print_usage(const char *progname)
{
  printf("usage: %s [-dwhflo]\n", progname);
  printf("\t-d <video-device\t/dev/videoX\n");
  printf("\t-w <width>\tdesired image width\n");
  printf("\t-h <height>\tdesired image height\n");
  printf("\t-f <fourcc>\tdesired image fourcc\n");
  printf("\t-l <loop-count>\tnumber of program loops\n");
  printf("\t-t <timeout>\ttimeout [ms]\n");
  printf("\t-o <out-dir>\tdirectory for file outputs\n");
}

static int parse_arguments(int argc, char *argv[], struct arguments_t *args)
{
  int c;

  while((c = getopt(argc, argv, "d:l:w:h:f:t:o:")) != -1)
  {
    switch(c)
    {
      case 'd':
        args->vdev_name = optarg;
        break;

      case 'l':
        if(sscanf(optarg, "%u", &args->loop_count) != 1)
        {
          printf("Invalid loop count\n");
          return -1;
        }
        break;

      case 'w':
        if(sscanf(optarg, "%u", &args->width) != 1)
        {
          printf("Invalid image width\n");
          return -1;
        }
        break;

      case 'h':
        if(sscanf(optarg, "%u", &args->height) != 1)
        {
          printf("Invalid image width\n");
          return -1;
        }
        break;

      case 'f':
        if(strlen(optarg) != 4)
        {
          printf("Invalid image fourcc\n");
          return -1;
        }
        args->fourcc = ((uint32_t) optarg[0] << 0) | ((uint32_t) optarg[1] << 8) | ((uint32_t) optarg[2] << 16) | ((uint32_t) optarg[3] << 24);
        break;

      case 't':
        if(sscanf(optarg, "%u", &args->timeout_ms) != 1)
        {
          printf("Invalid timeout\n");
          return -1;
        }
        break;

      case 'o':
        args->output_dir = optarg;
        break;

      case '?':
        return -1;
    }
  }

  return 0;
}

static int dump_image(const void *p, int size, const char *filename)
{
  FILE *fp;

  if((fp = fopen(filename, "w+")) == NULL)
    return -1;
  fwrite(p, size, 1, fp);
  fclose(fp);

  return 0;
}

int open_video_device(const char *vdevice, uint32_t in_width, uint32_t in_height, uint32_t in_fourcc, struct v4l2_pix_format *pix_fmt, bool *mplane_api)
{
  int fd;
  struct v4l2_capability caps;
  struct v4l2_format fmt;

  fd = open(vdevice, O_RDWR);
  if(fd < 0)
  {
    printf("Failed to open %s\n", vdevice);
    return -1;
  }

  memset(&caps, 0, sizeof(caps));
  if(ioctl(fd, VIDIOC_QUERYCAP, &caps))
  {
    printf("VIDIOC_QUERYCAP: %s\n", strerror(errno));
    goto err_cleanup;
  }

  if(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)
  {
    printf("Using single-planar API\n");
    *mplane_api = false;
  }
  else if(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
  {
    printf("Using multi-planar API\n");
    *mplane_api = true;
  }
  else
  {
    printf("Devicce does not support video capture\n");
    goto err_cleanup;
  }

  memset(&fmt, 0, sizeof(fmt));
  if(*mplane_api)
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  else
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if(ioctl(fd, VIDIOC_G_FMT, &fmt))
  {
    printf("VIDIOC_G_FMT: %s\n", strerror(errno));
    goto err_cleanup;
  }

  if(in_width > 0)
    fmt.fmt.pix.width = in_width;

  if(in_height > 0)
    fmt.fmt.pix.height = in_height;

  if(in_fourcc)
    fmt.fmt.pix.pixelformat = in_fourcc;

  if(ioctl(fd, VIDIOC_S_FMT, &fmt))
  {
    printf("VIDIOC_S_FMT: %s\n", strerror(errno));
    goto err_cleanup;
  }

  if(ioctl(fd, VIDIOC_G_FMT, &fmt))
  {
    printf("VIDIOC_G_FMT: %s\n", strerror(errno));
    goto err_cleanup;
  }

  memcpy(pix_fmt, &fmt.fmt.pix, sizeof(*pix_fmt));

  return fd;

err_cleanup:
  close(fd);

  return -1;
}

int main(int argc, char *argv[])
{
  int i;
  struct arguments_t args;
  int dmabuf_heap_fd;
  unsigned int loop_count;
  const int num_buffers = 3;
  int v4l2_fd;
  struct v4l2_pix_format pix_fmt;
  bool mplane_api;
  int dmabuf_fds[num_buffers];
  void *dmabuf_maps[num_buffers];
  struct v4l2_requestbuffers rqbufs;
  struct pollfd pfds[1];

  memset(&args, 0, sizeof(args));
  args.vdev_name = "/dev/video0";
  args.timeout_ms = 5000;
  args.loop_count = 10;
  args.output_dir = "/tmp";
  args.width = 0;
  args.height = 0;
  args.fourcc = 0;

  if(parse_arguments(argc, argv, &args) < 0)
  {
    printf("Invalid arguments\n");
    print_usage(argv[0]);
    return -1;
  }

  /* open v4l2 device */
  v4l2_fd = open_video_device(args.vdev_name, args.width, args.height, args.fourcc, &pix_fmt, &mplane_api);
  if(v4l2_fd < 0)
    return -1;

  printf("Actual v4l2 device:  %s\n", args.vdev_name);
  printf("Actual timeout:      %ums\n", args.timeout_ms);
  printf("Actual image width:  %u\n", pix_fmt.width);
  printf("Actual image height: %u\n", pix_fmt.height);
  printf("Actual image format: %.4s\n", (char*) &pix_fmt.pixelformat);
  printf("Actual image size:   %u\n", pix_fmt.sizeimage);

  /* request buffers from v4l2 device */
  memset(&rqbufs, 0, sizeof(rqbufs));
  rqbufs.count = num_buffers;
  if(mplane_api)
    rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  else
    rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  rqbufs.memory = V4L2_MEMORY_DMABUF;
  if(ioctl(v4l2_fd, VIDIOC_REQBUFS, &rqbufs))
  {
    printf("VIDIOC_REQBUFS: %s\n", strerror(errno));
    goto exit_cleanup;
  }
  if(rqbufs.count < num_buffers)
  {
    printf("VIDIOC_REQBUFS: too few buffers\n");
    goto exit_cleanup;
  }

  dmabuf_heap_fd = dmabuf_heap_open();
  if(dmabuf_heap_fd < 0)
  {
    printf("Could not open dmabuf-heap\n");
    goto exit_cleanup;
  }

  /* allocate and map dmabufs */
  for(i = 0; i < num_buffers; i++)
  {
    dmabuf_fds[i] = dmabuf_heap_alloc(dmabuf_heap_fd, NULL, pix_fmt.sizeimage);
    if(dmabuf_fds[i] < 0)
    {
      printf("Failed to alloc dmabuf %d\n", i);
      goto exit_cleanup;
    }

    dmabuf_maps[i] = mmap(0, pix_fmt.sizeimage, PROT_WRITE | PROT_READ, MAP_SHARED, dmabuf_fds[i], 0);
    if(dmabuf_maps[i] == MAP_FAILED)
    {
      printf("Failed to map dmabuf %d\n", i);
      goto exit_cleanup;
    }
  }

  /* enque dmabufs into v4l2 device */
  for(i = 0; i < num_buffers; ++i)
  {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    memset(&buf, 0, sizeof(buf));
    buf.index = i;
    buf.memory = V4L2_MEMORY_DMABUF;
    if(mplane_api)
    {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      memset(&planes, 0, sizeof(planes));
      buf.m.planes = planes;
      buf.length = 1;
      buf.m.planes[0].m.fd = dmabuf_fds[i];
    }
    else
    {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.m.fd = dmabuf_fds[i];
    }
    if(ioctl(v4l2_fd, VIDIOC_QBUF, &buf))
    {
      printf("VIDIOC_QBUF: %s\n", strerror(errno));
      goto exit_cleanup;
    }
  }

  /* start v4l2 device */
  int type = mplane_api ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if(ioctl(v4l2_fd, VIDIOC_STREAMON, &type))
  {
    printf("VIDIOC_STREAMON: %s\n", strerror(errno));
    goto exit_cleanup;
  }

  pfds[0].fd = v4l2_fd;
  pfds[0].events = POLLIN;

  loop_count = 0;
  while((poll(pfds, 1, args.timeout_ms) > 0) && (loop_count < args.loop_count))
  {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    int buf_index;

    /* dequeue a buffer */
    memset(&buf, 0, sizeof(buf));
    buf.memory = V4L2_MEMORY_DMABUF;
    if(mplane_api)
    {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      memset(&planes, 0, sizeof(planes));
      buf.m.planes = planes;
      buf.length = 1;
    }
    else
    {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }
    if(ioctl(v4l2_fd, VIDIOC_DQBUF, &buf))
    {
      printf("VIDIOC_DQBUF: %s\n", strerror(errno));
      goto exit_cleanup;
    }

    buf_index = buf.index;

    /* prepare buffer for CPU access */
    dmabuf_sync_start(dmabuf_fds[buf_index]);
    {
      char *filename = NULL;

      if(asprintf(&filename, "%s/image_%d.raw", args.output_dir, loop_count) > 0)
      {
        int rc;
        unsigned int offset, length;

        if(mplane_api)
        {
          offset = planes[0].data_offset;
          length = planes[0].bytesused;
        }
        else
        {
          offset = 0;
          length = buf.bytesused;
        }
        rc = dump_image(dmabuf_maps[buf_index] + offset, length, filename);
        printf("Dumping %u bytes with offset %u to %s ... %s\n", length, offset, filename, rc ? "failed" : "OK");
        free(filename);
      }
    }
    /* release buffer for CPU access */
    dmabuf_sync_stop(dmabuf_fds[buf_index]);

    /* enqueue a buffer */
    memset(&buf, 0, sizeof(buf));
    buf.index = buf_index;
    buf.memory = V4L2_MEMORY_DMABUF;
    if(mplane_api)
    {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      memset(&planes, 0, sizeof(planes));
      buf.m.planes = planes;
      buf.length = 1;
      buf.m.planes[0].m.fd = dmabuf_fds[buf_index];
    }
    else
    {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.m.fd = dmabuf_fds[buf_index];
    }
    if(ioctl(v4l2_fd, VIDIOC_QBUF, &buf))
    {
      printf("VIDIOC_QBUF: %s\n", strerror(errno));
      goto exit_cleanup;
    }

    loop_count++;
  }

  exit_cleanup:

  /* 
   * FIXME: add cleanup 
   */

  return 0;
}

