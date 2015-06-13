/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <gralloc_priv.h>
#include <utils/constants.h>

#include "hwc_display.h"
#include "hwc_logger.h"

#define __CLASS__ "HWCDisplay"

namespace sde {

HWCDisplay::HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type,
                       int id)
  : core_intf_(core_intf), hwc_procs_(hwc_procs), type_(type), id_(id), display_intf_(NULL) {
}

int HWCDisplay::Init() {
  DisplayError error = core_intf_->CreateDisplay(type_, this, &display_intf_);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Display create failed. Error = %d", error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::Deinit() {
  DisplayError error = core_intf_->DestroyDisplay(display_intf_);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Display destroy failed. Error = %d", error);
    return -EINVAL;
  }

  if (LIKELY(layer_stack_memory_.raw)) {
    delete[] layer_stack_memory_.raw;
  }

  return 0;
}

int HWCDisplay::EventControl(int event, int enable) {
  DisplayError error = kErrorNone;

  switch (event) {
  case HWC_EVENT_VSYNC:
    error = display_intf_->SetVSyncState(enable);
    break;
  case HWC_EVENT_ORIENTATION:
    // TODO(user): Need to handle this case
    break;
  default:
    DLOGW("Unsupported event = %d", event);
  }

  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Failed. event = %d, enable = %d, error = %d", event, enable, error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::Blank(int blank) {
  DLOGI("blank = %d, display = %d", blank, id_);
  DisplayState state = blank ? kStateOff : kStateOn;
  return SetState(state);
}

int HWCDisplay::GetDisplayConfigs(uint32_t *configs, size_t *num_configs) {
  if (*num_configs > 0) {
    configs[0] = 0;
    *num_configs = 1;
  }

  return 0;
}

int HWCDisplay::GetDisplayAttributes(uint32_t config, const uint32_t *attributes, int32_t *values) {
  DisplayError error = kErrorNone;

  DisplayConfigVariableInfo variable_config;
  error = display_intf_->GetConfig(&variable_config, 0);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("GetConfig variable info failed. Error = %d", error);
    return -EINVAL;
  }

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    switch (attributes[i]) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      values[i] = variable_config.vsync_period_ns;
      break;
    case HWC_DISPLAY_WIDTH:
      values[i] = variable_config.x_pixels;
      break;
    case HWC_DISPLAY_HEIGHT:
      values[i] = variable_config.y_pixels;
      break;
    case HWC_DISPLAY_DPI_X:
      values[i] = INT32(variable_config.x_dpi * 1000.0f);
      break;
    case HWC_DISPLAY_DPI_Y:
      values[i] = INT32(variable_config.y_dpi * 1000.0f);
      break;
    case HWC_DISPLAY_SECURE:
      values[i] = INT32(true); // For backward compatibility. All Physical displays are secure
      break;
    default:
      DLOGW("Spurious attribute type = %d", attributes[i]);
      return -EINVAL;
    }
  }

  return 0;
}

int HWCDisplay::SetState(DisplayState state) {
  DisplayError error = display_intf_->SetDisplayState(state);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Set state failed. Error = %d", error);
    return -EINVAL;
  }

  return 0;
}

DisplayError HWCDisplay::VSync(const DisplayEventVSync &vsync) {
  if (*hwc_procs_) {
    (*hwc_procs_)->vsync(*hwc_procs_, id_, vsync.timestamp);
  }

  return kErrorNone;
}

DisplayError HWCDisplay::Refresh() {
  if (*hwc_procs_) {
    (*hwc_procs_)->invalidate(*hwc_procs_);
  }

  return kErrorNone;
}

int HWCDisplay::AllocateLayerStack(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;

  // Allocate memory for a) total number of layers b) buffer handle for each layer c) number of
  // visible rectangles in each layer d) dirty rectangle for each layer
  size_t required_size = num_hw_layers * (sizeof(Layer) + sizeof(LayerBuffer));
  for (size_t i = 0; i < num_hw_layers; i++) {
    // visible rectangles + 1 dirty rectangle
    size_t num_rects = content_list->hwLayers[i].visibleRegionScreen.numRects + 1;
    required_size += num_rects * sizeof(LayerRect);
  }

  // Layer array may be large enough to hold current number of layers.
  // If not, re-allocate it now.
  if (UNLIKELY(layer_stack_memory_.size < required_size)) {
    if (LIKELY(layer_stack_memory_.raw)) {
      delete[] layer_stack_memory_.raw;
      layer_stack_memory_.size = 0;
    }

    // Allocate in multiple of kSizeSteps.
    required_size = ROUND_UP(required_size, layer_stack_memory_.kSizeSteps);

    layer_stack_memory_.raw = new uint8_t[required_size];
    if (UNLIKELY(!layer_stack_memory_.raw)) {
      return -ENOMEM;
    }

    layer_stack_memory_.size = required_size;
  }

  // Assign memory addresses now.
  uint8_t *current_address = layer_stack_memory_.raw;

  // Layer array address
  layer_stack_ = LayerStack();
  layer_stack_.layers = reinterpret_cast<Layer *>(current_address);
  layer_stack_.layer_count = static_cast<uint32_t>(num_hw_layers);
  current_address += num_hw_layers * sizeof(Layer);

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    layer = Layer();

    // Layer buffer handle address
    layer.input_buffer = reinterpret_cast<LayerBuffer *>(current_address);
    *layer.input_buffer = LayerBuffer();
    current_address += sizeof(LayerBuffer);

    // Visible rectangle address
    layer.visible_regions.rect = reinterpret_cast<LayerRect *>(current_address);
    layer.visible_regions.count = static_cast<uint32_t>(hwc_layer.visibleRegionScreen.numRects);
    for (size_t i = 0; i < layer.visible_regions.count; i++) {
      *layer.visible_regions.rect = LayerRect();
    }
    current_address += hwc_layer.visibleRegionScreen.numRects * sizeof(LayerRect);

    // Dirty rectangle address
    layer.dirty_regions.rect = reinterpret_cast<LayerRect *>(current_address);
    layer.dirty_regions.count = 1;
    *layer.dirty_regions.rect = LayerRect();
    current_address += sizeof(LayerRect);
  }

  return 0;
}

