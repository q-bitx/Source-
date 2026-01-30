#include <interfaces/scheduler.h>
#include <scheduler.h>

namespace interfaces {

class ImplSchedulerClient : public SchedulerClient
{
    CScheduler& m_scheduler;

public:
    explicit ImplSchedulerClient(CScheduler& scheduler) : m_scheduler(scheduler) {}

    void MaybeScheduleProcessQueue() override {
	m_scheduler.schedule([]{}, std::chrono::steady_clock::now() + std::chrono::milliseconds(0));
    }
};

std::unique_ptr<SchedulerClient> MakeSchedulerClient(CScheduler& scheduler)
{
    return std::make_unique<ImplSchedulerClient>(scheduler);
}

} // namespace interfaces
