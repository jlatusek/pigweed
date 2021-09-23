// Copyright 2020 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "pw_rpc/client.h"

#include "pw_log/log.h"
#include "pw_rpc/internal/packet.h"
#include "pw_status/try.h"

namespace pw::rpc {
namespace {

using internal::Packet;
using internal::PacketType;

}  // namespace

Status Client::ProcessPacket(ConstByteSpan data) {
  internal::Call* base;
  Result<Packet> result = Endpoint::ProcessPacket(data, Packet::kClient, base);

  PW_TRY(result.status());

  Packet& packet = *result;

  internal::Channel* channel = GetInternalChannel(packet.channel_id());
  if (channel == nullptr) {
    PW_LOG_WARN("RPC client received a packet for an unregistered channel");
    return Status::Unavailable();
  }

  if (base == nullptr) {
    PW_LOG_WARN("RPC client received a packet for a request it did not make");
    // Don't send responses to errors to avoid infinite error cycles.
    if (packet.type() != PacketType::SERVER_ERROR) {
      channel->Send(Packet::ClientError(packet, Status::FailedPrecondition()))
          .IgnoreError();
    }
    return OkStatus();  // OK since the packet was handled
  }

  internal::ClientCall& call = *static_cast<internal::ClientCall*>(base);

  switch (packet.type()) {
    case PacketType::RESPONSE:
      // RPCs without a server stream include a payload with the final packet.
      if (call.has_server_stream()) {
        static_cast<internal::StreamResponseClientCall&>(call).HandleCompleted(
            packet.status());
      } else {
        static_cast<internal::UnaryResponseClientCall&>(call).HandleCompleted(
            packet.payload(), packet.status());
      }
      break;
    case PacketType::SERVER_ERROR:
      call.HandleError(packet.status());
      break;
    case PacketType::SERVER_STREAM:
      if (call.has_server_stream()) {
        call.HandlePayload(packet.payload());
      } else {
        PW_LOG_DEBUG("Received SERVER_STREAM for RPC without a server stream");
        call.HandleError(Status::InvalidArgument());
        // Report the error to the server so it can abort the RPC.
        channel->Send(Packet::ClientError(packet, Status::InvalidArgument()))
            .IgnoreError();  // Errors are logged in Channel::Send.
      }
      break;
    default:
      PW_LOG_WARN("pw_rpc server unable to handle packet of type %u",
                  static_cast<unsigned>(packet.type()));
  }

  return OkStatus();  // OK since the packet was handled
}

}  // namespace pw::rpc
