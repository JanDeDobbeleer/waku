#include <windows.h>

#include "features/quickshell_io/process_job.h"

namespace wabar::quickshell_io {

struct ProcessJob::Impl {
  HANDLE job = nullptr;
  bool adopted = false;

  void setKillOnClose(bool kill) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = kill ? JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE : 0;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
  }
};

ProcessJob::ProcessJob() : impl_(std::make_unique<Impl>()) {
  impl_->job = CreateJobObjectW(nullptr, nullptr);
  if (impl_->job) impl_->setKillOnClose(true);
}

ProcessJob::~ProcessJob() {
  if (impl_->job) CloseHandle(impl_->job);  // kills the tree unless release() ran
}

bool ProcessJob::adopt(qint64 pid) {
  if (!impl_->job || pid <= 0) return false;
  HANDLE process =
      OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (!process) return false;
  const bool ok = AssignProcessToJobObject(impl_->job, process) != FALSE;
  CloseHandle(process);
  impl_->adopted = ok;
  return ok;
}

bool ProcessJob::adopted() const { return impl_->adopted; }

void ProcessJob::terminate() {
  if (impl_->job && impl_->adopted) TerminateJobObject(impl_->job, 1);
}

void ProcessJob::release() {
  if (impl_->job) impl_->setKillOnClose(false);
}

}  // namespace wabar::quickshell_io
