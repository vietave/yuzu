// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <tuple>
#include <utility>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/session.h"
#include "core/hle/kernel/thread.h"
#include "core/memory.h"

namespace Kernel {

ServerSession::ServerSession(KernelCore& kernel) : SynchronizationObject{kernel} {}
ServerSession::~ServerSession() = default;

ResultVal<std::shared_ptr<ServerSession>> ServerSession::Create(KernelCore& kernel,
                                                                std::shared_ptr<Session> parent,
                                                                std::string name) {
    std::shared_ptr<ServerSession> session{std::make_shared<ServerSession>(kernel)};

    session->request_event =
        Core::Timing::CreateEvent(name, [session](std::uintptr_t, std::chrono::nanoseconds) {
            session->CompleteSyncRequest();
        });
    session->name = std::move(name);
    session->parent = std::move(parent);

    return MakeResult(std::move(session));
}

bool ServerSession::ShouldWait(const Thread* thread) const {
    // Closed sessions should never wait, an error will be returned from svcReplyAndReceive.
    if (!parent->Client()) {
        return false;
    }

    // Wait if we have no pending requests, or if we're currently handling a request.
    return pending_requesting_threads.empty() || currently_handling != nullptr;
}

bool ServerSession::IsSignaled() const {
    // Closed sessions should never wait, an error will be returned from svcReplyAndReceive.
    if (!parent->Client()) {
        return true;
    }

    // Wait if we have no pending requests, or if we're currently handling a request.
    return !pending_requesting_threads.empty() && currently_handling == nullptr;
}

void ServerSession::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");
    // We are now handling a request, pop it from the stack.
    // TODO(Subv): What happens if the client endpoint is closed before any requests are made?
    ASSERT(!pending_requesting_threads.empty());
    currently_handling = pending_requesting_threads.back();
    pending_requesting_threads.pop_back();
}

void ServerSession::ClientDisconnected() {
    // We keep a shared pointer to the hle handler to keep it alive throughout
    // the call to ClientDisconnected, as ClientDisconnected invalidates the
    // hle_handler member itself during the course of the function executing.
    std::shared_ptr<SessionRequestHandler> handler = hle_handler;
    if (handler) {
        // Note that after this returns, this server session's hle_handler is
        // invalidated (set to null).
        handler->ClientDisconnected(SharedFrom(this));
    }

    // Clean up the list of client threads with pending requests, they are unneeded now that the
    // client endpoint is closed.
    pending_requesting_threads.clear();
    currently_handling = nullptr;
}

void ServerSession::AppendDomainRequestHandler(std::shared_ptr<SessionRequestHandler> handler) {
    domain_request_handlers.push_back(std::move(handler));
}

std::size_t ServerSession::NumDomainRequestHandlers() const {
    return domain_request_handlers.size();
}

ResultCode ServerSession::HandleDomainSyncRequest(Kernel::HLERequestContext& context) {
    if (!context.HasDomainMessageHeader()) {
        return RESULT_SUCCESS;
    }

    // Set domain handlers in HLE context, used for domain objects (IPC interfaces) as inputs
    context.SetDomainRequestHandlers(domain_request_handlers);

    // If there is a DomainMessageHeader, then this is CommandType "Request"
    const auto& domain_message_header = context.GetDomainMessageHeader();
    const u32 object_id{domain_message_header.object_id};
    switch (domain_message_header.command) {
    case IPC::DomainMessageHeader::CommandType::SendMessage:
        if (object_id > domain_request_handlers.size()) {
            LOG_CRITICAL(IPC,
                         "object_id {} is too big! This probably means a recent service call "
                         "to {} needed to return a new interface!",
                         object_id, name);
            UNREACHABLE();
            return RESULT_SUCCESS; // Ignore error if asserts are off
        }
        return domain_request_handlers[object_id - 1]->HandleSyncRequest(context);

    case IPC::DomainMessageHeader::CommandType::CloseVirtualHandle: {
        LOG_DEBUG(IPC, "CloseVirtualHandle, object_id=0x{:08X}", object_id);

        domain_request_handlers[object_id - 1] = nullptr;

        IPC::ResponseBuilder rb{context, 2};
        rb.Push(RESULT_SUCCESS);
        return RESULT_SUCCESS;
    }
    }

    LOG_CRITICAL(IPC, "Unknown domain command={}",
                 static_cast<int>(domain_message_header.command.Value()));
    ASSERT(false);
    return RESULT_SUCCESS;
}

ResultCode ServerSession::QueueSyncRequest(std::shared_ptr<Thread> thread,
                                           Core::Memory::Memory& memory) {
    u32* cmd_buf{reinterpret_cast<u32*>(memory.GetPointer(thread->GetTLSAddress()))};
    auto context =
        std::make_shared<HLERequestContext>(kernel, memory, SharedFrom(this), std::move(thread));

    context->PopulateFromIncomingCommandBuffer(kernel.CurrentProcess()->GetHandleTable(), cmd_buf);
    request_queue.Push(std::move(context));

    return RESULT_SUCCESS;
}

ResultCode ServerSession::CompleteSyncRequest() {
    ASSERT(!request_queue.Empty());

    auto& context = *request_queue.Front();

    ResultCode result = RESULT_SUCCESS;
    // If the session has been converted to a domain, handle the domain request
    if (IsDomain() && context.HasDomainMessageHeader()) {
        result = HandleDomainSyncRequest(context);
        // If there is no domain header, the regular session handler is used
    } else if (hle_handler != nullptr) {
        // If this ServerSession has an associated HLE handler, forward the request to it.
        result = hle_handler->HandleSyncRequest(context);
    }

    if (convert_to_domain) {
        ASSERT_MSG(IsSession(), "ServerSession is already a domain instance.");
        domain_request_handlers = {hle_handler};
        convert_to_domain = false;
    }

    // Some service requests require the thread to block
    {
        SchedulerLock lock(kernel);
        if (!context.IsThreadWaiting()) {
            context.GetThread().ResumeFromWait();
            context.GetThread().SetSynchronizationResults(nullptr, result);
        }
    }

    request_queue.Pop();

    return result;
}

ResultCode ServerSession::HandleSyncRequest(std::shared_ptr<Thread> thread,
                                            Core::Memory::Memory& memory,
                                            Core::Timing::CoreTiming& core_timing) {
    const ResultCode result = QueueSyncRequest(std::move(thread), memory);
    const auto delay = std::chrono::nanoseconds{kernel.IsMulticore() ? 0 : 20000};
    core_timing.ScheduleEvent(delay, request_event, {});
    return result;
}

} // namespace Kernel
