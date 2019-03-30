// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstring>
#include <ctime>
#include <fmt/time.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/fatal/fatal.h"
#include "core/hle/service/fatal/fatal_p.h"
#include "core/hle/service/fatal/fatal_u.h"

namespace Service::Fatal {

Module::Interface::Interface(std::shared_ptr<Module> module, const char* name)
    : ServiceFramework(name), module(std::move(module)) {}

Module::Interface::~Interface() = default;

struct FatalInfo {
    enum class Architecture : s32 {
        AArch64,
        AArch32,
    };

    const char* ArchAsString() const {
        return arch == Architecture::AArch64 ? "AArch64" : "AArch32";
    }

    std::array<u64_le, 31> registers{};
    u64_le sp{};
    u64_le pc{};
    u64_le pstate{};
    u64_le afsr0{};
    u64_le afsr1{};
    u64_le esr{};
    u64_le far{};

    std::array<u64_le, 32> backtrace{};
    u64_le program_entry_point{};

    // Bit flags that indicate which registers have been set with values
    // for this context. The service itself uses these to determine which
    // registers to specifically print out.
    u64_le set_flags{};

    u32_le backtrace_size{};
    Architecture arch{};
    u32_le unk10{}; // TODO(ogniK): Is this even used or is it just padding?
};
static_assert(sizeof(FatalInfo) == 0x250, "FatalInfo is an invalid size");

enum class FatalType : u32 {
    ErrorReportAndScreen = 0,
    ErrorReport = 1,
    ErrorScreen = 2,
};

static void GenerateErrorReport(ResultCode error_code, const FatalInfo& info) {
    const auto title_id = Core::CurrentProcess()->GetTitleID();
    std::string crash_report = fmt::format(
        "Yuzu {}-{} crash report\n"
        "Title ID:                        {:016x}\n"
        "Result:                          0x{:X} ({:04}-{:04d})\n"
        "Set flags:                       0x{:16X}\n"
        "Program entry point:             0x{:16X}\n"
        "\n",
        Common::g_scm_branch, Common::g_scm_desc, title_id, error_code.raw,
        2000 + static_cast<u32>(error_code.module.Value()),
        static_cast<u32>(error_code.description.Value()), info.set_flags, info.program_entry_point);
    if (info.backtrace_size != 0x0) {
        crash_report += "Registers:\n";
        for (size_t i = 0; i < info.registers.size(); i++) {
            crash_report +=
                fmt::format("    X[{:02d}]:                       {:016x}\n", i, info.registers[i]);
        }
        crash_report += fmt::format("    SP:                          {:016x}\n", info.sp);
        crash_report += fmt::format("    PC:                          {:016x}\n", info.pc);
        crash_report += fmt::format("    PSTATE:                      {:016x}\n", info.pstate);
        crash_report += fmt::format("    AFSR0:                       {:016x}\n", info.afsr0);
        crash_report += fmt::format("    AFSR1:                       {:016x}\n", info.afsr1);
        crash_report += fmt::format("    ESR:                         {:016x}\n", info.esr);
        crash_report += fmt::format("    FAR:                         {:016x}\n", info.far);
        crash_report += "\nBacktrace:\n";
        for (size_t i = 0; i < info.backtrace_size; i++) {
            crash_report +=
                fmt::format("    Backtrace[{:02d}]:               {:016x}\n", i, info.backtrace[i]);
        }

        crash_report += fmt::format("Architecture:                    {}\n", info.ArchAsString());
        crash_report += fmt::format("Unknown 10:                      0x{:016x}\n", info.unk10);
    }

    LOG_ERROR(Service_Fatal, "{}", crash_report);

    const std::string crashreport_dir =
        FileUtil::GetUserPath(FileUtil::UserPath::LogDir) + "crash_logs";

    if (!FileUtil::CreateFullPath(crashreport_dir)) {
        LOG_ERROR(
            Service_Fatal,
            "Unable to create crash report directory. Possible log directory permissions issue.");
        return;
    }

    const std::time_t t = std::time(nullptr);
    const std::string crashreport_filename =
        fmt::format("{}/{:016x}-{:%F-%H%M%S}.log", crashreport_dir, title_id, *std::localtime(&t));

    auto file = FileUtil::IOFile(crashreport_filename, "wb");
    if (file.IsOpen()) {
        file.WriteString(crash_report);
        LOG_ERROR(Service_Fatal, "Saving error report to {}", crashreport_filename);
    } else {
        LOG_ERROR(Service_Fatal, "Failed to save error report to {}", crashreport_filename);
    }
}

static void ThrowFatalError(ResultCode error_code, FatalType fatal_type, const FatalInfo& info) {
    LOG_ERROR(Service_Fatal, "Threw fatal error type {} with error code 0x{:X}",
              static_cast<u32>(fatal_type), error_code.raw);
    switch (fatal_type) {
    case FatalType::ErrorReportAndScreen:
        GenerateErrorReport(error_code, info);
        [[fallthrough]];
    case FatalType::ErrorScreen:
        // Since we have no fatal:u error screen. We should just kill execution instead
        ASSERT(false);
        break;
        // Should not throw a fatal screen but should generate an error report
    case FatalType::ErrorReport:
        GenerateErrorReport(error_code, info);
        break;
    };
}

void Module::Interface::ThrowFatal(Kernel::HLERequestContext& ctx) {
    LOG_ERROR(Service_Fatal, "called");
    IPC::RequestParser rp{ctx};
    auto error_code = rp.Pop<ResultCode>();

    ThrowFatalError(error_code, FatalType::ErrorScreen, {});
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Module::Interface::ThrowFatalWithPolicy(Kernel::HLERequestContext& ctx) {
    LOG_ERROR(Service_Fatal, "called");
    IPC::RequestParser rp(ctx);
    auto error_code = rp.Pop<ResultCode>();
    auto fatal_type = rp.PopEnum<FatalType>();

    ThrowFatalError(error_code, fatal_type, {}); // No info is passed with ThrowFatalWithPolicy
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Module::Interface::ThrowFatalWithCpuContext(Kernel::HLERequestContext& ctx) {
    LOG_ERROR(Service_Fatal, "called");
    IPC::RequestParser rp(ctx);
    auto error_code = rp.Pop<ResultCode>();
    auto fatal_type = rp.PopEnum<FatalType>();
    auto fatal_info = ctx.ReadBuffer();
    FatalInfo info{};

    ASSERT_MSG(fatal_info.size() == sizeof(FatalInfo), "Invalid fatal info buffer size!");
    std::memcpy(&info, fatal_info.data(), sizeof(FatalInfo));

    ThrowFatalError(error_code, fatal_type, info);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto module = std::make_shared<Module>();
    std::make_shared<Fatal_P>(module)->InstallAsService(service_manager);
    std::make_shared<Fatal_U>(module)->InstallAsService(service_manager);
}

} // namespace Service::Fatal
