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

#include <debug/AddrRanges.hh>
#include <debug/SimBricksMemSidechannel.hh>

#include <simbricks/mem_sidechannel.hh>

#include "base/chunk_generator.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "sim/sim_object.hh"
#include "simbricks/mem/proto.h"

namespace gem5 {
namespace simbricks {
namespace mem_sidechannel {

extern "C" {
#include <simbricks/mem/if.h>
}

Adapter::Adapter(const Params &p)
    : SimObject(p),
      MemSidechannelAdapter::Interface(*this),
      adapter(*this, *this),
      memPort(name() + ".memPort", this),
      sys(p.system) {
  DPRINTF(SimBricksMemSidechannel,
          "simbricks-mem_sidechannel: adapter constructed\n");

  adapter.cfgSetPollInterval(p.poll_interval);
  if (p.listen)
    adapter.listen(p.uxsocket_path, p.shm_path);
  else
    adapter.connect(p.uxsocket_path);
}

Adapter::~Adapter() {
}

void Adapter::init() {
  if (!memPort.isConnected())
    panic("Port of %s not connected!", name());

  reqId = sys->getRequestorId(this);

  adapter.init();
  SimObject::init();
}

void Adapter::handleInMsg(volatile union SimbricksProtoMemH2M *msg) {
  uint8_t ty = adapter.inType(msg);
  switch (ty) {
    case SIMBRICKS_PROTO_MEM_H2M_MSG_READ: {
      /* Read memory */
      volatile struct SimbricksProtoMemH2MRead *read_msg = &msg->read;
      volatile union SimbricksProtoMemM2H *out_msg = adapter.outAlloc();
      volatile struct SimbricksProtoMemM2HReadcomp *read_comp =
          &out_msg->readcomp;

      uint16_t req_len = read_msg->len;
      if (sizeof(*read_comp) + req_len > adapter.outMaxSize()) {
        panic(
            "%s read of size %u doesn't fit into SimBricks message queue. "
            "Consider sending smaller reads or bumping the size of queue "
            "entries.",
            __func__, req_len);
      }

      ChunkGenerator gen{read_msg->addr, read_msg->len, sys->cacheLineSize()};
      while (!gen.done()) {
        RequestPtr request_ptr = std::make_shared<Request>(
            gen.addr(), gen.size(), 0, sys->getRequestorId(this));
        Packet packet = Packet{request_ptr, MemCmd::ReadReq};
        packet.dataStatic(read_comp->data + gen.complete());
        memPort.sendFunctional(&packet);
        gen.next();
      }

      read_comp->req_id = read_msg->req_id;
      adapter.outSend(out_msg, SIMBRICKS_PROTO_MEM_M2H_MSG_READCOMP);
      break;
    }
    case SIMBRICKS_PROTO_MEM_H2M_MSG_WRITE:
    case SIMBRICKS_PROTO_MEM_H2M_MSG_WRITE_POSTED: {
      /* Write memory */
      volatile struct SimbricksProtoMemH2MWrite *write_msg = &msg->write;
      uint16_t req_len = write_msg->len;
      if (sizeof(*write_msg) + req_len > adapter.outMaxSize()) {
        panic(
            "%s write of size %u doesn't fit into SimBricks message queue. "
            "Consider sending smaller writes or bumping the size of queue "
            "entries.",
            __func__, req_len);
      }

      ChunkGenerator gen{write_msg->addr, write_msg->len, sys->cacheLineSize()};
      while (!gen.done()) {
        RequestPtr request_ptr = std::make_shared<Request>(
            gen.addr(), gen.size(), 0, sys->getRequestorId(this));
        Packet packet = Packet{request_ptr, MemCmd::WriteReq};
        packet.dataStatic(write_msg->data + gen.complete());
        memPort.sendFunctional(&packet);
        gen.next();
      }

      if (ty == SIMBRICKS_PROTO_MEM_H2M_MSG_WRITE) {
        volatile union SimbricksProtoMemM2H *out_msg = adapter.outAlloc();
        volatile struct SimbricksProtoMemM2HWritecomp *write_comp =
            &out_msg->writecomp;
        write_comp->req_id = write_msg->req_id;
        adapter.outSend(out_msg, SIMBRICKS_PROTO_MEM_M2H_MSG_WRITECOMP);
      }

      break;
    }

    default:
      panic("%s unsupported type=%x", __func__, ty);
  }

  adapter.inDone(msg);
}

void Adapter::startup() {
  adapter.startup();
}

}  // namespace mem_sidechannel
}  // namespace simbricks
}  // namespace gem5
