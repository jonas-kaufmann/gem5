/*
 * Copyright 2022 Max Planck Institute for Software Systems, and
 * National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __SIMBRICKS_MEM_HH__
#define __SIMBRICKS_MEM_HH__

#include <chrono>
#include <thread>

#include "mem/port.hh"
#include "params/SimBricksMemSidechannel.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/system.hh"
#include "simbricks/base.hh"

namespace gem5 {
namespace simbricks {
namespace mem_sidechannel {
extern "C" {
#include <simbricks/mem/if.h>
}

class MemPort : public RequestPort {
 public:
  MemPort(const std::string &name, SimObject *owner)
      : RequestPort(name, owner) {
  }

 protected:
  void recvRetrySnoopResp() override final {
    panic("%s was not expecting %s\n", name(), __func__);
  }

  void recvReqRetry() override final {
    panic("%s was not expecting %s\n", name(), __func__);
  }

  bool recvTimingResp(PacketPtr pkt) override final {
    panic("%s was not expecting %s\n", name(), __func__);
    return false;
  }

  void recvTimingSnoopReq(PacketPtr pkt) override final {
    panic("%s was not expecting %s\n", name(), __func__);
  }
};

class MemSidechannelAdapter
    : public base::GenericBaseAdapter<SimbricksProtoMemH2M,
                                      SimbricksProtoMemM2H> {
 public:
  MemSidechannelAdapter(SimObject &parent, Interface &intf_)
      : base::GenericBaseAdapter<SimbricksProtoMemH2M, SimbricksProtoMemM2H>(
            parent, intf_, false) {
    // Main goal here is to override processInEvent function
    this->inEvent =
        EventFunctionWrapper([this] { processInEvent(); }, "MemSidechannelIn",
                             false, this->inEvent.priority() - 1);
  }

 protected:
  void processInEvent() {
    // Process what we can
    while (poll())
      ;

    // Start pollThread, which will schedule the inEvent again, once a message
    // becomes available
    if (pollThread.joinable()) {
      pollThread.join();
    }
    pollThread = std::thread{[this] { this->pollThreadFn(); }};
  }

  void pollThreadFn() {
    while (!this->peek(std::numeric_limits<uint64_t>::max())) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Got message, enqueue handling it for the current tick
    this->eventq->lock();
    this->eventq->schedule(&this->inEvent, this->eventq->getCurTick());
    this->eventq->unlock();
  }

  std::thread pollThread{};
};

class Adapter : public SimObject, public MemSidechannelAdapter::Interface {
 protected:
  // SimBricks base adapter
  MemSidechannelAdapter adapter;

  void handleInMsg(volatile SimbricksProtoMemH2M *msg) override final;

  size_t introOutPrepare(void *data, size_t maxlen) override final {
    size_t introlen = sizeof(struct SimbricksProtoMemMemIntro);
    assert(introlen <= maxlen);
    memset(data, 0, introlen);
    return introlen;
  }

  void introInReceived(const void *data, size_t len) override final {
    struct SimbricksProtoMemHostIntro *mi =
        (struct SimbricksProtoMemHostIntro *)data;
    if (len < sizeof(*mi))
      panic("introInReceived: intro short");
  }

  void initIfParams(SimbricksBaseIfParams &p) override final {
    SimbricksMemIfDefaultParams(&p);
  }

  Port &getPort(const std::string &if_name,
                PortID idx = InvalidPortID) override final {
    if (if_name == "port") {
      return memPort;
    }
    return SimObject::getPort(if_name, idx);
  }

 public:
  PARAMS(SimBricksMemSidechannel);

  Adapter(const Params &p);
  ~Adapter();

  void init() override;

  virtual void startup() override;

 protected:
  MemPort memPort;
  System *sys;
  RequestorID reqId;
};

}  // namespace mem_sidechannel
}  // namespace simbricks
}  // namespace gem5

#endif  // __SIMBRICKS_MEM_HH__
