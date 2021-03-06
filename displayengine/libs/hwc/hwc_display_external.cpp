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

#include <utils/constants.h>

#include "hwc_display_external.h"
#include "hwc_logger.h"

#define __CLASS__ "HWCDisplayExternal"

namespace sde {

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kHDMI, HWC_DISPLAY_EXTERNAL) {
}

int HWCDisplayExternal::Init() {
  return 0;
}

int HWCDisplayExternal::Deinit() {
  return 0;
}

int HWCDisplayExternal::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;

  status = AllocateLayerStack(content_list);
  if (UNLIKELY(status)) {
    return status;
  }

  layer_stack_.retire_fence_fd = -1;

  status = PrepareLayerStack(content_list);
  if (UNLIKELY(status)) {
    return status;
  }

  return 0;
}

int HWCDisplayExternal::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  status = HWCDisplay::CommitLayerStack(content_list);
  if (UNLIKELY(status)) {
    return status;
  }

  content_list->retireFenceFd = layer_stack_.retire_fence_fd;

  return 0;
}

int HWCDisplayExternal::PowerOn() {
  return 0;
}

int HWCDisplayExternal::PowerOff() {
  return 0;
}

}  // namespace sde

