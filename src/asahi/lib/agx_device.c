/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "agx_device.h"
#include <inttypes.h>
#include "agx_bo.h"
#include "decode.h"

#include <fcntl.h>
#include <xf86drm.h>
#include "drm-uapi/asahi_drm.h"
#include "drm-uapi/dma-buf.h"
#include "util/log.h"
#include "util/os_file.h"
#include "util/os_mman.h"
#include "util/simple_mtx.h"

void
agx_bo_free(struct agx_device *dev, struct agx_bo *bo)
{
   const uint64_t handle = bo->handle;

   if (bo->ptr.cpu)
      munmap(bo->ptr.cpu, bo->size);

   if (bo->ptr.gpu) {
      struct util_vma_heap *heap;
      uint64_t bo_addr = bo->ptr.gpu;

      if (bo->flags & AGX_BO_LOW_VA) {
         heap = &dev->usc_heap;
         bo_addr += dev->shader_base;
      } else {
         heap = &dev->main_heap;
      }

      simple_mtx_lock(&dev->vma_lock);
      util_vma_heap_free(heap, bo_addr, bo->size + dev->guard_size);
      simple_mtx_unlock(&dev->vma_lock);

      /* No need to unmap the BO, as the kernel will take care of that when we
       * close it. */
   }

   if (bo->prime_fd != -1)
      close(bo->prime_fd);

   /* Reset the handle. This has to happen before the GEM close to avoid a race.
    */
   memset(bo, 0, sizeof(*bo));
   __sync_synchronize();

   struct drm_gem_close args = {.handle = handle};
   drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &args);
}

static int
agx_bo_bind(struct agx_device *dev, struct agx_bo *bo, uint64_t addr,
            uint32_t flags)
{
   struct drm_asahi_gem_bind gem_bind = {
      .op = ASAHI_BIND_OP_BIND,
      .flags = flags,
      .handle = bo->handle,
      .vm_id = dev->vm_id,
      .offset = 0,
      .range = bo->size,
      .addr = addr,
   };

   int ret = drmIoctl(dev->fd, DRM_IOCTL_ASAHI_GEM_BIND, &gem_bind);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_GEM_BIND failed: %m (handle=%d)\n",
              bo->handle);
   }

   return ret;
}

struct agx_bo *
agx_bo_alloc(struct agx_device *dev, size_t size, enum agx_bo_flags flags)
{
   struct agx_bo *bo;
   unsigned handle = 0;

   size = ALIGN_POT(size, dev->params.vm_page_size);

   /* executable implies low va */
   assert(!(flags & AGX_BO_EXEC) || (flags & AGX_BO_LOW_VA));

   struct drm_asahi_gem_create gem_create = {.size = size};

   if (flags & AGX_BO_WRITEBACK)
      gem_create.flags |= ASAHI_GEM_WRITEBACK;

   if (!(flags & (AGX_BO_SHARED | AGX_BO_SHAREABLE))) {
      gem_create.flags |= ASAHI_GEM_VM_PRIVATE;
      gem_create.vm_id = dev->vm_id;
   }

   int ret = drmIoctl(dev->fd, DRM_IOCTL_ASAHI_GEM_CREATE, &gem_create);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_GEM_CREATE failed: %m\n");
      return NULL;
   }

   handle = gem_create.handle;

   pthread_mutex_lock(&dev->bo_map_lock);
   bo = agx_lookup_bo(dev, handle);
   dev->max_handle = MAX2(dev->max_handle, handle);
   pthread_mutex_unlock(&dev->bo_map_lock);

   /* Fresh handle */
   assert(!memcmp(bo, &((struct agx_bo){}), sizeof(*bo)));

   bo->type = AGX_ALLOC_REGULAR;
   bo->size = gem_create.size;
   bo->flags = flags;
   bo->dev = dev;
   bo->handle = handle;
   bo->prime_fd = -1;

   ASSERTED bool lo = (flags & AGX_BO_LOW_VA);

   struct util_vma_heap *heap;
   if (lo)
      heap = &dev->usc_heap;
   else
      heap = &dev->main_heap;

   simple_mtx_lock(&dev->vma_lock);
   bo->ptr.gpu = util_vma_heap_alloc(heap, size + dev->guard_size,
                                     dev->params.vm_page_size);
   simple_mtx_unlock(&dev->vma_lock);
   if (!bo->ptr.gpu) {
      fprintf(stderr, "Failed to allocate BO VMA\n");
      agx_bo_free(dev, bo);
      return NULL;
   }

   bo->guid = bo->handle; /* TODO: We don't care about guids */

   uint32_t bind = ASAHI_BIND_READ;
   if (!(flags & AGX_BO_READONLY)) {
      bind |= ASAHI_BIND_WRITE;
   }

   ret = agx_bo_bind(dev, bo, bo->ptr.gpu, bind);
   if (ret) {
      agx_bo_free(dev, bo);
      return NULL;
   }

   agx_bo_mmap(bo);

   if (flags & AGX_BO_LOW_VA)
      bo->ptr.gpu -= dev->shader_base;

   assert(bo->ptr.gpu < (1ull << (lo ? 32 : 40)));

   return bo;
}

