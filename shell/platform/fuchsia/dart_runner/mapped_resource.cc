// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mapped_resource.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <lib/trace/event.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/dlfcn.h>
#include <zircon/status.h>

#include "flutter/fml/logging.h"
#include "logging.h"
#include "runtime/dart/utils/inlines.h"
#include "runtime/dart/utils/vmo.h"
#include "third_party/dart/runtime/include/dart_api.h"

namespace dart_runner {

static bool OpenVmo(fuchsia::mem::Buffer* resource_vmo,
                    fdio_ns_t* namespc,
                    const std::string& path,
                    bool executable) {
  TRACE_DURATION("dart", "LoadFromNamespace", "path", path);

  // openat of a path with a leading '/' ignores the namespace fd.
  dart_utils::Check(path[0] != '/', LOG_TAG);

  if (namespc == nullptr) {
    if (!dart_utils::VmoFromFilename(path, resource_vmo)) {
      return false;
    }
  } else {
    auto root_dir = fdio_ns_opendir(namespc);
    if (root_dir < 0) {
      FML_LOG(ERROR) << "Failed to open namespace directory";
      return false;
    }

    bool result = dart_utils::VmoFromFilenameAt(root_dir, path, resource_vmo);
    close(root_dir);
    if (!result) {
      return result;
    }
  }

  if (executable) {
    // VmoFromFilenameAt will return VMOs without ZX_RIGHT_EXECUTE,
    // so we need replace_as_executable to be able to map them as
    // ZX_VM_PERM_EXECUTE.
    // TODO(mdempsky): Update comment once SEC-42 is fixed.
    zx_status_t status = resource_vmo->vmo.replace_as_executable(
        zx::handle(), &resource_vmo->vmo);
    if (status != ZX_OK) {
      FML_LOG(ERROR) << "Failed to make VMO executable: "
                     << zx_status_get_string(status);
      return false;
    }
  }

  return true;
}

bool MappedResource::LoadFromNamespace(fdio_ns_t* namespc,
                                       const std::string& path,
                                       MappedResource& resource,
                                       bool executable) {
  fuchsia::mem::Buffer resource_vmo;
  return OpenVmo(&resource_vmo, namespc, path, executable) &&
         LoadFromVmo(path, std::move(resource_vmo), resource, executable);
}

bool MappedResource::LoadFromVmo(const std::string& path,
                                 fuchsia::mem::Buffer resource_vmo,
                                 MappedResource& resource,
                                 bool executable) {
  if (resource_vmo.size == 0) {
    return true;
  }

  uint32_t flags = ZX_VM_PERM_READ;
  if (executable) {
    flags |= ZX_VM_PERM_EXECUTE;
  }
  uintptr_t addr;
  zx_status_t status = zx::vmar::root_self()->map(
      0, resource_vmo.vmo, 0, resource_vmo.size, flags, &addr);
  if (status != ZX_OK) {
    FML_LOG(ERROR) << "Failed to map " << path << ": "
                   << zx_status_get_string(status);

    return false;
  }

  resource.address_ = reinterpret_cast<void*>(addr);
  resource.size_ = resource_vmo.size;
  return true;
}

MappedResource::~MappedResource() {
  if (address_ != nullptr) {
    zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(address_), size_);
    address_ = nullptr;
    size_ = 0;
  }
}

bool ElfSnapshot::Load(fdio_ns_t* namespc, const std::string& path) {
  fuchsia::mem::Buffer vmo;
  if (!OpenVmo(&vmo, namespc, path, /*executable=*/true)) {
    FX_LOGF(ERROR, LOG_TAG, "Failed to open VMO for %s", path.c_str());
    return false;
  }
  void* const handle = dlopen_vmo(vmo.vmo.get(), RTLD_LAZY | RTLD_LOCAL);
  if (handle == nullptr) {
    FX_LOGF(ERROR, LOG_TAG, "Failed to load ELF snapshot: %s (reason: %s)",
            path.c_str(), dlerror());
    return false;
  }
  handle_ = handle;
  return true;
}
const uint8_t* ElfSnapshot::Resolve(const char* symbol) const {
  if (handle_ == nullptr)
    return nullptr;
  void* const addr = dlsym(handle_, symbol);
  if (addr == nullptr) {
    FX_LOGF(ERROR, LOG_TAG, "Failed to resolve symbol: %s (reason: %s)", symbol,
            dlerror());
  }
  return reinterpret_cast<const uint8_t*>(addr);
}
ElfSnapshot::~ElfSnapshot() {
  dlclose(handle_);
}
const uint8_t* ElfSnapshot::VmData() const {
  return Resolve(kVmSnapshotDataCSymbol);
}
const uint8_t* ElfSnapshot::VmInstrs() const {
  return Resolve(kVmSnapshotInstructionsCSymbol);
}
const uint8_t* ElfSnapshot::IsolateData() const {
  return Resolve(kIsolateSnapshotDataCSymbol);
}
const uint8_t* ElfSnapshot::IsolateInstrs() const {
  return Resolve(kIsolateSnapshotInstructionsCSymbol);
}

}  // namespace dart_runner
