#include <interfaces/chain.h>
#include <node/context.h>
#include <logging.h>
#include <scheduler.h>
#include <util/time.h>

#include <chrono>
#include <memory>

namespace node {
namespace {

class ChainClientMin final : public interfaces::ChainClient
{
public:
    explicit ChainClientMin(NodeContext& ctx) : m_ctx(ctx) {}

    void registerRpcs() override
    {
        // Core RPC already registered via RegisterAllCoreRPCCommands(tableRPC) in init.cpp.
        LogPrintf("ChainClientMin: registerRpcs\n");
    }

    bool verify() override
    {
        // Nothing to verify for the minimal client.
        return true;
    }

    bool load() override
    {
        // Nothing to load for the minimal client.
        return true;
    }

    void start(CScheduler& /*scheduler*/) override
    {
        // Minimal client doesn't schedule anything.
        LogPrintf("ChainClientMin: start\n");
    }

    void flush() override
    {
        // Nothing to flush.
    }

    void stop() override
    {
        // Nothing to stop.
    }

    void setMockTime(int64_t /*time*/) override
    {
        // Minimal client ignores mocktime.
        // If you later want: SetMockTime(time); but that's global and may not be desired here.
    }

    void schedulerMockForward(std::chrono::seconds /*delta_seconds*/) override
    {
        // Minimal client doesn't own a mock scheduler.
    }

private:
    NodeContext& m_ctx;
};

} // namespace

std::unique_ptr<interfaces::ChainClient> MakeChainClient(NodeContext& context)
{
    return std::make_unique<ChainClientMin>(context);
}

} // namespace node