void
agx_bo_mmap(struct agx_bo *bo)
{
   struct drm_asahi_gem_mmap_offset gem_mmap_offset = {.handle = bo->handle};
   int ret;

   if (bo->ptr.cpu)
      return;

   ret =
      drmIoctl(bo->dev->fd, DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET, &gem_mmap_offset);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_MMAP_BO failed: %m\n");
      assert(0);
   }

   bo->ptr.cpu = os_mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         bo->dev->fd, gem_mmap_offset.offset);
   if (bo->ptr.cpu == MAP_FAILED) {
      bo->ptr.cpu = NULL;
      fprintf(stderr,
              "mmap failed: result=%p size=0x%llx fd=%i offset=0x%llx %m\n",
              bo->ptr.cpu, (long long)bo->size, bo->dev->fd,
              (long long)gem_mmap_offset.offset);
   }
}

struct agx_bo *
agx_bo_import(struct agx_device *dev, int fd)
{
   struct agx_bo *bo;
   ASSERTED int ret;
   unsigned gem_handle;

   pthread_mutex_lock(&dev->bo_map_lock);

   ret = drmPrimeFDToHandle(dev->fd, fd, &gem_handle);
   if (ret) {
      fprintf(stderr, "import failed: Could not map fd %d to handle\n", fd);
      pthread_mutex_unlock(&dev->bo_map_lock);
      return NULL;
   }

   bo = agx_lookup_bo(dev, gem_handle);
   dev->max_handle = MAX2(dev->max_handle, gem_handle);

   if (!bo->dev) {
      bo->dev = dev;
      bo->size = lseek(fd, 0, SEEK_END);

      /* Sometimes this can fail and return -1. size of -1 is not
       * a nice thing for mmap to try mmap. Be more robust also
       * for zero sized maps and fail nicely too
       */
      if ((bo->size == 0) || (bo->size == (size_t)-1)) {
         pthread_mutex_unlock(&dev->bo_map_lock);
         return NULL;
      }
      if (bo->size & (dev->params.vm_page_size - 1)) {
         fprintf(
            stderr,
            "import failed: BO is not a multiple of the page size (0x%llx bytes)\n",
            (long long)bo->size);
         goto error;
      }

      bo->flags = AGX_BO_SHARED | AGX_BO_SHAREABLE;
      bo->handle = gem_handle;
      bo->prime_fd = os_dupfd_cloexec(fd);
      bo->label = "Imported BO";
      assert(bo->prime_fd >= 0);

      p_atomic_set(&bo->refcnt, 1);

      simple_mtx_lock(&dev->vma_lock);
      bo->ptr.gpu = util_vma_heap_alloc(
         &dev->main_heap, bo->size + dev->guard_size, dev->params.vm_page_size);
      simple_mtx_unlock(&dev->vma_lock);

      if (!bo->ptr.gpu) {
         fprintf(
            stderr,
            "import failed: Could not allocate from VMA heap (0x%llx bytes)\n",
            (long long)bo->size);
         abort();
      }

      ret =
         agx_bo_bind(dev, bo, bo->ptr.gpu, ASAHI_BIND_READ | ASAHI_BIND_WRITE);
      if (ret) {
         fprintf(stderr, "import failed: Could not bind BO at 0x%llx\n",
                 (long long)bo->ptr.gpu);
         abort();
      }
   } else {
      /* bo->refcnt == 0 can happen if the BO
       * was being released but agx_bo_import() acquired the
       * lock before agx_bo_unreference(). In that case, refcnt
       * is 0 and we can't use agx_bo_reference() directly, we
       * have to re-initialize the refcnt().
       * Note that agx_bo_unreference() checks
       * refcnt value just after acquiring the lock to
       * make sure the object is not freed if agx_bo_import()
       * acquired it in the meantime.
       */
      if (p_atomic_read(&bo->refcnt) == 0)
         p_atomic_set(&bo->refcnt, 1);
      else
         agx_bo_reference(bo);
   }
   pthread_mutex_unlock(&dev->bo_map_lock);

   return bo;

error:
   memset(bo, 0, sizeof(*bo));
   pthread_mutex_unlock(&dev->bo_map_lock);
   return NULL;
}

