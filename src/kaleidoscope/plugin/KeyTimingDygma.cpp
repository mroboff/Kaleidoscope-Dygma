/* -*- mode: c++ -*-
 * KeyTimingDygma -- Per-key hold and tap timing for dual-use keys and superkeys
 * Copyright (C) 2026  Mark Roboff (Keyboard Curator firmware fork)
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "kaleidoscope/plugin/KeyTimingDygma.h"

#include <Kaleidoscope-EEPROM-Settings.h>
#include <Kaleidoscope-FocusSerial.h>

#include "kaleidoscope/plugin/Qukeys.h"
#include "kaleidoscope/plugin/SuperkeysHandler.h"

namespace kaleidoscope {
namespace plugin {

// The keyboard's superkey timing, kept by the superkeys handler.
extern SuperkeysHandler::Configurations configurations;

KeyTiming::Entry KeyTiming::entries_[KeyTiming::CAPACITY];
uint16_t KeyTiming::storage_base_ = 0;
bool KeyTiming::enabled_ = false;

static constexpr uint16_t STORAGE_BYTES = sizeof(KeyTiming::Entry) * KeyTiming::CAPACITY;
static constexpr uint16_t EEPROM_BYTES = 8192;

bool KeyTiming::is_empty(const Entry &entry) {
  return entry.key == 0 || entry.key == DEFAULT_U8;
}

void KeyTiming::reserve_storage() {
  // The table only exists when it fits after everything stock firmware
  // stores; otherwise the commands are not offered at all.
  if (::EEPROMSettings.used() + STORAGE_BYTES > EEPROM_BYTES) {
    enabled_ = false;
    return;
  }
  storage_base_ = ::EEPROMSettings.requestSlice(STORAGE_BYTES);
  enabled_ = true;
  load();
}

void KeyTiming::load() {
  Runtime.storage().get(storage_base_, entries_);
  // Erased flash reads as 0xFF: every such entry is empty.
  for (uint8_t i = 0; i < CAPACITY; i++) {
    if (is_empty(entries_[i])) {
      entries_[i].key = 0;
    }
  }
}

void KeyTiming::save() {
  Runtime.storage().put(storage_base_, entries_);
  Runtime.storage().commit();
}

const KeyTiming::Entry *KeyTiming::find(KeyAddr addr, uint8_t layer) {
  if (!enabled_ || !addr.isValid()) {
    return nullptr;
  }
  uint8_t key = static_cast<uint8_t>(addr.toInt() + 1);
  const Entry *any_layer = nullptr;
  for (uint8_t i = 0; i < CAPACITY; i++) {
    const Entry &entry = entries_[i];
    if (entry.key != key) {
      continue;
    }
    if (entry.layer == static_cast<uint8_t>(layer + 1)) {
      return &entry;
    }
    if (entry.layer == ANY_LAYER && any_layer == nullptr) {
      any_layer = &entry;
    }
  }
  return any_layer;
}

bool KeyTiming::qukey_timing(KeyAddr addr, uint8_t layer, QukeyTiming &timing) {
  const Entry *entry = find(addr, layer);
  if (entry == nullptr) {
    return false;
  }
  if (entry->hold != DEFAULT_U16) timing.hold_timeout = entry->hold;
  if (entry->overlap != DEFAULT_U8) timing.overlap_threshold = entry->overlap < 100 ? entry->overlap : 0;
  if (entry->min_hold != DEFAULT_U8) timing.minimum_hold_time = entry->min_hold;
  if (entry->min_prior != DEFAULT_U8) timing.minimum_prior_interval = entry->min_prior;
  return true;
}

bool KeyTiming::superkey_timing(KeyAddr addr, uint8_t layer, Utils::SharedConfig &timing) {
  const Entry *entry = find(addr, layer);
  if (entry == nullptr) {
    return false;
  }
  if (entry->sk_hold != DEFAULT_U16) timing.hold_start_ = entry->sk_hold;
  if (entry->sk_overlap != DEFAULT_U8) timing.overlap_threshold_ = entry->sk_overlap;
  if (entry->sk_timeout != DEFAULT_U16) timing.time_out_ = entry->sk_timeout;
  return true;
}

void KeyTiming::send_map() {
  for (uint8_t i = 0; i < CAPACITY; i++) {
    const Entry &e = entries_[i];
    if (is_empty(e)) {
      continue;
    }
    ::Focus.send(static_cast<uint16_t>(e.key),
                 static_cast<uint16_t>(e.layer),
                 e.hold,
                 static_cast<uint16_t>(e.overlap == DEFAULT_U8 ? DEFAULT_U16 : e.overlap),
                 static_cast<uint16_t>(e.min_hold == DEFAULT_U8 ? DEFAULT_U16 : e.min_hold),
                 static_cast<uint16_t>(e.min_prior == DEFAULT_U8 ? DEFAULT_U16 : e.min_prior),
                 e.sk_hold,
                 static_cast<uint16_t>(e.sk_overlap == DEFAULT_U8 ? DEFAULT_U16 : e.sk_overlap),
                 e.sk_timeout);
  }
  ::Focus.send(static_cast<uint16_t>(0));
}

static uint8_t narrow(uint16_t value) {
  if (value == KeyTiming::DEFAULT_U16 || value > 254) {
    return KeyTiming::DEFAULT_U8;
  }
  return static_cast<uint8_t>(value);
}

void KeyTiming::read_map() {
  uint8_t count = 0;
  while (!::Focus.isEOL() && count < CAPACITY) {
    uint16_t values[9];
    uint8_t got = 0;
    while (got < 9 && !::Focus.isEOL()) {
      ::Focus.read(values[got]);
      got++;
    }
    if (got < 9 || values[0] == 0 || values[0] == DEFAULT_U16) {
      break;
    }
    Entry &e = entries_[count];
    e.key = static_cast<uint8_t>(values[0]);
    e.layer = narrow(values[1]) == DEFAULT_U8 ? ANY_LAYER : static_cast<uint8_t>(values[1]);
    e.hold = values[2];
    e.overlap = narrow(values[3]);
    e.min_hold = narrow(values[4]);
    e.min_prior = narrow(values[5]);
    e.sk_hold = values[6];
    e.sk_overlap = narrow(values[7]);
    e.sk_timeout = values[8];
    count++;
  }
  // Whatever is left on the line is dropped, so that an oversized table
  // never leaves the keyboard half-written with the rest misread.
  while (!::Focus.isEOL()) {
    uint16_t discard;
    ::Focus.read(discard);
  }
  for (uint8_t i = count; i < CAPACITY; i++) {
    entries_[i].key = 0;
  }
  save();
}

void KeyTiming::send_defaults() {
  ::Focus.send(::qukeys.hold_timeout(),
               static_cast<uint16_t>(::qukeys.overlap_threshold()),
               static_cast<uint16_t>(::qukeys.minimum_hold_time()),
               static_cast<uint16_t>(::qukeys.minimum_prior_interval()),
               configurations.hold_start_,
               static_cast<uint16_t>(configurations.overlap_threshold_),
               configurations.time_out_);
}

EventHandlerResult KeyTiming::onFocusEvent(const char *command) {
  if (!enabled_) {
    return EventHandlerResult::OK;
  }
  if (::Focus.handleHelp(command, PSTR("timing.map\ntiming.capacity\ntiming.defaults")))
    return EventHandlerResult::OK;

  if (strncmp_P(command, PSTR("timing."), 7) != 0)
    return EventHandlerResult::OK;

  if (strcmp_P(command + 7, PSTR("map")) == 0) {
    if (::Focus.isEOL()) {
      send_map();
    } else {
      read_map();
    }
    return EventHandlerResult::EVENT_CONSUMED;
  }
  if (strcmp_P(command + 7, PSTR("capacity")) == 0) {
    ::Focus.send(static_cast<uint16_t>(CAPACITY));
    return EventHandlerResult::EVENT_CONSUMED;
  }
  if (strcmp_P(command + 7, PSTR("defaults")) == 0) {
    send_defaults();
    return EventHandlerResult::EVENT_CONSUMED;
  }
  return EventHandlerResult::OK;
}

}  // namespace plugin
}  // namespace kaleidoscope

kaleidoscope::plugin::KeyTiming KeyTiming;
