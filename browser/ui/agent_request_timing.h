#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_REQUEST_TIMING_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_REQUEST_TIMING_H_

#include <optional>
#include "base/time/time.h"

namespace yee {
// Local monotonic timings. Native processing excludes file polling/write and
// CLI scheduling; user wait includes dialog presentation through its outcome.
class AgentRequestTiming {
 public:
  void Start(base::TimeTicks now) {
    started_ = now;
    waiting_.reset();
    user_wait_ = base::TimeDelta();
  }
  void BeginUserWait(base::TimeTicks now) {
    if (!waiting_)
      waiting_ = now;
  }
  void EndUserWait(base::TimeTicks now) {
    if (waiting_) {
      user_wait_ += now - *waiting_;
      waiting_.reset();
    }
  }
  base::TimeDelta Elapsed(base::TimeTicks now) const { return now - started_; }
  base::TimeDelta UserWait() const { return user_wait_; }

 private:
  base::TimeTicks started_;
  std::optional<base::TimeTicks> waiting_;
  base::TimeDelta user_wait_;
};
}  // namespace yee
#endif
