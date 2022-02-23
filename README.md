# dmabuf-v4l2-demo
Simple example application to show how to allocate dmabufs from user space  (from a dmabuf heap) and use them for v4l2 capture.

##  dmabuf heaps

If the entries in /dev/dma_heap are missing, maybe the dmabuf heaps are not enabled.
Have a look a these config symbols:
```
CONFIG_DMABUF_HEAPS
CONFIG_DMABUF_SYSFS_STATS
CONFIG_DMABUF_HEAPS_SYSTEM
CONFIG_DMABUF_HEAPS_CMA
```

## Configure CMA area

There are basically three ways to configure the CMA area:
- Kernel configuration (Priority 3)
- Kernel command line (Priority 2)
- Device tree entry (Priority 1)

By using a device tree node it is also possible to specify the location of the CMA area.
Such a device tree node could look like this example:
```
linux,cma {
  compatible = "shared-dma-pool";
  reusable;
  size = <0 0x30000000>;
  alloc-ranges = <0 0x80000000 0 0x30000000>;
  linux,cma-default;
};
```
> Note: Depending on the configuration method, the name of the dmabuf-heap device node changes. If the mentioned device tree node is used, the node will be ***/dev/dma_heap/linux,cma***, otherwise the node will be ***/dev/dma_heap/reserved***.

## v4l2 test device

For quick testing, the vivid v4l2 test driver can be used.
If this module is not yet available, have a look a these config symbols:
```
CONFIG_MEDIA_TEST_SUPPORT
CONFIG_V4L_TEST_DRIVERS
CONFIG_VIDEO_VIVID
```

To create a v4l2 capture device with vivid run:
```
modprobe vivid num_inputs=1
```

## Running the example

To capture 5 frames from /dev/video0 and dump them into /tmp run:
```
dmabuf-v4l2 -d /dev/video0 -o /tmp -l 5
```

One might also be interested in the [dma-buf documentation](https://www.kernel.org/doc/html/latest/driver-api/dma-buf.html).