int HWCDisplay::PrepareLayerStack(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
  if (num_hw_layers <= 1) {
    return 0;
  }

  // Configure each layer
  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);

    Layer &layer = layer_stack_.layers[i];
    LayerBuffer *layer_buffer = layer.input_buffer;

    if (pvt_handle) {
      if (SetFormat(pvt_handle->format, &layer_buffer->format)) {
        return -EINVAL;
      }

      layer_buffer->width = pvt_handle->width;
      layer_buffer->height = pvt_handle->height;
      if (pvt_handle->bufferType == BUFFER_TYPE_VIDEO) {
        layer_stack_.flags.video_present = true;
      }
      if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        layer_stack_.flags.secure_present = true;
      }
    }

    SetRect(hwc_layer.displayFrame, &layer.dst_rect);
    SetRect(hwc_layer.sourceCropf, &layer.src_rect);
    for (size_t j = 0; j < hwc_layer.visibleRegionScreen.numRects; j++) {
        SetRect(hwc_layer.visibleRegionScreen.rects[j], &layer.visible_regions.rect[j]);
    }
    SetRect(hwc_layer.dirtyRect, &layer.dirty_regions.rect[0]);
    SetComposition(hwc_layer.compositionType, &layer.composition);
    SetBlending(hwc_layer.blending, &layer.blending);

    LayerTransform &layer_transform = layer.transform;
    uint32_t &hwc_transform = hwc_layer.transform;
    layer_transform.flip_horizontal = ((hwc_transform & HWC_TRANSFORM_FLIP_H) > 0);
    layer_transform.flip_vertical = ((hwc_transform & HWC_TRANSFORM_FLIP_V) > 0);
    layer_transform.rotation = ((hwc_transform & HWC_TRANSFORM_ROT_90) ? 90.0f : 0.0f);

    layer.plane_alpha = hwc_layer.planeAlpha;
    layer.flags.skip = ((hwc_layer.flags & HWC_SKIP_LAYER) > 0);
    layer.flags.updating = (layer_stack_cache_.layer_cache[i].handle != hwc_layer.handle);

    if (layer.flags.skip) {
      layer_stack_.flags.skip_present = true;
    }
  }

  // Configure layer stack
  layer_stack_.flags.geometry_changed = ((content_list->flags & HWC_GEOMETRY_CHANGED) > 0);

  DisplayError error = display_intf_->Prepare(&layer_stack_);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Prepare failed. Error = %d", error);
    return -EINVAL;
  }

  bool needs_fb_refresh = NeedsFrameBufferRefresh(content_list);

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerComposition composition = layer.composition;

    // If current layer does not need frame buffer redraw, then mark it as HWC_OVERLAY
    if (!needs_fb_refresh && (composition != kCompositionGPUTarget)) {
      composition = kCompositionSDE;
    }

    SetComposition(composition, &hwc_layer.compositionType);
  }

  // Cache the current layer stack information like layer_count, composition type and layer handle
  // for the future.
  CacheLayerStackInfo(content_list);

  return 0;
}

