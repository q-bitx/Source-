#ifndef QBITX_INTERFACES_SCHEDULER_H
#define QBITX_INTERFACES_SCHEDULER_H

#include <memory>

class CScheduler;

namespace interfaces {

class SchedulerClient
{
public:
    virtual ~SchedulerClient() = default;
    virtual void MaybeScheduleProcessQueue() = 0;
};

std::unique_ptr<SchedulerClient> MakeSchedulerClient(CScheduler& scheduler);

} // namespace interfaces

#endif // QBITX_INTERFACES_SCHEDULER_H