int
agx_bo_export(struct agx_bo *bo)
{
   int fd;

   assert(bo->flags & AGX_BO_SHAREABLE);

   if (drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC, &fd))
      return -1;

   if (!(bo->flags & AGX_BO_SHARED)) {
      bo->flags |= AGX_BO_SHARED;
      assert(bo->prime_fd == -1);
      bo->prime_fd = os_dupfd_cloexec(fd);

      /* If there is a pending writer to this BO, import it into the buffer
       * for implicit sync.
       */
      uint32_t writer_syncobj = p_atomic_read_relaxed(&bo->writer_syncobj);
      if (writer_syncobj) {
         int out_sync_fd = -1;
         int ret =
            drmSyncobjExportSyncFile(bo->dev->fd, writer_syncobj, &out_sync_fd);
         assert(ret >= 0);
         assert(out_sync_fd >= 0);

         ret = agx_import_sync_file(bo->dev, bo, out_sync_fd);
         assert(ret >= 0);
         close(out_sync_fd);
      }
   }

   assert(bo->prime_fd >= 0);
   return fd;
}

static void
agx_get_global_ids(struct agx_device *dev)
{
   dev->next_global_id = 0;
   dev->last_global_id = 0x1000000;
}

uint64_t
agx_get_global_id(struct agx_device *dev)
{
   if (unlikely(dev->next_global_id >= dev->last_global_id)) {
      agx_get_global_ids(dev);
   }

   return dev->next_global_id++;
}

static ssize_t
agx_get_params(struct agx_device *dev, void *buf, size_t size)
{
   struct drm_asahi_get_params get_param = {
      .param_group = 0,
      .pointer = (uint64_t)(uintptr_t)buf,
      .size = size,
   };

   memset(buf, 0, size);

   int ret = drmIoctl(dev->fd, DRM_IOCTL_ASAHI_GET_PARAMS, &get_param);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_GET_PARAMS failed: %m\n");
      return -EINVAL;
   }

   return get_param.size;
}

