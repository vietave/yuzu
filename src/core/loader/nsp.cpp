// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "common/common_types.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nca.h"
#include "core/loader/nsp.h"

namespace Loader {

AppLoader_NSP::AppLoader_NSP(FileSys::VirtualFile file)
    : AppLoader(file), nsp(std::make_unique<FileSys::NSP>(file)),
      title_id(nsp->GetProgramTitleID()) {

    if (nsp->GetStatus() != ResultStatus::Success)
        return;

    if (nsp->IsExtractedType()) {
        secondary_loader = std::make_unique<AppLoader_DeconstructedRomDirectory>(nsp->GetExeFS());
    } else {
        const auto control_nca =
            nsp->GetNCA(nsp->GetProgramTitleID(), FileSys::ContentRecordType::Control);
        if (control_nca == nullptr || control_nca->GetStatus() != ResultStatus::Success)
            return;

        std::tie(nacp_file, icon_file) =
            FileSys::PatchManager(nsp->GetProgramTitleID()).ParseControlNCA(*control_nca);

        if (title_id == 0)
            return;

        secondary_loader = std::make_unique<AppLoader_NCA>(
            nsp->GetNCAFile(title_id, FileSys::ContentRecordType::Program));
    }
}

AppLoader_NSP::~AppLoader_NSP() = default;

FileType AppLoader_NSP::IdentifyType(const FileSys::VirtualFile& file) {
    FileSys::NSP nsp(file);

    if (nsp.GetStatus() == ResultStatus::Success) {
        // Extracted Type case
        if (nsp.IsExtractedType() && nsp.GetExeFS() != nullptr &&
            FileSys::IsDirectoryExeFS(nsp.GetExeFS())) {
            return FileType::NSP;
        }

        // Non-Extracted Type case
        if (!nsp.IsExtractedType() &&
            nsp.GetNCA(nsp.GetFirstTitleID(), FileSys::ContentRecordType::Program) != nullptr &&
            AppLoader_NCA::IdentifyType(nsp.GetNCAFile(
                nsp.GetFirstTitleID(), FileSys::ContentRecordType::Program)) == FileType::NCA) {
            return FileType::NSP;
        }
    }

    return FileType::Error;
}

AppLoader_NSP::LoadResult AppLoader_NSP::Load(Kernel::Process& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (!nsp->IsExtractedType() && title_id == 0) {
        return {ResultStatus::ErrorNSPMissingProgramNCA, {}};
    }

    const auto nsp_status = nsp->GetStatus();
    if (nsp_status != ResultStatus::Success) {
        return {nsp_status, {}};
    }

    const auto nsp_program_status = nsp->GetProgramStatus(title_id);
    if (nsp_program_status != ResultStatus::Success) {
        return {nsp_program_status, {}};
    }

    if (!nsp->IsExtractedType() &&
        nsp->GetNCA(title_id, FileSys::ContentRecordType::Program) == nullptr) {
        if (!Core::Crypto::KeyManager::KeyFileExists(false)) {
            return {ResultStatus::ErrorMissingProductionKeyFile, {}};
        }

        return {ResultStatus::ErrorNSPMissingProgramNCA, {}};
    }

    const auto result = secondary_loader->Load(process, system);
    if (result.first != ResultStatus::Success) {
        return result;
    }

    FileSys::VirtualFile update_raw;
    if (ReadUpdateRaw(update_raw) == ResultStatus::Success && update_raw != nullptr) {
        system.GetFileSystemController().SetPackedUpdate(std::move(update_raw));
    }

    is_loaded = true;
    return result;
}

ResultStatus AppLoader_NSP::ReadRomFS(FileSys::VirtualFile& file) {
    return secondary_loader->ReadRomFS(file);
}

u64 AppLoader_NSP::ReadRomFSIVFCOffset() const {
    return secondary_loader->ReadRomFSIVFCOffset();
}

ResultStatus AppLoader_NSP::ReadUpdateRaw(FileSys::VirtualFile& file) {
    if (nsp->IsExtractedType())
        return ResultStatus::ErrorNoPackedUpdate;

    const auto read =
        nsp->GetNCAFile(FileSys::GetUpdateTitleID(title_id), FileSys::ContentRecordType::Program);

    if (read == nullptr)
        return ResultStatus::ErrorNoPackedUpdate;
    const auto nca_test = std::make_shared<FileSys::NCA>(read);

    if (nca_test->GetStatus() != ResultStatus::ErrorMissingBKTRBaseRomFS)
        return nca_test->GetStatus();

    file = read;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadProgramId(u64& out_program_id) {
    if (title_id == 0)
        return ResultStatus::ErrorNotInitialized;
    out_program_id = title_id;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadIcon(std::vector<u8>& buffer) {
    if (icon_file == nullptr)
        return ResultStatus::ErrorNoControl;
    buffer = icon_file->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadTitle(std::string& title) {
    if (nacp_file == nullptr)
        return ResultStatus::ErrorNoControl;
    title = nacp_file->GetApplicationName();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadControlData(FileSys::NACP& nacp) {
    if (nacp_file == nullptr)
        return ResultStatus::ErrorNoControl;
    nacp = *nacp_file;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadManualRomFS(FileSys::VirtualFile& file) {
    const auto nca =
        nsp->GetNCA(nsp->GetProgramTitleID(), FileSys::ContentRecordType::HtmlDocument);
    if (nsp->GetStatus() != ResultStatus::Success || nca == nullptr)
        return ResultStatus::ErrorNoRomFS;
    file = nca->GetRomFS();
    return file == nullptr ? ResultStatus::ErrorNoRomFS : ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadBanner(std::vector<u8>& buffer) {
    return secondary_loader->ReadBanner(buffer);
}

ResultStatus AppLoader_NSP::ReadLogo(std::vector<u8>& buffer) {
    return secondary_loader->ReadLogo(buffer);
}

ResultStatus AppLoader_NSP::ReadNSOModules(Modules& modules) {
    return secondary_loader->ReadNSOModules(modules);
}

} // namespace Loader
