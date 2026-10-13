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

#pragma once

#include <Kaleidoscope.h>
#include <cstdint>

#include "kaleidoscope/plugin/Superkeys/includes.h"

namespace kaleidoscope {
namespace plugin {

/*
 * A table in EEPROM of timing values for single keys, by key position and
 * layer. Stock firmware keeps one hold timeout for every dual-use key
 * (Qukeys) and one timing set for every superkey; a key in this table
 * uses its own values instead, field by field, and the keyboard's values
 * for the fields it leaves at "default".
 *
 * Focus commands:
 *   timing.map       get/set the table: 9 values per entry,
 *                    `key layer hold overlap minHold minPrior skHold skOverlap skTimeout`,
 *                    the list ending with a key of 0. `key` is the keymap
 *                    position + 1 (1..80), `layer` is 0 for every layer or
 *                    the layer number + 1. A field of 65535 means "the
 *                    keyboard's default".
 *   timing.capacity  how many entries the table holds.
 *   timing.defaults  the keyboard's current values for the seven fields.
 */
class KeyTiming : public kaleidoscope::Plugin {
 public:
  static constexpr uint8_t CAPACITY = 48;
  static constexpr uint8_t ANY_LAYER = 0;
  static constexpr uint16_t DEFAULT_U16 = 0xFFFF;
  static constexpr uint8_t DEFAULT_U8 = 0xFF;

  struct __attribute__((packed)) Entry {
    uint8_t key;        // keymap position + 1; 0 or 0xFF: no entry
    uint8_t layer;      // 0: any layer; otherwise layer + 1
    uint16_t hold;      // Qukeys hold timeout, ms
    uint8_t overlap;    // Qukeys overlap threshold, percent
    uint8_t min_hold;   // Qukeys minimum hold time, ms
    uint8_t min_prior;  // Qukeys minimum prior interval, ms
    uint16_t sk_hold;   // superkey hold start, ms
    uint8_t sk_overlap; // superkey overlap threshold, ms
    uint16_t sk_timeout; // superkey trigger timeout, ms
  };

  struct QukeyTiming {
    uint16_t hold_timeout;
    uint8_t overlap_threshold;
    uint8_t minimum_hold_time;
    uint8_t minimum_prior_interval;
  };

  KeyTiming() {}

  EventHandlerResult onFocusEvent(const char *command);

  // Reserves the table's EEPROM slice and loads it. Called from the sketch
  // after every stock plugin has reserved its storage, so that stock
  // firmware and this one keep the same layout for everything else.
  static void reserve_storage();

  // Fills the fields of `timing` the table overrides for the key at
  // `addr` on `layer`; returns whether an entry was found.
  static bool qukey_timing(KeyAddr addr, uint8_t layer, QukeyTiming &timing);
  static bool superkey_timing(KeyAddr addr, uint8_t layer, Utils::SharedConfig &timing);

 private:
  static Entry entries_[CAPACITY];
  static uint16_t storage_base_;
  static bool enabled_;

  static const Entry *find(KeyAddr addr, uint8_t layer);
  static bool is_empty(const Entry &entry);
  static void load();
  static void save();
  static void send_map();
  static void read_map();
  static void send_defaults();
};

}  // namespace plugin
}  // namespace kaleidoscope

extern kaleidoscope::plugin::KeyTiming KeyTiming;
