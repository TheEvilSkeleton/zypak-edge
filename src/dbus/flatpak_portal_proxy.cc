// Copyright 2020 Endless Mobile, Inc.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus/flatpak_portal_proxy.h"

#include "base/debug.h"
#include "dbus/bus_message.h"
#include "dbus/bus_readable_message.h"
#include "dbus/bus_writable_message.h"

namespace zypak::dbus {

void FlatpakPortalProxy::AttachToBus(Bus* bus) {
  ZYPAK_ASSERT(bus_ == nullptr);
  bus_ = bus;
}

std::optional<std::uint32_t> FlatpakPortalProxy::GetVersionBlocking() {
  return GetUint32PropertyBlocking("version");
}

std::optional<FlatpakPortalProxy::Supports> FlatpakPortalProxy::GetSupportsBlocking() {
  if (auto supports = GetUint32PropertyBlocking("supports")) {
    return static_cast<Supports>(*supports);
  } else {
    return {};
  }
}

void FlatpakPortalProxy::SpawnAsync(std::string_view cwd, std::vector<std::string> argv,
                                    const FdMap& fds,
                                    std::unordered_map<std::string, std::string> env,
                                    SpawnFlags flags, SpawnOptions options,
                                    SpawnReplyHandler handler) {
  MethodCall call(kFlatpakPortalRef, "Spawn");
  MessageWriter writer = call.OpenWriter();

  writer.WriteFixedArray<TypeCode::kByte>(reinterpret_cast<const std::byte*>(cwd.data()),
                                          cwd.size() + 1);  // include null terminator

  {
    MessageWriter argv_writer = writer.EnterContainer<TypeCode::kArray>("ay");
    for (const std::string& arg : argv) {
      argv_writer.WriteFixedArray<TypeCode::kByte>(reinterpret_cast<const std::byte*>(arg.data()),
                                                   arg.size() + 1);  // include null terminator
    }
  }

  {
    MessageWriter fds_writer = writer.EnterContainer<TypeCode::kArray>("{uh}");
    for (const FdAssignment& assignment : fds) {
      MessageWriter pair_writer = fds_writer.EnterContainer<TypeCode::kDictEntry>();
      pair_writer.Write<TypeCode::kUInt32>(assignment.target());
      pair_writer.Write<TypeCode::kHandle>(assignment.fd().get());
    }
  }

  {
    MessageWriter env_writer = writer.EnterContainer<TypeCode::kArray>("{ss}");
    for (const auto& [var, value] : env) {
      MessageWriter pair_writer = env_writer.EnterContainer<TypeCode::kDictEntry>();
      pair_writer.Write<TypeCode::kString>(var);
      pair_writer.Write<TypeCode::kString>(value);
    }
  }

  writer.Write<TypeCode::kUInt32>(static_cast<std::uint32_t>(flags));

  {
    constexpr std::string_view kOptionSandboxFlags = "sandbox-flags";

    MessageWriter options_writer = writer.EnterContainer<TypeCode::kArray>("{sv}");
    MessageWriter pair_writer = options_writer.EnterContainer<TypeCode::kDictEntry>();
    pair_writer.Write<TypeCode::kString>(kOptionSandboxFlags);

    MessageWriter value_writer = pair_writer.EnterContainer<TypeCode::kVariant>("u");
    value_writer.Write<TypeCode::kUInt32>(static_cast<std::uint32_t>(options.sandbox_flags));
  }

  bus_->CallAsync(std::move(call), [handler2 = std::move(handler)](Reply reply) {
    if (std::optional<InvocationError> error = reply.ReadError()) {
      handler2(std::move(*error));
      return;
    }

    MessageReader reader = reply.OpenReader();
    std::uint32_t pid;
    if (!reader.Read<TypeCode::kUInt32>(&pid)) {
      Log() << "Failed to read u32 pid from Spawn reply";
      return;
    } else {
      handler2(pid);
    }
  });
}

std::optional<InvocationError> FlatpakPortalProxy::SpawnSignalBlocking(std::uint32_t pid,
                                                                       std::uint32_t signal) {
  MethodCall call(kFlatpakPortalRef, "SpawnSignal");
  MessageWriter writer = call.OpenWriter();

  writer.Write<TypeCode::kUInt32>(pid);
  writer.Write<TypeCode::kUInt32>(signal);

  Reply reply = bus_->CallBlocking(std::move(call));
  return reply.ReadError();
}

void FlatpakPortalProxy::SubscribeToSpawnStarted(SpawnStartedHandler handler) {
  bus_->SignalConnect(kFlatpakPortalRef.interface().data(), "SpawnStarted",
                      [handler2 = std::move(handler)](Signal signal) {
                        MessageReader reader = signal.OpenReader();
                        SpawnStartedMessage message;
                        if (!reader.Read<TypeCode::kUInt32>(&message.external_pid) ||
                            !reader.Read<TypeCode::kUInt32>(&message.internal_pid)) {
                          Log() << "Failed to read SpawnStarted message";
                        } else {
                          handler2(message);
                        }
                      });
}

void FlatpakPortalProxy::SubscribeToSpawnExited(SpawnExitedHandler handler) {
  bus_->SignalConnect(kFlatpakPortalRef.interface().data(), "SpawnExited",
                      [handler2 = std::move(handler)](Signal signal) {
                        MessageReader reader = signal.OpenReader();
                        SpawnExitedMessage message;
                        if (!reader.Read<TypeCode::kUInt32>(&message.external_pid) ||
                            !reader.Read<TypeCode::kUInt32>(&message.exit_status)) {
                          Log() << "Failed to read SpawnExited message";
                        } else {
                          handler2(message);
                        }
                      });
}

std::optional<std::uint32_t> FlatpakPortalProxy::GetUint32PropertyBlocking(std::string_view name) {
  Bus::PropertyResult<TypeCode::kUInt32> reply =
      bus_->GetPropertyBlocking<TypeCode::kUInt32>(kFlatpakPortalRef, name);

  if (auto* value = std::get_if<std::uint32_t>(&reply)) {
    return *value;
  } else if (auto* error = std::get_if<InvocationError>(&reply)) {
    Log() << "Error retrieving " << name << " property: " << *error;
  } else {
    Log() << "Unknown error retrieving " << name << " property";
  }

  return {};
}

}  // namespace zypak::dbus