bool
agx_open_device(void *memctx, struct agx_device *dev)
{
   ssize_t params_size = -1;

   params_size = agx_get_params(dev, &dev->params, sizeof(dev->params));
   if (params_size <= 0) {
      assert(0);
      return false;
   }
   assert(params_size >= sizeof(dev->params));

   if (dev->params.unstable_uabi_version != DRM_ASAHI_UNSTABLE_UABI_VERSION) {
      fprintf(stderr, "UABI mismatch: Kernel %d, Mesa %d\n",
              dev->params.unstable_uabi_version,
              DRM_ASAHI_UNSTABLE_UABI_VERSION);
      assert(0);
      return false;
   }

   uint64_t incompat =
      dev->params.feat_incompat & (~AGX_SUPPORTED_INCOMPAT_FEATURES);
   if (incompat) {
      fprintf(stderr, "Missing GPU incompat features: 0x%" PRIx64 "\n",
              incompat);
      assert(0);
      return false;
   }

   if (dev->params.gpu_generation >= 13 && dev->params.gpu_variant != 'P') {
      const char *variant = " Unknown";
      switch (dev->params.gpu_variant) {
      case 'G':
         variant = "";
         break;
      case 'S':
         variant = " Pro";
         break;
      case 'C':
         variant = " Max";
         break;
      case 'D':
         variant = " Ultra";
         break;
      }
      snprintf(dev->name, sizeof(dev->name), "Apple M%d%s (G%d%c %02X)",
               dev->params.gpu_generation - 12, variant,
               dev->params.gpu_generation, dev->params.gpu_variant,
               dev->params.gpu_revision + 0xA0);
   } else {
      // Note: untested, theoretically this is the logic for at least a few
      // generations back.
      const char *variant = " Unknown";
      switch (dev->params.gpu_variant) {
      case 'P':
         variant = "";
         break;
      case 'G':
         variant = "X";
         break;
      }
      snprintf(dev->name, sizeof(dev->name), "Apple A%d%s (G%d%c %02X)",
               dev->params.gpu_generation + 1, variant,
               dev->params.gpu_generation, dev->params.gpu_variant,
               dev->params.gpu_revision + 0xA0);
   }

   dev->guard_size = dev->params.vm_page_size;
   dev->shader_base = dev->params.vm_shader_start;

   util_sparse_array_init(&dev->bo_map, sizeof(struct agx_bo), 512);
   pthread_mutex_init(&dev->bo_map_lock, NULL);

   simple_mtx_init(&dev->bo_cache.lock, mtx_plain);
   list_inithead(&dev->bo_cache.lru);

   for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); ++i)
      list_inithead(&dev->bo_cache.buckets[i]);

   struct drm_asahi_vm_create vm_create = {};

   int ret = drmIoctl(dev->fd, DRM_IOCTL_ASAHI_VM_CREATE, &vm_create);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_VM_CREATE failed: %m\n");
      assert(0);
      return false;
   }

   simple_mtx_init(&dev->vma_lock, mtx_plain);
   util_vma_heap_init(&dev->main_heap, dev->params.vm_user_start,
                      dev->params.vm_user_end - dev->params.vm_user_start + 1);
   util_vma_heap_init(
      &dev->usc_heap, dev->params.vm_shader_start,
      dev->params.vm_shader_end - dev->params.vm_shader_start + 1);

   dev->vm_id = vm_create.vm_id;

   dev->queue_id = agx_create_command_queue(
      dev, DRM_ASAHI_QUEUE_CAP_RENDER | DRM_ASAHI_QUEUE_CAP_BLIT |
              DRM_ASAHI_QUEUE_CAP_COMPUTE);
   agx_get_global_ids(dev);

   return true;
}

void
agx_close_device(struct agx_device *dev)
{
   agx_bo_cache_evict_all(dev);
   util_sparse_array_finish(&dev->bo_map);

   util_vma_heap_finish(&dev->main_heap);
   util_vma_heap_finish(&dev->usc_heap);

   close(dev->fd);
}

uint32_t
agx_create_command_queue(struct agx_device *dev, uint32_t caps)
{
   struct drm_asahi_queue_create queue_create = {
      .vm_id = dev->vm_id,
      .queue_caps = caps,
      .priority = 1,
      .flags = 0,
   };

   int ret = drmIoctl(dev->fd, DRM_IOCTL_ASAHI_QUEUE_CREATE, &queue_create);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_QUEUE_CREATE failed: %m\n");
      assert(0);
   }

   return queue_create.queue_id;
}