int HWCDisplay::CommitLayerStack(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
  if (num_hw_layers <= 1) {
    if (!num_hw_layers) {
      return 0;
    }

    // TODO(user): handle if only 1 layer(fb target) is received.
    int &acquireFenceFd = content_list->hwLayers[0].acquireFenceFd;
    if (acquireFenceFd >= 0) {
      close(acquireFenceFd);
    }

    return 0;
  }

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);
    LayerBuffer *layer_buffer = layer_stack_.layers[i].input_buffer;

    if (pvt_handle) {
      layer_buffer->planes[0].fd = pvt_handle->fd;
      layer_buffer->planes[0].offset = pvt_handle->offset;
      layer_buffer->planes[0].stride = pvt_handle->width;
    }

    layer_buffer->acquire_fence_fd = hwc_layer.acquireFenceFd;
  }

  DisplayError error = display_intf_->Commit(&layer_stack_);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Commit failed. Error = %d", error);
    return -EINVAL;
  }

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerBuffer *layer_buffer = layer_stack_.layers[i].input_buffer;

    if (layer.composition == kCompositionSDE || layer.composition == kCompositionGPUTarget) {
      hwc_layer.releaseFenceFd = layer_buffer->release_fence_fd;
    }

    if (hwc_layer.acquireFenceFd >= 0) {
      close(hwc_layer.acquireFenceFd);
    }
  }

  return 0;
}

bool HWCDisplay::NeedsFrameBufferRefresh(hwc_display_contents_1_t *content_list) {
  uint32_t layer_count = layer_stack_.layer_count;

  // Frame buffer needs to be refreshed for the following reasons:
  // 1. Any layer is marked skip in the current layer stack.
  // 2. Any layer is added/removed/layer properties changes in the current layer stack.
  // 3. Any layer handle is changed and it is marked for GPU composition
  // 4. Any layer's current composition is different from previous composition.
  if ((layer_stack_cache_.layer_count != layer_count) || layer_stack_.flags.skip_present ||
       layer_stack_.flags.geometry_changed) {
    return true;
  }

  for (uint32_t i = 0; i < layer_count; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerCache &layer_cache = layer_stack_cache_.layer_cache[i];

    if (layer.composition == kCompositionGPUTarget) {
      continue;
    }

    if (layer_cache.composition != layer.composition) {
      return true;
    }

    if ((layer.composition == kCompositionGPU) && (layer_cache.handle != hwc_layer.handle)) {
      return true;
    }
  }

  return false;
}

void HWCDisplay::CacheLayerStackInfo(hwc_display_contents_1_t *content_list) {
  uint32_t layer_count = layer_stack_.layer_count;

  for (uint32_t i = 0; i < layer_count; i++) {
    Layer &layer = layer_stack_.layers[i];

    if (layer.composition == kCompositionGPUTarget) {
      continue;
    }

    layer_stack_cache_.layer_cache[i].handle = content_list->hwLayers[i].handle;
    layer_stack_cache_.layer_cache[i].composition = layer.composition;
  }

  layer_stack_cache_.layer_count = layer_count;
}

void HWCDisplay::SetRect(const hwc_rect_t &source, LayerRect *target) {
  target->left = FLOAT(source.left);
  target->top = FLOAT(source.top);
  target->right = FLOAT(source.right);
  target->bottom = FLOAT(source.bottom);
}

void HWCDisplay::SetRect(const hwc_frect_t &source, LayerRect *target) {
  target->left = source.left;
  target->top = source.top;
  target->right = source.right;
  target->bottom = source.bottom;
}

void HWCDisplay::SetComposition(const int32_t &source, LayerComposition *target) {
  switch (source) {
  case HWC_FRAMEBUFFER_TARGET:  *target = kCompositionGPUTarget;  break;
  default:                      *target = kCompositionSDE;        break;
  }
}

void HWCDisplay::SetComposition(const int32_t &source, int32_t *target) {
  switch (source) {
  case kCompositionGPUTarget:   *target = HWC_FRAMEBUFFER_TARGET; break;
  case kCompositionSDE:         *target = HWC_OVERLAY;            break;
  default:                      *target = HWC_FRAMEBUFFER;        break;
  }
}

void HWCDisplay::SetBlending(const int32_t &source, LayerBlending *target) {
  switch (source) {
  case HWC_BLENDING_PREMULT:    *target = kBlendingPremultiplied;   break;
  case HWC_BLENDING_COVERAGE:   *target = kBlendingCoverage;        break;
  default:                      *target = kBlendingNone;            break;
  }
}

int HWCDisplay::SetFormat(const int32_t &source, LayerBufferFormat *target) {
  switch (source) {
  case HAL_PIXEL_FORMAT_RGBA_8888:            *target = kFormatRGBA8888;                  break;
  case HAL_PIXEL_FORMAT_BGRA_8888:            *target = kFormatBGRA8888;                  break;
  case HAL_PIXEL_FORMAT_RGBX_8888:            *target = kFormatRGBX8888;                  break;
  case HAL_PIXEL_FORMAT_BGRX_8888:            *target = kFormatBGRX8888;                  break;
  case HAL_PIXEL_FORMAT_RGB_888:              *target = kFormatRGB888;                    break;
  case HAL_PIXEL_FORMAT_RGB_565:              *target = kFormatRGB565;                    break;
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:   *target = kFormatYCbCr420SemiPlanarVenus;   break;
  default:
    DLOGW("Unsupported format type = %d", source);
    return -EINVAL;
  }

  return 0;
}

}  // namespace sde

