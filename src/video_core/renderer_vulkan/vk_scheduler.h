// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <stack>
#include <thread>
#include <utility>
#include "common/common_types.h"
#include "common/threadsafe_queue.h"
#include "video_core/renderer_vulkan/wrapper.h"

namespace Vulkan {

class CommandPool;
class MasterSemaphore;
class StateTracker;
class VKDevice;
class VKQueryCache;

/// The scheduler abstracts command buffer and fence management with an interface that's able to do
/// OpenGL-like operations on Vulkan command buffers.
class VKScheduler {
public:
    explicit VKScheduler(const VKDevice& device, StateTracker& state_tracker);
    ~VKScheduler();

    /// Returns the current command buffer tick.
    [[nodiscard]] u64 CurrentTick() const noexcept;

    /// Returns true when a tick has been triggered by the GPU.
    [[nodiscard]] bool IsFree(u64 tick) const noexcept;

    /// Waits for the given tick to trigger on the GPU.
    void Wait(u64 tick);

    /// Sends the current execution context to the GPU.
    void Flush(VkSemaphore semaphore = nullptr);

    /// Sends the current execution context to the GPU and waits for it to complete.
    void Finish(VkSemaphore semaphore = nullptr);

    /// Waits for the worker thread to finish executing everything. After this function returns it's
    /// safe to touch worker resources.
    void WaitWorker();

    /// Sends currently recorded work to the worker thread.
    void DispatchWork();

    /// Requests to begin a renderpass.
    void RequestRenderpass(VkRenderPass renderpass, VkFramebuffer framebuffer,
                           VkExtent2D render_area);

    /// Requests the current executino context to be able to execute operations only allowed outside
    /// of a renderpass.
    void RequestOutsideRenderPassOperationContext();

    /// Binds a pipeline to the current execution context.
    void BindGraphicsPipeline(VkPipeline pipeline);

    /// Assigns the query cache.
    void SetQueryCache(VKQueryCache& query_cache_) {
        query_cache = &query_cache_;
    }

    /// Send work to a separate thread.
    template <typename T>
    void Record(T&& command) {
        if (chunk->Record(command)) {
            return;
        }
        DispatchWork();
        (void)chunk->Record(command);
    }

    /// Returns the master timeline semaphore.
    [[nodiscard]] MasterSemaphore& GetMasterSemaphore() const noexcept {
        return *master_semaphore;
    }

private:
    class Command {
    public:
        virtual ~Command() = default;

        virtual void Execute(vk::CommandBuffer cmdbuf) const = 0;

        Command* GetNext() const {
            return next;
        }

        void SetNext(Command* next_) {
            next = next_;
        }

    private:
        Command* next = nullptr;
    };

    template <typename T>
    class TypedCommand final : public Command {
    public:
        explicit TypedCommand(T&& command) : command{std::move(command)} {}
        ~TypedCommand() override = default;

        TypedCommand(TypedCommand&&) = delete;
        TypedCommand& operator=(TypedCommand&&) = delete;

        void Execute(vk::CommandBuffer cmdbuf) const override {
            command(cmdbuf);
        }

    private:
        T command;
    };

    class CommandChunk final {
    public:
        void ExecuteAll(vk::CommandBuffer cmdbuf);

        template <typename T>
        bool Record(T& command) {
            using FuncType = TypedCommand<T>;
            static_assert(sizeof(FuncType) < sizeof(data), "Lambda is too large");

            if (command_offset > sizeof(data) - sizeof(FuncType)) {
                return false;
            }

            Command* current_last = last;

            last = new (data.data() + command_offset) FuncType(std::move(command));

            if (current_last) {
                current_last->SetNext(last);
            } else {
                first = last;
            }

            command_offset += sizeof(FuncType);
            return true;
        }

        bool Empty() const {
            return command_offset == 0;
        }

    private:
        Command* first = nullptr;
        Command* last = nullptr;

        std::size_t command_offset = 0;
        std::array<u8, 0x8000> data{};
    };

    struct State {
        VkRenderPass renderpass = nullptr;
        VkFramebuffer framebuffer = nullptr;
        VkExtent2D render_area = {0, 0};
        VkPipeline graphics_pipeline = nullptr;
    };

    void WorkerThread();

    void SubmitExecution(VkSemaphore semaphore);

    void AllocateNewContext();

    void InvalidateState();

    void EndPendingOperations();

    void EndRenderPass();

    void AcquireNewChunk();

    const VKDevice& device;
    StateTracker& state_tracker;

    std::unique_ptr<MasterSemaphore> master_semaphore;
    std::unique_ptr<CommandPool> command_pool;

    VKQueryCache* query_cache = nullptr;

    vk::CommandBuffer current_cmdbuf;

    std::unique_ptr<CommandChunk> chunk;
    std::thread worker_thread;

    State state;
    Common::SPSCQueue<std::unique_ptr<CommandChunk>> chunk_queue;
    Common::SPSCQueue<std::unique_ptr<CommandChunk>> chunk_reserve;
    std::mutex mutex;
    std::condition_variable cv;
    bool quit = false;
};

} // namespace Vulkan