int
agx_submit_single(struct agx_device *dev, enum drm_asahi_cmd_type cmd_type,
                  uint32_t barriers, struct drm_asahi_sync *in_syncs,
                  unsigned in_sync_count, struct drm_asahi_sync *out_syncs,
                  unsigned out_sync_count, void *cmdbuf, uint32_t result_handle,
                  uint32_t result_off, uint32_t result_size)
{
   size_t cmdbuf_size;

   switch (cmd_type) {
   case DRM_ASAHI_CMD_RENDER:
      cmdbuf_size = sizeof(struct drm_asahi_cmd_render);
      break;
   case DRM_ASAHI_CMD_BLIT:
      assert(0);
      return -ENOTSUP;
   case DRM_ASAHI_CMD_COMPUTE:
      cmdbuf_size = sizeof(struct drm_asahi_cmd_compute);
      break;
   default:
      assert(0);
      return -ENOTSUP;
   }

   struct drm_asahi_command cmd = {
      .cmd_type = cmd_type,
      .flags = 0,
      .cmd_buffer = (uint64_t)(uintptr_t)cmdbuf,
      .cmd_buffer_size = cmdbuf_size,
      .result_offset = result_off,
      .result_size = result_size,
      .barriers = {DRM_ASAHI_BARRIER_NONE, DRM_ASAHI_BARRIER_NONE},
   };

   for (int i = 0; i < DRM_ASAHI_SUBQUEUE_COUNT; i++) {
      if (barriers & (1 << i)) {
         cmd.barriers[i] = 0; // Barrier on previous submission
      }
   }

   struct drm_asahi_submit submit = {
      .flags = 0,
      .queue_id = dev->queue_id,
      .result_handle = result_handle,
      .in_sync_count = in_sync_count,
      .out_sync_count = out_sync_count,
      .command_count = 1,
      .in_syncs = (uint64_t)(uintptr_t)in_syncs,
      .out_syncs = (uint64_t)(uintptr_t)out_syncs,
      .commands = (uint64_t)(uintptr_t)&cmd,
   };

   int ret = drmIoctl(dev->fd, DRM_IOCTL_ASAHI_SUBMIT, &submit);
   if (ret) {
      switch (cmd_type) {
      case DRM_ASAHI_CMD_RENDER: {
         struct drm_asahi_cmd_render *c = cmdbuf;
         fprintf(
            stderr,
            "DRM_IOCTL_ASAHI_SUBMIT render failed: %m (%dx%d tile %dx%d layers %d samples %d)\n",
            c->fb_width, c->fb_height, c->utile_width, c->utile_height,
            c->layers, c->samples);
         assert(0);
         break;
      }
      case DRM_ASAHI_CMD_COMPUTE:
         fprintf(stderr, "DRM_IOCTL_ASAHI_SUBMIT compute failed: %m\n");
         assert(0);
         break;
      default:
         assert(0);
      }
   }

   if (ret == ENODEV)
      abort();

   return ret;
}

int
agx_import_sync_file(struct agx_device *dev, struct agx_bo *bo, int fd)
{
   struct dma_buf_import_sync_file import_sync_file_ioctl = {
      .flags = DMA_BUF_SYNC_WRITE,
      .fd = fd,
   };

   assert(fd >= 0);
   assert(bo->prime_fd != -1);

   int ret = drmIoctl(bo->prime_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE,
                      &import_sync_file_ioctl);
   assert(ret >= 0);

   return ret;
}

int
agx_export_sync_file(struct agx_device *dev, struct agx_bo *bo)
{
   struct dma_buf_export_sync_file export_sync_file_ioctl = {
      .flags = DMA_BUF_SYNC_RW,
      .fd = -1,
   };

   assert(bo->prime_fd != -1);

   int ret = drmIoctl(bo->prime_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE,
                      &export_sync_file_ioctl);
   assert(ret >= 0);
   assert(export_sync_file_ioctl.fd >= 0);

   return ret >= 0 ? export_sync_file_ioctl.fd : ret;
}

void
agx_debug_fault(struct agx_device *dev, uint64_t addr)
{
   pthread_mutex_lock(&dev->bo_map_lock);

   struct agx_bo *best = NULL;

   for (uint32_t handle = 0; handle < dev->max_handle; handle++) {
      struct agx_bo *bo = agx_lookup_bo(dev, handle);
      uint64_t bo_addr = bo->ptr.gpu;
      if (bo->flags & AGX_BO_LOW_VA)
         bo_addr += dev->shader_base;

      if (!bo->dev || bo_addr > addr)
         continue;

      if (!best || bo_addr > best->ptr.gpu)
         best = bo;
   }

   if (!best) {
      mesa_logw("Address 0x%" PRIx64 " is unknown\n", addr);
   } else {
      uint64_t start = best->ptr.gpu;
      uint64_t end = best->ptr.gpu + best->size;
      if (addr > (end + 1024 * 1024 * 1024)) {
         /* 1GiB max as a sanity check */
         mesa_logw("Address 0x%" PRIx64 " is unknown\n", addr);
      } else if (addr > end) {
         mesa_logw("Address 0x%" PRIx64 " is 0x%" PRIx64
                   " bytes beyond an object at 0x%" PRIx64 "..0x%" PRIx64
                   " (%s)\n",
                   addr, addr - end, start, end - 1, best->label);
      } else {
         mesa_logw("Address 0x%" PRIx64 " is 0x%" PRIx64
                   " bytes into an object at 0x%" PRIx64 "..0x%" PRIx64
                   " (%s)\n",
                   addr, addr - start, start, end - 1, best->label);
      }
   }

   pthread_mutex_unlock(&dev->bo_map_lock);
}
