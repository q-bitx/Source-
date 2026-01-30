#include <interfaces/init.h>
#include <interfaces/node.h>
#include <node/context.h>
#include <interfaces/chain.h>
#ifdef ENABLE_WALLET
#include <interfaces/wallet.h>
#include <common/args.h>
#include <util/check.h>
#endif
#include <memory>
//#include <interfaces/chainclient.h>
//#include <node/interfaces.h>

namespace interfaces {

class InitImpl : public Init
{
public:
    explicit InitImpl(node::NodeContext& context) : m_context(context) {}

    std::unique_ptr<Node> makeNode() override {
        return MakeNode(m_context);
    }

    std::unique_ptr<ChainClient> makeChainClient() override {
        return node::MakeChainClient(m_context);
    }

    std::unique_ptr<Chain> makeChain() override {
    return MakeChain(m_context);
    }

#ifdef ENABLE_WALLET
    std::unique_ptr<WalletLoader> makeWalletLoader(Chain& chain) override {
        return MakeWalletLoader(chain, *Assert(m_context.args));
    }
#endif

private:
    node::NodeContext& m_context;
};

std::unique_ptr<Init> MakeNodeInit(node::NodeContext& context, int argc, char* argv[], int& exit_status)
{
    return std::make_unique<InitImpl>(context);
}

} // namespace interfaces
