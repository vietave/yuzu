// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"

namespace FileSys {

RomFSFactory::RomFSFactory(Loader::AppLoader& app_loader, ContentProvider& provider,
                           Service::FileSystem::FileSystemController& controller)
    : content_provider{provider}, filesystem_controller{controller} {
    // Load the RomFS from the app
    if (app_loader.ReadRomFS(file) != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_FS, "Unable to read RomFS!");
    }

    updatable = app_loader.IsRomFSUpdatable();
    ivfc_offset = app_loader.ReadRomFSIVFCOffset();
}

RomFSFactory::~RomFSFactory() = default;

void RomFSFactory::SetPackedUpdate(VirtualFile update_raw) {
    this->update_raw = std::move(update_raw);
}

ResultVal<VirtualFile> RomFSFactory::OpenCurrentProcess(u64 current_process_title_id) const {
    if (!updatable)
        return MakeResult<VirtualFile>(file);

    const PatchManager patch_manager(current_process_title_id);
    return MakeResult<VirtualFile>(
        patch_manager.PatchRomFS(file, ivfc_offset, ContentRecordType::Program, update_raw));
}

ResultVal<VirtualFile> RomFSFactory::Open(u64 title_id, StorageId storage,
                                          ContentRecordType type) const {
    const std::shared_ptr<NCA> res = GetEntry(title_id, storage, type);
    if (res == nullptr) {
        // TODO(DarkLordZach): Find the right error code to use here
        return RESULT_UNKNOWN;
    }

    const auto romfs = res->GetRomFS();
    if (romfs == nullptr) {
        // TODO(DarkLordZach): Find the right error code to use here
        return RESULT_UNKNOWN;
    }

    return MakeResult<VirtualFile>(romfs);
}

std::shared_ptr<NCA> RomFSFactory::GetEntry(u64 title_id, StorageId storage,
                                            ContentRecordType type) const {
    switch (storage) {
    case StorageId::None:
        return content_provider.GetEntry(title_id, type);
    case StorageId::NandSystem:
        return filesystem_controller.GetSystemNANDContents()->GetEntry(title_id, type);
    case StorageId::NandUser:
        return filesystem_controller.GetUserNANDContents()->GetEntry(title_id, type);
    case StorageId::SdCard:
        return filesystem_controller.GetSDMCContents()->GetEntry(title_id, type);
    case StorageId::Host:
    case StorageId::GameCard:
    default:
        UNIMPLEMENTED_MSG("Unimplemented storage_id={:02X}", static_cast<u8>(storage));
        return nullptr;
    }
}

} // namespace FileSys
